// SPDX-License-Identifier: Apache-2.0
#include "openstitch/document/footprint.hpp"

#include <algorithm>
#include <variant>

#include "openstitch/geometry/polyline.hpp"

namespace openstitch::document {

geometry::PathSet satin_polygon(const SatinParams& params, Micrometers tolerance) {
    geometry::PathSet set;
    const auto a = geometry::flatten(params.rail_a, tolerance).points;
    auto b = geometry::flatten(params.rail_b, tolerance).points;
    std::reverse(b.begin(), b.end());
    for (const auto& pt : a) {
        set.outer.nodes.push_back({pt, geometry::NodeType::Corner, {}, {}});
    }
    for (const auto& pt : b) {
        set.outer.nodes.push_back({pt, geometry::NodeType::Corner, {}, {}});
    }
    set.outer.closed = true;
    return set;
}

std::vector<geometry::PathSet> embroidery_footprint(const Project& project,
                                                    const EmbroideryObject& object,
                                                    Micrometers tolerance) {
    std::vector<geometry::PathSet> sets;
    if (!object.visible || std::holds_alternative<RunningStitchParams>(object.params)) {
        return sets;
    }
    if (const auto* satin = std::get_if<SatinParams>(&object.params)) {
        sets.push_back(satin_polygon(*satin, tolerance));
        return sets;
    }
    const auto* vec = project.findObject(object.source_vector);
    if (vec == nullptr) {
        return sets;
    }
    for (const auto& s : vec->paths) {
        sets.push_back(geometry::flatten_to_polygon(s, tolerance));
    }
    return sets;
}

} // namespace openstitch::document
