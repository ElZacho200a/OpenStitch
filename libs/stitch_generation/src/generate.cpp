// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/generate.hpp"

#include <algorithm>
#include <cmath>
#include <variant>

#include "openstitch/auto_satin/skeleton_satin.hpp"
#include "openstitch/core/parallel.hpp"
#include "openstitch/geometry/offset.hpp"
#include "openstitch/geometry/polyline.hpp"
#include "openstitch/stitch_generation/directional_fill.hpp"
#include "openstitch/stitch_generation/join.hpp"
#include "openstitch/stitch_generation/lock.hpp"
#include "openstitch/stitch_generation/routing.hpp"
#include "openstitch/stitch_generation/running_stitch.hpp"
#include "openstitch/stitch_generation/satin.hpp"
#include "openstitch/stitch_generation/tatami.hpp"
#include "openstitch/stitch_generation/underlay_auto.hpp"

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
        sequence.commands.push_back(
            {points.front(), stitch::CommandType::Jump, source, stitch::StitchPass::Travel});
    }
    for (const Vec2um& p : points) {
        sequence.commands.push_back({p, stitch::CommandType::Stitch, source, pass});
    }
}

// Comme `emit_polyline`, mais certains points internes (`jump_before`,
// indices triés dans `points`) sont eux aussi atteints par un saut plutôt
// qu'enchaînés depuis le point précédent -- cf. `SatinResult::jump_before` :
// une colonne satin dont deux barreaux consécutifs cessent de se comporter
// comme un ruban (coude serré) lève le fil plutôt que de forcer un point
// continu disproportionné qui traverserait visuellement la forme.
void emit_polyline_with_breaks(stitch::StitchSequence& sequence, const std::vector<Vec2um>& points,
                               const std::vector<std::size_t>& jump_before, ObjectId source,
                               stitch::StitchPass pass = stitch::StitchPass::TopStitch) {
    if (points.size() < 2) {
        return;
    }
    std::size_t nextBreak = 0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const bool isBreak =
            i == 0 || (nextBreak < jump_before.size() && jump_before[nextBreak] == i);
        if (isBreak) {
            if (nextBreak < jump_before.size() && jump_before[nextBreak] == i) {
                ++nextBreak;
            }
            const bool chained = !sequence.commands.empty() &&
                                 sequence.commands.back().type == stitch::CommandType::Stitch &&
                                 sequence.commands.back().pos == points[i];
            if (!chained) {
                sequence.commands.push_back(
                    {points[i], stitch::CommandType::Jump, source, stitch::StitchPass::Travel});
            }
        }
        sequence.commands.push_back({points[i], stitch::CommandType::Stitch, source, pass});
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
    // Sous-couche automatique (HP-ENG-002) : selon la largeur réelle de la colonne.
    if (params.underlay_mode == document::UnderlayMode::Auto) {
        const SatinUnderlayChoice c =
            choose_satin_underlay(satin_mean_width_mm(params.rail_a, params.rail_b));
        config.center_underlay = c.center;
        config.underlay_edge = c.edge;
        config.underlay_zigzag = c.zigzag;
    }
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
            const std::size_t n = result.satin.size();
            std::reverse(result.satin.begin(), result.satin.end());
            // `jump_before[k]` marquait l'arête (i-1, i) comme sautée ; une
            // fois la séquence inversée, ce même point physique se retrouve
            // avant la nouvelle position de l'ancien point (i-1), soit
            // n - i (cf. audit lettres/coude).
            for (std::size_t& idx : result.jump_before) {
                idx = n - idx;
            }
            std::sort(result.jump_before.begin(), result.jump_before.end());
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
    emit_polyline_with_breaks(sequence, result.satin, result.jump_before, object.id,
                              stitch::StitchPass::TopStitch);
    if (params.lock_end != document::SatinLock::None && result.satin.size() >= 2) {
        const std::size_t n = result.satin.size();
        const auto lk = lock_stitches(result.satin[n - 1], result.satin[n - 2],
                                      static_cast<LockType>(static_cast<int>(params.lock_end)),
                                      params.lock_length, params.lock_passes);
        emit_polyline(sequence, lk, object.id, stitch::StitchPass::Lock);
    }
}

