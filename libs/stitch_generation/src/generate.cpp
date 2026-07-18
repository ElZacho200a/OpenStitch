// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/generate.hpp"

#include "openstitch/stitch_generation/running_stitch.hpp"

namespace openstitch::stitch_generation {

Result<stitch::StitchSequence> generate_sequence(const document::Project& project) {
    stitch::StitchSequence sequence;
    const document::EmbroideryObject* previous = nullptr;

    for (const document::EmbroideryObject& object : project.embroidery_objects) {
        if (!object.visible) {
            continue;
        }
        const document::VectorObject* source = nullptr;
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

        if (previous != nullptr && previous->rgb != object.rgb && !sequence.commands.empty()) {
            sequence.commands.push_back({sequence.commands.back().pos,
                                         stitch::CommandType::ColorChange, object.id});
        }

        const auto stitchContour = [&](const geometry::Path& path) {
            const auto sampled =
                sample_path(path, object.params.stitch_length, object.params.min_length);
            const auto points = apply_repeats(sampled, object.params.repeats);
            if (points.size() < 2) {
                return;  // contour dégénéré : rien à coudre
            }
            sequence.commands.push_back({points[0], stitch::CommandType::Jump, object.id});
            for (const Vec2um& p : points) {
                sequence.commands.push_back({p, stitch::CommandType::Stitch, object.id});
            }
        };

        for (const geometry::PathSet& set : source->paths) {
            stitchContour(set.outer);
            for (const geometry::Path& hole : set.holes) {
                stitchContour(hole);
            }
        }
        previous = &object;
    }

    if (sequence.commands.empty()) {
        return fail(ErrorCategory::UserInput,
                    "Aucun objet de broderie visible : rien à générer");
    }
    sequence.commands.push_back(
        {sequence.commands.back().pos, stitch::CommandType::End, ObjectId{}});
    return sequence;
}

}  // namespace openstitch::stitch_generation
