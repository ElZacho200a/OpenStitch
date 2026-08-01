// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/generate.hpp"

#include <algorithm>
#include <variant>

#include "openstitch/geometry/offset.hpp"
#include "openstitch/stitch_generation/lock.hpp"
#include "openstitch/stitch_generation/routing.hpp"
#include "openstitch/stitch_generation/running_stitch.hpp"
#include "openstitch/stitch_generation/satin.hpp"
#include "openstitch/stitch_generation/tatami.hpp"

namespace openstitch::stitch_generation {

namespace {

// Ajoute une polyligne à la séquence : un saut vers son premier point, puis
// les points cousus. Ignore les tracés dégénérés.
void emit_polyline(stitch::StitchSequence& sequence, const std::vector<Vec2um>& points,
                   ObjectId source, stitch::StitchPass pass = stitch::StitchPass::TopStitch) {
    if (points.size() < 2) {
        return;
    }
    // Un saut vers le premier point, sauf si l'on enchaîne directement depuis un
    // point cousu situé à la même position (passe précédente du même objet :
    // sous-couche -> lock -> satin). On saute toujours après un ColorChange, un
    // Jump ou une frontière d'objet, même à position identique.
    const bool chained = !sequence.commands.empty() &&
                         sequence.commands.back().type == stitch::CommandType::Stitch &&
                         sequence.commands.back().pos == points.front();
    if (!chained) {
        sequence.commands.push_back({points.front(), stitch::CommandType::Jump, source,
                                     stitch::StitchPass::Travel});
    }
    for (const Vec2um& p : points) {
        sequence.commands.push_back({p, stitch::CommandType::Stitch, source, pass});
    }
}

// Émet un remplissage : un point `jump` (ou le tout premier) devient un
// déplacement (Jump, aiguille relevée, passe Travel), un point `travel` une
// pénétration cachée (Stitch, passe Travel), les autres la couche supérieure
// (Stitch, TopStitch). Garantit qu'aucune couture ne traverse un trou.
void emit_fill(stitch::StitchSequence& sequence, const std::vector<FillStitch>& fill,
               ObjectId source) {
    bool started = false;
    for (const FillStitch& fs : fill) {
        if (!started || fs.jump) {
            sequence.commands.push_back(
                {fs.pos, stitch::CommandType::Jump, source, stitch::StitchPass::Travel});
        } else if (fs.travel) {
            sequence.commands.push_back(
                {fs.pos, stitch::CommandType::Stitch, source, stitch::StitchPass::Travel});
        } else {
            sequence.commands.push_back(
                {fs.pos, stitch::CommandType::Stitch, source, stitch::StitchPass::TopStitch});
        }
        started = true;
    }
}

void generate_running(stitch::StitchSequence& sequence, const document::VectorObject& source,
                      const document::EmbroideryObject& object,
                      const document::RunningStitchParams& params) {
    const auto stitchContour = [&](const geometry::Path& path) {
        const auto sampled = sample_path(path, params.stitch_length, params.min_length);
        const auto points = apply_repeats(sampled, params.repeats);
        emit_polyline(sequence, points, object.id);
    };
    for (const geometry::PathSet& set : source.paths) {
        stitchContour(set.outer);
        for (const geometry::Path& hole : set.holes) {
            stitchContour(hole);
        }
    }
}

void generate_satin(stitch::StitchSequence& sequence, const document::EmbroideryObject& object,
                    const document::SatinParams& params) {
    SatinConfig config;
    config.density = params.density;
    config.pull_compensation = params.pull_compensation;
    config.center_underlay = params.center_underlay;
    // Finitions (Lot 3) : mêmes valeurs d'énumération, graine = id objet.
    config.short_stitch = static_cast<ShortStitchMode>(static_cast<int>(params.short_stitch));
    config.split_stitch = static_cast<SplitStitchMode>(static_cast<int>(params.split_stitch));
    config.cap_start = static_cast<SatinCapType>(static_cast<int>(params.cap_start));
    config.cap_end = static_cast<SatinCapType>(static_cast<int>(params.cap_end));
    config.max_stitch_length = params.max_stitch_length;
    config.split_seed = object.id.value;
    // Sous-couches + compensation (Lot 4).
    config.underlay_edge = params.underlay_edge;
    config.underlay_zigzag = params.underlay_zigzag;
    config.pull_left = params.pull_left;
    config.pull_right = params.pull_right;
    config.push_start = params.push_start;
    config.push_end = params.push_end;
    // Avec barreaux (satin auto) : correspondance par sections + espacement
    // perpendiculaire. Sans barreaux (satin manuel/legacy) : ré-échantillonnage
    // par fraction d'abscisse.
    SatinResult result;
    if (params.rungs.size() >= 2) {
        std::vector<SatinRungSeg> rungs;
        rungs.reserve(params.rungs.size());
        for (const auto& r : params.rungs) {
            rungs.emplace_back(r.a, r.b);
        }
        result = fill_satin_columns(params.rail_a, params.rail_b, rungs, config);
    } else {
        result = fill_satin(params.rail_a, params.rail_b, config);
    }
    // Entrée/sortie (§12) : oriente le satin pour démarrer près de l'entrée et
    // finir près de la sortie (projection = point le plus proche des extrémités).
    if ((params.entry_point || params.exit_point) && result.satin.size() >= 2) {
        const Vec2um s = result.satin.front();
        const Vec2um e = result.satin.back();
        double normal = 0.0;
        double reversed = 0.0;
        if (params.entry_point) {
            normal += length_um(*params.entry_point - s);
            reversed += length_um(*params.entry_point - e);
        }
        if (params.exit_point) {
            normal += length_um(*params.exit_point - e);
            reversed += length_um(*params.exit_point - s);
        }
        if (reversed < normal) {
            std::reverse(result.satin.begin(), result.satin.end());
        }
    }

    // Sous-couches d'abord (passes distinctes), puis lock d'entrée, couche
    // supérieure, lock de sortie. Un seul lock par bout (jamais par sous-passe).
    for (const auto& u : result.underlays) {
        emit_polyline(sequence, u.points, object.id, stitch::StitchPass::Underlay);
    }
    if (params.lock_start != document::SatinLock::None && result.satin.size() >= 2) {
        const auto lk = lock_stitches(result.satin.front(), result.satin[1],
                                      static_cast<LockType>(static_cast<int>(params.lock_start)),
                                      params.lock_length, params.lock_passes);
        emit_polyline(sequence, lk, object.id, stitch::StitchPass::Lock);
    }
    emit_polyline(sequence, result.satin, object.id, stitch::StitchPass::TopStitch);
    if (params.lock_end != document::SatinLock::None && result.satin.size() >= 2) {
        const std::size_t n = result.satin.size();
        const auto lk = lock_stitches(result.satin[n - 1], result.satin[n - 2],
                                      static_cast<LockType>(static_cast<int>(params.lock_end)),
                                      params.lock_length, params.lock_passes);
        emit_polyline(sequence, lk, object.id, stitch::StitchPass::Lock);
    }
}

// Extrémités représentatives d'une colonne satin, pour le routage (§13) :
// milieux des barreaux d'about, sinon extrémités du rail A.
std::pair<Vec2um, Vec2um> column_endpoints(const document::SatinParams& p) {
    const auto mid = [](Vec2um a, Vec2um b) {
        return Vec2um{Micrometers{(a.x.value + b.x.value) / 2}, Micrometers{(a.y.value + b.y.value) / 2}};
    };
    if (p.rungs.size() >= 2) {
        return {mid(p.rungs.front().a, p.rungs.front().b), mid(p.rungs.back().a, p.rungs.back().b)};
    }
    if (p.rail_a.nodes.size() >= 2) {
        return {p.rail_a.nodes.front().pos, p.rail_a.nodes.back().pos};
    }
    return {Vec2um{}, Vec2um{}};
}

// Route un groupe de colonnes satin de même couleur (§13) : ordre et
// orientation minimisant les déplacements, liaisons courtes cousues en trajet
// caché (passe Travel, pas de coupe) plutôt qu'en sauts.
void generate_satin_group(stitch::StitchSequence& sequence,
                          const std::vector<const document::EmbroideryObject*>& group) {
    std::vector<RouteColumn> cols;
    cols.reserve(group.size());
    for (const auto* obj : group) {
        const auto& sp = std::get<document::SatinParams>(obj->params);
        const auto [s, e] = column_endpoints(sp);
        RouteColumn route{obj->id, s, e};
        if (sp.topology) {
            route.start_junction = sp.topology->start_junction;
            route.end_junction = sp.topology->end_junction;
        }
        cols.push_back(route);
    }
    const Vec2um origin =
        sequence.commands.empty() ? cols.front().start : sequence.commands.back().pos;
    const RoutePlan plan = route_columns(cols, origin, RoutingConfig{});

    for (const RouteStep& step : plan.steps) {
        const document::EmbroideryObject& obj = *group[step.column_index];
        document::SatinParams sp = std::get<document::SatinParams>(obj.params);
        const auto [s, e] = column_endpoints(sp);
        // L'orientation décidée par le routage est imposée via entrée/sortie
        // (chemin déjà testé dans generate_satin).
        sp.entry_point = step.reversed ? e : s;
        sp.exit_point = step.reversed ? s : e;

        stitch::StitchSequence tmp;
        generate_satin(tmp, obj, sp);
        if (tmp.commands.empty()) {
            continue;
        }
        // Position de la première pénétration de la colonne (cible de liaison).
        Vec2um firstStitch = tmp.commands.front().pos;
        for (const auto& c : tmp.commands) {
            if (c.type == stitch::CommandType::Stitch) {
                firstStitch = c.pos;
                break;
            }
        }
        if (step.connector == ConnectorKind::Underpath && !sequence.commands.empty()) {
            // Trajet caché : running stitch de la position courante vers l'entrée,
            // cousu (passe Travel) — remplace un saut, sans coupe.
            geometry::Path link;
            link.closed = false;
            link.nodes = {{sequence.commands.back().pos, geometry::NodeType::Corner, {}, {}},
                          {firstStitch, geometry::NodeType::Corner, {}, {}}};
            const auto up = sample_path(link, Micrometers{2'500}, Micrometers{500});
            // On saute up[0] (== position courante) et up[dernier] (== firstStitch,
            // fourni par la colonne) pour n'ajouter que les pénétrations cachées.
            for (std::size_t i = 1; i + 1 < up.size(); ++i) {
                sequence.commands.push_back(
                    {up[i], stitch::CommandType::Stitch, obj.id, stitch::StitchPass::Travel});
            }
            // On enchaîne la colonne sans son saut de tête (index 0).
            for (std::size_t i = 1; i < tmp.commands.size(); ++i) {
                sequence.commands.push_back(tmp.commands[i]);
            }
        } else {
            // Début de groupe ou liaison trop longue : on garde le saut de tête.
            for (const auto& c : tmp.commands) {
                sequence.commands.push_back(c);
            }
        }
    }
}

void generate_tatami(stitch::StitchSequence& sequence, const document::VectorObject& source,
                     const document::EmbroideryObject& object,
                     const document::TatamiParams& params) {
    for (const geometry::PathSet& set : source.paths) {
        // Retrait de bord (compensation de contour). Si le retrait fait
        // disparaître la forme, on remplit la forme brute.
        std::vector<geometry::PathSet> filled;
        if (params.inset.value > 0) {
            if (auto inset = geometry::inset_path_set(set, params.inset); inset && !inset->empty()) {
                filled = std::move(*inset);
            }
        }
        if (filled.empty()) {
            filled.push_back(set);
        }
        for (const geometry::PathSet& region : filled) {
            // Sous-couches d'abord (§15), puis couche supérieure.
            for (const auto& up : tatami_underlay(region, params)) {
                emit_polyline(sequence, up, object.id, stitch::StitchPass::Underlay);
            }
            emit_fill(sequence, fill_tatami(region, params), object.id);
        }
    }
}

}  // namespace

namespace {

// Une colonne satin auto route avec ses voisines : elle porte des barreaux
// (issue de `build_satin_columns`), par opposition à un satin manuel/legacy.
bool is_routable_satin(const document::EmbroideryObject& o) {
    if (!o.is_satin()) {
        return false;
    }
    return std::get<document::SatinParams>(o.params).rungs.size() >= 2;
}

}  // namespace

Result<stitch::StitchSequence> generate_sequence(const document::Project& project) {
    stitch::StitchSequence sequence;
    const auto& objects = project.embroidery_objects;
    const document::EmbroideryObject* previous = nullptr;

    for (std::size_t idx = 0; idx < objects.size();) {
        const document::EmbroideryObject& object = objects[idx];
        if (!object.visible) {
            ++idx;
            continue;
        }
        // Le satin porte sa géométrie ; les autres types suivent un vecteur.
        const document::VectorObject* source = nullptr;
        if (!object.is_satin()) {
            for (const auto& vec : project.vector_objects) {
                if (vec.id == object.source_vector) {
                    source = &vec;
                    break;
                }
            }
            if (source == nullptr) {
                return fail(ErrorCategory::Internal,
                            "Objet vectoriel source introuvable pour « " + object.name + " »",
                            "source_vector=" + std::to_string(object.source_vector.value));
            }
        }

        if (previous != nullptr && previous->rgb != object.rgb && !sequence.commands.empty()) {
            sequence.commands.push_back(
                {sequence.commands.back().pos, stitch::CommandType::ColorChange, object.id});
        }

        // Routage (§13) : un groupe **contigu** de colonnes satin auto de même
        // couleur et même source est ordonné/orienté ensemble, liaisons cachées.
        if (is_routable_satin(object)) {
            std::vector<const document::EmbroideryObject*> group{&object};
            std::size_t j = idx + 1;
            for (; j < objects.size(); ++j) {
                const auto& o = objects[j];
                if (!o.visible || !is_routable_satin(o) || o.rgb != object.rgb ||
                    o.source_vector != object.source_vector) {
                    break;
                }
                group.push_back(&o);
            }
            if (group.size() >= 2) {
                generate_satin_group(sequence, group);
            } else {
                generate_satin(sequence, object, std::get<document::SatinParams>(object.params));
            }
            previous = group.back();
            idx = j;
            continue;
        }

        std::visit(
            [&](const auto& params) {
                using T = std::decay_t<decltype(params)>;
                if constexpr (std::is_same_v<T, document::RunningStitchParams>) {
                    generate_running(sequence, *source, object, params);
                } else if constexpr (std::is_same_v<T, document::TatamiParams>) {
                    generate_tatami(sequence, *source, object, params);
                } else if constexpr (std::is_same_v<T, document::SatinParams>) {
                    generate_satin(sequence, object, params);
                }
            },
            object.params);
        previous = &object;
        ++idx;
    }

    if (sequence.commands.empty()) {
        return fail(ErrorCategory::UserInput, "Aucun objet de broderie visible : rien à générer");
    }
    sequence.commands.push_back(
        {sequence.commands.back().pos, stitch::CommandType::End, ObjectId{}});
    return sequence;
}

}  // namespace openstitch::stitch_generation