// Auto-satin par squelette et traversées orientées (spec
// specs/plans/satin-squelette-traversees.md) : l'objet suit la région de son
// vecteur source, les traversées sont recalculées à chaque génération. Chaque
// colonne (branche du squelette) passe par les finitions communes du satin
// (`finish_satin_stations`) ; les colonnes sont enchaînées au plus proche.
// Paramètres du moteur de squelette pour un auto-satin (partagés entre la génération et le
// préchauffage parallèle du cache : mêmes entrées exactes, donc mêmes entrées de cache).
auto_satin::SkeletonSatinParameters auto_satin_engine(const document::AutoSatinParams& params) {
    auto_satin::SkeletonSatinParameters engine;
    engine.spacing = params.spacing;
    for (const auto& g : params.guides) {
        engine.guides.push_back({g.anchor, g.angle.radians, g.absolute});
    }
    return engine;
}

void generate_auto_satin(stitch::StitchSequence& sequence, const document::VectorObject& source,
                         const document::EmbroideryObject& object,
                         const document::AutoSatinParams& params) {
    const auto_satin::SkeletonSatinParameters engine = auto_satin_engine(params);

    SatinConfig config;
    config.density = params.spacing;
    config.pull_compensation = params.pull_compensation;
    config.center_underlay = params.center_underlay;
    config.short_stitch = static_cast<ShortStitchMode>(static_cast<int>(params.short_stitch));
    config.split_stitch = static_cast<SplitStitchMode>(static_cast<int>(params.split_stitch));
    config.max_stitch_length = params.split_threshold;
    config.split_length = params.split_length;
    config.split_connecting_throws = true;
    config.split_seed = object.id.value;
    config.cap_start = static_cast<SatinCapType>(static_cast<int>(params.cap_start));
    config.cap_end = static_cast<SatinCapType>(static_cast<int>(params.cap_end));
    config.underlay_edge = params.underlay_edge;
    config.underlay_zigzag = params.underlay_zigzag;
    config.pull_left = params.pull_left;
    config.pull_right = params.pull_right;
    config.push_start = params.push_start;
    config.push_end = params.push_end;
    // Sous-couche automatique (HP-ENG-002) : largeur moyenne de la région source.
    if (params.underlay_mode == document::UnderlayMode::Auto) {
        double area = 0.0;
        double perimeter = 0.0;
        for (const geometry::PathSet& set : source.paths) {
            const ShapeMetrics m = measure_shape(set);
            area += m.area_mm2;
            perimeter += m.perimeter_mm;
        }
        const SatinUnderlayChoice c =
            choose_satin_underlay(perimeter > 1e-9 ? 2.0 * area / perimeter : 0.0);
        config.center_underlay = c.center;
        config.underlay_edge = c.edge;
        config.underlay_zigzag = c.zigzag;
    }

    std::vector<std::vector<SatinStation>> columns;
    for (const geometry::PathSet& set : source.paths) {
        const auto result = auto_satin::generate_skeleton_satin(set, engine);
        if (!result) {
            continue; // région non analysable : aucun point pour ce morceau
        }
        for (const auto& column : result->columns) {
            std::vector<SatinStation> stations;
            stations.reserve(column.crossings.size());
            for (const auto& c : column.crossings) {
                stations.push_back({c.a, c.b, false, false});
            }
            if (stations.size() >= 2) {
                columns.push_back(std::move(stations));
            }
        }
    }
    if (columns.empty()) {
        return;
    }

    const auto midpoint = [](const SatinStation& st) {
        return Vec2um{Micrometers{(st.a.x.value + st.b.x.value) / 2},
                      Micrometers{(st.a.y.value + st.b.y.value) / 2}};
    };
    // Enchaînement glouton au plus proche : à chaque pas, la colonne et le sens
    // dont le DÉBUT est le plus proche de la position courante.
    struct Step {
        std::size_t column;
        bool reversed;
    };
    std::vector<Step> order;
    std::vector<char> taken(columns.size(), 0);
    Vec2um here = params.entry_point          ? *params.entry_point
                  : sequence.commands.empty() ? midpoint(columns.front().front())
                                              : sequence.commands.back().pos;
    for (std::size_t n = 0; n < columns.size(); ++n) {
        double best = -1.0;
        Step pick{0, false};
        for (std::size_t c = 0; c < columns.size(); ++c) {
            if (taken[c]) {
                continue;
            }
            for (const bool rev : {false, true}) {
                const auto& st = rev ? columns[c].back() : columns[c].front();
                const double d = length_um(midpoint(st) - here);
                if (best < 0.0 || d < best) {
                    best = d;
                    pick = {c, rev};
                }
            }
        }
        taken[pick.column] = 1;
        order.push_back(pick);
        const auto& last =
            pick.reversed ? columns[pick.column].front() : columns[pick.column].back();
        here = midpoint(last);
    }
    // Entrée/sortie : on inverse tout le parcours si cela rapproche le début de
    // l'entrée et la fin de la sortie (même règle que `generate_satin`).
    if (params.entry_point || params.exit_point) {
        const auto start = [&](const std::vector<Step>& o) {
            const auto& c = columns[o.front().column];
            return midpoint(o.front().reversed ? c.back() : c.front());
        };
        const auto finish = [&](const std::vector<Step>& o) {
            const auto& c = columns[o.back().column];
            return midpoint(o.back().reversed ? c.front() : c.back());
        };
        std::vector<Step> flipped(order.rbegin(), order.rend());
        for (auto& st : flipped) {
            st.reversed = !st.reversed;
        }
        const auto cost = [&](const std::vector<Step>& o) {
            double total = 0.0;
            if (params.entry_point) {
                total += length_um(*params.entry_point - start(o));
            }
            if (params.exit_point) {
                total += length_um(*params.exit_point - finish(o));
            }
            return total;
        };
        if (cost(flipped) < cost(order)) {
            order = std::move(flipped);
        }
    }

    for (std::size_t i = 0; i < order.size(); ++i) {
        auto stations = columns[order[i].column];
        if (order[i].reversed) {
            std::reverse(stations.begin(), stations.end());
        }
        const SatinResult result = finish_satin_stations(stations, config);
        for (const auto& u : result.underlays) {
            emit_polyline(sequence, u.points, object.id, stitch::StitchPass::Underlay);
        }
        if (i == 0 && params.lock_start != document::SatinLock::None && result.satin.size() >= 2) {
            const auto lk =
                lock_stitches(result.satin.front(), result.satin[1],
                              static_cast<LockType>(static_cast<int>(params.lock_start)),
                              params.lock_length, params.lock_passes);
            emit_polyline(sequence, lk, object.id, stitch::StitchPass::Lock);
        }
        emit_polyline_with_breaks(sequence, result.satin, result.jump_before, object.id,
                                  stitch::StitchPass::TopStitch);
        if (i + 1 == order.size() && params.lock_end != document::SatinLock::None &&
            result.satin.size() >= 2) {
            const std::size_t n = result.satin.size();
            const auto lk = lock_stitches(result.satin[n - 1], result.satin[n - 2],
                                          static_cast<LockType>(static_cast<int>(params.lock_end)),
                                          params.lock_length, params.lock_passes);
            emit_polyline(sequence, lk, object.id, stitch::StitchPass::Lock);
        }
    }
}

