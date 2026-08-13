// SPDX-License-Identifier: Apache-2.0
#include "openstitch/satin_planning/overlap.hpp"

#include <sstream>

#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/offset.hpp"

namespace openstitch::satin_planning {

namespace {

const SatinRegion* find_region(const RegionSplitReport& split, std::size_t pathIndex) {
    for (const auto& r : split.regions) {
        if (r.path_index == pathIndex) return &r;
    }
    return nullptr;
}

// Dilate `region` de `overlap_distance` puis la recadre dans `bounds` (la
// geometrie exacte d'avant coupe, sans interstice). Repli sur `region`
// inchangee si la dilatation ou le recadrage echoue.
geometry::PathSet extend_toward(const geometry::PathSet& region, const geometry::PathSet& bounds,
                                 Micrometers overlapDistance) {
    const auto dilated = geometry::inset_path_set(region, -overlapDistance);
    if (!dilated.has_value() || dilated->empty()) return region;

    const auto clamped = geometry::intersect_polygons({(*dilated)[0]}, {bounds});
    if (!clamped.has_value() || clamped->empty()) return region;

    return (*clamped)[0];
}

}  // namespace

OverlapReport generate_overlaps(const RegionSplitReport& split, const OverlapParams& params) {
    OverlapReport report;

    for (const auto& candidate : split.merge_candidates) {
        const SatinRegion* first = find_region(split, candidate.first_path_index);
        const SatinRegion* second = find_region(split, candidate.second_path_index);
        if (first == nullptr || second == nullptr) continue;  // deja filtre par split_region, garde-fou

        RegionOverlap overlap;
        overlap.first_path_index = candidate.first_path_index;
        overlap.second_path_index = candidate.second_path_index;
        overlap.first_extended = extend_toward(first->region, candidate.merged_region, params.overlap_distance);
        overlap.second_extended = extend_toward(second->region, candidate.merged_region, params.overlap_distance);
        overlap.first_extended_area_mm2 = geometry::path_set_area_um2(overlap.first_extended) / 1e6;
        overlap.second_extended_area_mm2 = geometry::path_set_area_um2(overlap.second_extended) / 1e6;

        report.overlaps.push_back(std::move(overlap));
    }

    return report;
}

std::string format_overlap_report(const OverlapReport& report) {
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(3);

    out << "[SGSD phase 8 -- recouvrements entre regions adjacentes]\n\n";
    for (const auto& o : report.overlaps) {
        out << "chemins " << o.first_path_index << "+" << o.second_path_index
            << " : premier=" << o.first_extended_area_mm2 << "mm2 second=" << o.second_extended_area_mm2 << "mm2\n";
    }
    out << "\nTotal : " << report.overlaps.size() << " recouvrement(s)\n";
    return out.str();
}

}  // namespace openstitch::satin_planning
