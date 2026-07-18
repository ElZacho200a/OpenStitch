// SPDX-License-Identifier: Apache-2.0
#include "openstitch/geometry/clean.hpp"

#include <clipper2/clipper.h>

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

Path from_clipper(const Clipper2Lib::Path64& path) {
    Path out;
    out.closed = true;
    out.nodes.reserve(path.size());
    for (const auto& pt : path) {
        out.nodes.push_back(PathNode{
            Vec2um{Micrometers{static_cast<std::int32_t>(pt.x)},
                   Micrometers{static_cast<std::int32_t>(pt.y)}},
            NodeType::Corner, std::nullopt, std::nullopt});
    }
    return out;
}

// Parcourt l'arbre Clipper : chaque niveau pair est un contour extérieur,
// ses enfants directs sont ses trous, et les enfants des trous repartent
// comme extérieurs indépendants.
void collect(const Clipper2Lib::PolyPath64& outer, std::vector<PathSet>& out) {
    PathSet set;
    set.outer = from_clipper(outer.Polygon());
    for (const auto& hole : outer) {
        set.holes.push_back(from_clipper(hole->Polygon()));
    }
    out.push_back(std::move(set));
    for (const auto& hole : outer) {
        for (const auto& nested : *hole) {
            collect(*nested, out);
        }
    }
}

}  // namespace

Result<std::vector<PathSet>> clean_to_path_sets(const std::vector<Path>& raw) {
    Clipper2Lib::Paths64 subject;
    for (const Path& path : raw) {
        if (path.nodes.size() >= 3) {
            subject.push_back(to_clipper(path));
        }
    }
    if (subject.empty()) {
        return std::vector<PathSet>{};
    }

    Clipper2Lib::Clipper64 clipper;
    clipper.AddSubject(subject);
    Clipper2Lib::PolyTree64 tree;
    if (!clipper.Execute(Clipper2Lib::ClipType::Union, Clipper2Lib::FillRule::EvenOdd, tree)) {
        return fail(ErrorCategory::Internal, "Échec du nettoyage géométrique",
                    "Clipper64::Execute a renvoyé false");
    }

    std::vector<PathSet> out;
    for (const auto& top : tree) {
        collect(*top, out);
    }
    return out;
}

}  // namespace openstitch::geometry