// Extrémités représentatives d'une colonne satin, pour le routage (§13) :
// milieux des barreaux d'about, sinon extrémités du rail A.
std::pair<Vec2um, Vec2um> column_endpoints(const document::SatinParams& p) {
    const auto mid = [](Vec2um a, Vec2um b) {
        return Vec2um{Micrometers{(a.x.value + b.x.value) / 2},
                      Micrometers{(a.y.value + b.y.value) / 2}};
    };
    // Reproduit le décalage longitudinal (`push_start`/`push_end`) réellement
    // appliqué par `fill_satin_columns` (`shiftEnd`, satin.cpp) — même borne
    // (jamais au-delà du barreau voisin). Sans cela, la décision de routage
    // (§ trajet caché / saut) se fonderait sur le milieu du barreau BRUT, pas
    // sur le point réellement cousu une fois la colonne décalée : un écart
    // évalué à quelques mm près du seuil pourrait, une fois le décalage réel
    // appliqué, dépasser le seuil (trajet caché non garanti par un fil traversant
    // un espace non couvert) ou, symétriquement, imposer un saut évitable —
    // défaut trouvé par revue.
    const auto shifted = [](Vec2um at, Vec2um away, Micrometers push) -> Vec2um {
        if (push.value == 0) {
            return at;
        }
        const double dx = static_cast<double>(at.x.value - away.x.value);
        const double dy = static_cast<double>(at.y.value - away.y.value);
        const double n = std::hypot(dx, dy);
        if (n < 1e-6) {
            return at;
        }
        const double amount = std::max(static_cast<double>(push.value), -(n - 1.0));
        return Vec2um{
            Micrometers{static_cast<std::int32_t>(std::lround(at.x.value + dx / n * amount))},
            Micrometers{static_cast<std::int32_t>(std::lround(at.y.value + dy / n * amount))}};
    };
    if (p.rungs.size() >= 2) {
        const Vec2um s = mid(p.rungs.front().a, p.rungs.front().b);
        const Vec2um e = mid(p.rungs.back().a, p.rungs.back().b);
        const Vec2um sNext = mid(p.rungs[1].a, p.rungs[1].b);
        const Vec2um eNext = mid(p.rungs[p.rungs.size() - 2].a, p.rungs[p.rungs.size() - 2].b);
        return {shifted(s, sNext, p.push_start), shifted(e, eNext, p.push_end)};
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
            if (auto inset = geometry::inset_path_set(set, params.inset);
                inset && !inset->empty()) {
                filled = std::move(*inset);
            }
        }
        if (filled.empty()) {
            filled.push_back(set);
        }
        for (const geometry::PathSet& region : filled) {
            // Sous-couches d'abord (§15), puis couche supérieure.
            for (const auto& up : tatami_underlay(region, resolve_underlay(params, region))) {
                emit_polyline(sequence, up, object.id, stitch::StitchPass::Underlay);
            }
            emit_fill(sequence, fill_tatami(region, params), object.id);
        }
    }
}

