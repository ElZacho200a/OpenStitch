// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/generate.hpp"

#include <algorithm>
#include <variant>

#include "openstitch/geometry/offset.hpp"
#include "openstitch/stitch_generation/lock.hpp"
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

// Émet un remplissage : chaque point marqué `travel` devient un déplacement
// (Jump, aiguille relevée), les autres des points cousus. Garantit qu'aucune
// couture ne traverse un trou ou ne sort de la région (routage du tatami).
void emit_fill(stitch::StitchSequence& sequence, const std::vector<FillStitch>& fill,
               ObjectId source) {
    bool started = false;
    for (const FillStitch& fs : fill) {
        const auto type = (fs.jump || !started) ? stitch::CommandType::Jump
                                                : stitch::CommandType::Stitch;
        sequence.commands.push_back({fs.pos, type, source});
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
            emit_fill(sequence, fill_tatami(region, params), object.id);
        }
    }
}

}  // namespace

Result<stitch::StitchSequence> generate_sequence(const document::Project& project) {
    stitch::StitchSequence sequence;
    const document::EmbroideryObject* previous = nullptr;

    for (const document::EmbroideryObject& object : project.embroidery_objects) {
        if (!object.visible) {
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
    }

    if (sequence.commands.empty()) {
        return fail(ErrorCategory::UserInput, "Aucun objet de broderie visible : rien à générer");
    }
    sequence.commands.push_back(
        {sequence.commands.back().pos, stitch::CommandType::End, ObjectId{}});
    return sequence;
}

}  // namespace openstitch::stitch_generation
