// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/generate.hpp"

#include <variant>

#include "openstitch/geometry/offset.hpp"
#include "openstitch/stitch_generation/running_stitch.hpp"
#include "openstitch/stitch_generation/satin.hpp"
#include "openstitch/stitch_generation/tatami.hpp"

namespace openstitch::stitch_generation {

namespace {

// Ajoute une polyligne à la séquence : un saut vers son premier point, puis
// les points cousus. Ignore les tracés dégénérés.
void emit_polyline(stitch::StitchSequence& sequence, const std::vector<Vec2um>& points,
                   ObjectId source) {
    if (points.size() < 2) {
        return;
    }
    sequence.commands.push_back({points.front(), stitch::CommandType::Jump, source});
    for (const Vec2um& p : points) {
        sequence.commands.push_back({p, stitch::CommandType::Stitch, source});
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
    const SatinResult result = fill_satin(params.rail_a, params.rail_b, config);
    emit_polyline(sequence, result.underlay, object.id);
    emit_polyline(sequence, result.satin, object.id);
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