// Remplissage directionnel : même enveloppe que le tatami (retrait de bord,
// repli sur la forme brute si le retrait la fait disparaître, sous-couches
// puis couche supérieure), seul le générateur de lignes change.
void generate_directional(stitch::StitchSequence& sequence, const document::VectorObject& source,
                          const document::EmbroideryObject& object,
                          const document::DirectionalFillParams& params) {
    for (const geometry::PathSet& set : source.paths) {
        std::vector<geometry::PathSet> filled;
        if (params.inset.value > 0) {
            if (auto inset = geometry::inset_path_set(set, params.inset);
                inset && !inset->empty()) {
                filled = std::move(*inset);
            }
        }
        if (filled.empty()) {
            filled.push_back(set);
        }
        for (const geometry::PathSet& region : filled) {
            for (const auto& up : directional_underlay(region, resolve_underlay(params, region))) {
                emit_polyline(sequence, up, object.id, stitch::StitchPass::Underlay);
            }
            emit_fill(sequence, fill_directional(region, params), object.id);
        }
    }
}

} // namespace

namespace {

// Une colonne satin auto route avec ses voisines : elle porte des barreaux
// (issue de `build_satin_columns`), par opposition à un satin manuel/legacy.
bool is_routable_satin(const document::EmbroideryObject& o) {
    if (!o.is_satin()) {
        return false;
    }
    const auto& p = std::get<document::SatinParams>(o.params);
    // Un satin de bordure (HP-STI-004) est une boucle ou un tracé indépendant : jamais
    // routé avec ses voisins (le routage supposerait un réseau de colonnes jointives).
    return p.rungs.size() >= 2 && !p.border;
}

} // namespace

