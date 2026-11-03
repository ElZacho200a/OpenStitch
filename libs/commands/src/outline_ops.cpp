// SPDX-License-Identifier: Apache-2.0
#include "openstitch/commands/outline_ops.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

#include "openstitch/commands/project_commands.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/offset.hpp"
#include "openstitch/geometry/polyline.hpp"

namespace openstitch::commands {

namespace {

constexpr Micrometers kFlattenTolerance{50};

VectorOpResult fail(std::string message) {
    VectorOpResult result;
    result.error = std::move(message);
    return result;
}

} // namespace

VectorOpResult make_outline_command(document::Project& project, const std::vector<ObjectId>& ids,
                                    const OutlineOptions& options) {
    using geometry::PathSet;
    if (options.gap.value < 0) {
        return fail("L'écart du contour doit être positif ou nul.");
    }
    std::vector<PathSet> all;
    std::array<std::uint8_t, 3> rgb{0, 0, 0};
    std::size_t found = 0;
    for (const auto& object : project.vector_objects) {
        if (std::find(ids.begin(), ids.end(), object.id) == ids.end()) {
            continue;
        }
        ++found;
        rgb = object.rgb;
        for (const PathSet& set : object.paths) {
            all.push_back(geometry::flatten_to_polygon(set, kFlattenTolerance));
        }
    }
    if (found == 0 || all.empty()) {
        return fail("Aucune forme à contourner.");
    }
    auto united = geometry::union_polygons_closing(all, Micrometers{0});
    if (!united) {
        return fail(united.error().message);
    }

    std::vector<PathSet> result;
    // inset_path_set : delta > 0 = retrait ; < 0 = agrandissement. `by` = 0 : copie.
    const auto offsetSet = [](const PathSet& set,
                              std::int32_t delta) -> Result<std::vector<PathSet>> {
        if (delta == 0) {
            return std::vector<PathSet>{set};
        }
        return geometry::inset_path_set(set, Micrometers{delta});
    };
    for (const PathSet& set : *united) {
        PathSet solid;
        solid.outer = set.outer; // le contour suit l'enveloppe extérieure
        if (options.kind == OutlineKind::Satin) {
            auto inner = offsetSet(solid, -options.gap.value);
            auto outer = offsetSet(solid, -(options.gap.value + options.satin_width.value));
            if (!inner || !outer) {
                return fail("Décalage impossible.");
            }
            if (inner->empty() || outer->empty()) {
                continue;
            }
            PathSet ring;
            ring.outer = outer->front().outer;
            ring.holes.push_back(inner->front().outer);
            result.push_back(std::move(ring));
        } else {
            auto grown = offsetSet(solid, -options.gap.value);
            if (!grown) {
                return fail(grown.error().message);
            }
            for (PathSet& piece : *grown) {
                piece.holes.clear();
                result.push_back(std::move(piece));
            }
        }
        if (options.around_holes && options.kind != OutlineKind::Satin) {
            for (const auto& hole : set.holes) {
                PathSet holeSet;
                holeSet.outer = hole;
                // Le contour de trou s'écarte de la matière : retrait du trou lui-même.
                auto shrunk = offsetSet(holeSet, options.gap.value);
                if (shrunk) {
                    for (PathSet& piece : *shrunk) {
                        piece.holes.clear();
                        result.push_back(std::move(piece));
                    }
                }
            }
        }
    }
    if (result.empty()) {
        return fail("Le contour s'effondre : écart trop grand.");
    }

    document::VectorObject vector;
    vector.id = project.object_ids.next();
    vector.name = "Contour";
    vector.rgb = rgb;
    vector.paths = std::move(result);

    document::EmbroideryObject embroidery;
    embroidery.id = project.object_ids.next();
    embroidery.name = "Contour";
    embroidery.source_vector = vector.id;
    embroidery.rgb = rgb;
    embroidery.intent = document::EmbroideryIntent::ForcedUserChoice;
    if (options.kind == OutlineKind::Satin) {
        embroidery.params = document::AutoSatinParams{};
    } else {
        document::RunningStitchParams rp;
        rp.repeats = options.kind == OutlineKind::Triple ? 3 : 1;
        embroidery.params = rp;
    }

    VectorOpResult out;
    out.resulting_objects = 1;
    std::vector<document::VectorObject> vectors;
    vectors.push_back(std::move(vector));
    std::vector<document::EmbroideryObject> embroideries;
    embroideries.push_back(std::move(embroidery));
    out.command = std::make_unique<AddObjectBatchCommand>(std::move(vectors),
                                                          std::move(embroideries), "Contour");
    return out;
}

} // namespace openstitch::commands
