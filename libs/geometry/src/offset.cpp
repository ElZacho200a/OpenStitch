// SPDX-License-Identifier: Apache-2.0
#include "openstitch/geometry/offset.hpp"

#include <clipper2/clipper.h>

#include "openstitch/geometry/clean.hpp"

namespace openstitch::geometry {

namespace {

Clipper2Lib::Path64 to_clipper(const Path& path) {
    Clipper2Lib::Path64 out;
    out.reserve(path.nodes.size());
    for (const PathNode& node : path.nodes) {
        out.emplace_back(node.pos.x.value, node.pos.y.value);
    }
    return out;
}

}  // namespace

Result<std::vector<PathSet>> inset_path_set(const PathSet& set, Micrometers delta) {
    if (delta.value == 0) {
        return std::vector<PathSet>{set};
    }

    Clipper2Lib::Paths64 subject;
    subject.push_back(to_clipper(set.outer));
    for (const Path& hole : set.holes) {
        subject.push_back(to_clipper(hole));
    }

    // delta > 0 = retrait intérieur -> offset négatif au sens Clipper.
    const auto solution = Clipper2Lib::InflatePaths(
        subject, static_cast<double>(-delta.value), Clipper2Lib::JoinType::Miter,
        Clipper2Lib::EndType::Polygon);

    // On repasse par le nettoyage pour reconstruire la hiérarchie trous/extérieur.
    std::vector<Path> raw;
    raw.reserve(solution.size());
    for (const auto& poly : solution) {
        Path path;
        path.closed = true;
        path.nodes.reserve(poly.size());
        for (const auto& pt : poly) {
            path.nodes.push_back(PathNode{
                Vec2um{Micrometers{static_cast<std::int32_t>(pt.x)},
                       Micrometers{static_cast<std::int32_t>(pt.y)}},
                NodeType::Corner, std::nullopt, std::nullopt});
        }
        raw.push_back(std::move(path));
    }
    return clean_to_path_sets(raw);
}

}  // namespace openstitch::geometry