Result<stitch::StitchSequence> generate_sequence(const document::Project& project) {
    // S2a (AD-04) : un design importé est une donnée SOURCE, jamais
    // régénérée par objet -- restituée telle quelle, avant toute
    // construction depuis `embroidery_objects` (vide pour un projet
    // d'import). Remplace l'ancienne exception desktop
    // `MainWindow::sequenceImported_` (§17) : ce chemin est désormais le
    // SEUL, commun à tous les consommateurs de production via
    // `effective_sequence`.
    if (project.imported_design) {
        return project.imported_design->sequence;
    }
    stitch::StitchSequence sequence;
    const auto& objects = project.embroidery_objects;

    // Phase 1 -- plan : les « unités » de génération, dans l'ordre de couture. Une unité est un
    // objet seul, ou un groupe contigu de colonnes satin routées ensemble (§13).
    enum class Kind { Independent, AutoSatin, Sequential };
    struct Unit {
        std::size_t first{0};
        std::size_t count{1};
        const document::VectorObject* source{nullptr};
        Kind kind{Kind::Sequential};
    };
    std::vector<Unit> units;
    for (std::size_t idx = 0; idx < objects.size();) {
        const document::EmbroideryObject& object = objects[idx];
        if (!object.visible) {
            ++idx;
            continue;
        }
        // Le satin porte sa géométrie ; les autres types suivent un vecteur.
        Unit unit;
        unit.first = idx;
        if (!object.is_satin()) {
            for (const auto& vec : project.vector_objects) {
                if (vec.id == object.source_vector) {
                    unit.source = &vec;
                    break;
                }
            }
            if (unit.source == nullptr) {
                return fail(ErrorCategory::Internal,
                            "Objet vectoriel source introuvable pour « " + object.name + " »",
                            "source_vector=" + std::to_string(object.source_vector.value));
            }
        }
        if (is_routable_satin(object)) {
            std::size_t j = idx + 1;
            for (; j < objects.size(); ++j) {
                const auto& o = objects[j];
                if (!o.visible || !is_routable_satin(o) || o.rgb != object.rgb ||
                    o.source_vector != object.source_vector) {
                    break;
                }
            }
            unit.count = j - idx;
        } else if (object.is_auto_satin()) {
            unit.kind = Kind::AutoSatin;
        } else if (!object.is_satin()) {
            unit.kind = Kind::Independent;
        }
        idx += unit.count;
        units.push_back(unit);
    }

    // Phase 2 -- calcul parallèle de ce qui ne dépend pas de la position précédente. Les
    // remplissages et contours sont générés dans une séquence locale (leur seul lien avec
    // l'objet précédent est le saut d'entrée, toujours émis à une frontière d'objet) ; le
    // squelette d'un auto-satin (partie coûteuse) est calculé dans le cache partagé, que la
    // phase 3 relit sans recalcul. Chaque tâche n'écrit que dans sa propre case : le résultat ne
    // dépend ni du nombre de fils ni de leur ordonnancement.
    std::vector<std::size_t> parallelUnits;
    for (std::size_t u = 0; u < units.size(); ++u) {
        if (units[u].kind != Kind::Sequential) {
            parallelUnits.push_back(u);
        }
    }
    std::vector<stitch::StitchSequence> chunks(units.size());
    parallel_for(parallelUnits.size(), [&](std::size_t k) {
        const std::size_t u = parallelUnits[k];
        const Unit& unit = units[u];
        const document::EmbroideryObject& object = objects[unit.first];
        if (unit.kind == Kind::AutoSatin) {
            const auto& params = std::get<document::AutoSatinParams>(object.params);
            const auto engine = auto_satin_engine(params);
            for (const geometry::PathSet& set : unit.source->paths) {
                (void)auto_satin::generate_skeleton_satin(set, engine);
            }
            return;
        }
        stitch::StitchSequence& chunk = chunks[u];
        std::visit(
            [&](const auto& params) {
                using T = std::decay_t<decltype(params)>;
                if constexpr (std::is_same_v<T, document::RunningStitchParams>) {
                    generate_running(chunk, *unit.source, object, params);
                } else if constexpr (std::is_same_v<T, document::TatamiParams>) {
                    generate_tatami(chunk, *unit.source, object, params);
                } else if constexpr (std::is_same_v<T, document::DirectionalFillParams>) {
                    generate_directional(chunk, *unit.source, object, params);
                }
            },
            object.params);
    });

    // Phase 3 -- assemblage séquentiel, dans l'ordre de couture.
    const document::EmbroideryObject* previous = nullptr;
    for (std::size_t u = 0; u < units.size(); ++u) {
        const Unit& unit = units[u];
        const document::EmbroideryObject& object = objects[unit.first];
        if (previous != nullptr && previous->rgb != object.rgb && !sequence.commands.empty()) {
            sequence.commands.push_back(
                {sequence.commands.back().pos, stitch::CommandType::ColorChange, object.id});
        }

        // Routage (§13) : un groupe **contigu** de colonnes satin auto de même
        // couleur et même source est ordonné/orienté ensemble, liaisons cachées.
        if (is_routable_satin(object)) {
            std::vector<const document::EmbroideryObject*> group;
            for (std::size_t j = unit.first; j < unit.first + unit.count; ++j) {
                group.push_back(&objects[j]);
            }
            if (group.size() >= 2) {
                generate_satin_group(sequence, group);
            } else {
                document::SatinParams single = std::get<document::SatinParams>(object.params);
                // Entrée automatique (HP-ENG-010) : même règle que pour les autres colonnes.
                if (join_active(project, object) && !sequence.commands.empty() &&
                    !single.entry_point && !single.exit_point) {
                    single.entry_point = sequence.commands.back().pos;
                }
                generate_satin(sequence, object, single);
            }
            previous = group.back();
            continue;
        }

        // Entrée/sortie automatiques (HP-ENG-010) : l'objet est cousu dans le sens qui
        // minimise le déplacement depuis la fin de l'objet précédent. Un point d'entrée
        // explicite de l'objet est toujours respecté.
        const bool joinHere = join_active(project, object) && !sequence.commands.empty();
        const std::optional<Vec2um> previousEnd =
            joinHere ? std::optional<Vec2um>{sequence.commands.back().pos} : std::nullopt;

        if (unit.kind == Kind::Independent) {
            auto& chunk = chunks[u].commands;
            const auto* tatami = std::get_if<document::TatamiParams>(&object.params);
            if (joinHere && !(tatami != nullptr && tatami->entry_point)) {
                std::optional<Vec2um> nextStart;
                if (u + 1 < units.size() && units[u + 1].kind == Kind::Independent &&
                    !chunks[u + 1].commands.empty()) {
                    nextStart = chunks[u + 1].commands.front().pos;
                }
                chunk = orient_chunk(chunk, previousEnd, nextStart);
            }
            sequence.commands.insert(sequence.commands.end(), chunk.begin(), chunk.end());
            previous = &object;
            continue;
        }

        std::visit(
            [&](const auto& params) {
                using T = std::decay_t<decltype(params)>;
                if constexpr (std::is_same_v<T, document::SatinParams>) {
                    if (previousEnd && !params.entry_point && !params.exit_point) {
                        document::SatinParams joined = params;
                        joined.entry_point = previousEnd;
                        generate_satin(sequence, object, joined);
                    } else {
                        generate_satin(sequence, object, params);
                    }
                } else if constexpr (std::is_same_v<T, document::AutoSatinParams>) {
                    if (previousEnd && !params.entry_point && !params.exit_point) {
                        document::AutoSatinParams joined = params;
                        joined.entry_point = previousEnd;
                        generate_auto_satin(sequence, *unit.source, object, joined);
                    } else {
                        generate_auto_satin(sequence, *unit.source, object, params);
                    }
                }
            },
            object.params);
        previous = &object;
    }

    if (sequence.commands.empty()) {
        return fail(ErrorCategory::UserInput, "Aucun objet de broderie visible : rien à générer");
    }
    sequence.commands.push_back(
        {sequence.commands.back().pos, stitch::CommandType::End, ObjectId{}});
    return sequence;
}

} // namespace openstitch::stitch_generation
