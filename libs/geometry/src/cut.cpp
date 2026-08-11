// SPDX-License-Identifier: Apache-2.0
#include "openstitch/geometry/cut.hpp"

#include <clipper2/clipper.h>

#include <algorithm>
#include <cmath>
#include <limits>

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

// Même parcours d'arbre que clean.cpp (chaque .cpp de geometry reste
// autonome vis-à-vis de Clipper2 -- aucun type Clipper ne traverse une
// frontière de fichier, encore moins de bibliothèque, cf. ADR-005).
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

Result<std::vector<PathSet>> cut_path_set(const PathSet& region, Vec2um a, Vec2um b,
                                          Micrometers cut_width) {
    const double ax = static_cast<double>(a.x.value);
    const double ay = static_cast<double>(a.y.value);
    const double bx = static_cast<double>(b.x.value);
    const double by = static_cast<double>(b.y.value);
    const double dx = bx - ax;
    const double dy = by - ay;
    const double len = std::hypot(dx, dy);
    if (len < 1.0) {
        return std::vector<PathSet>{region};  // ligne dégénérée : rien à couper
    }
    const double ux = dx / len;
    const double uy = dy / len;
    const double nx = -uy;  // normale (demi-largeur de la bande retirée)
    const double ny = ux;

    // Étend très au-delà de la région (diagonale de sa boîte englobante + 1 mm
    // de marge) pour garantir une traversée complète quels que soient les
    // points A/B fournis par l'utilisateur -- l'utilisateur vise la jonction,
    // pas les bords exacts de la forme.
    double minX = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double minY = std::numeric_limits<double>::max();
    double maxY = std::numeric_limits<double>::lowest();
    const auto scan = [&](const Path& p) {
        for (const auto& node : p.nodes) {
            minX = std::min(minX, static_cast<double>(node.pos.x.value));
            maxX = std::max(maxX, static_cast<double>(node.pos.x.value));
            minY = std::min(minY, static_cast<double>(node.pos.y.value));
            maxY = std::max(maxY, static_cast<double>(node.pos.y.value));
        }
    };
    scan(region.outer);
    for (const auto& hole : region.holes) {
        scan(hole);
    }
    if (minX > maxX) {
        return std::vector<PathSet>{region};  // région vide
    }
    const double reach = std::hypot(maxX - minX, maxY - minY) + 1000.0;

    const double midx = (ax + bx) / 2.0;
    const double midy = (ay + by) / 2.0;
    const double e0x = midx - ux * reach;
    const double e0y = midy - uy * reach;
    const double e1x = midx + ux * reach;
    const double e1y = midy + uy * reach;
    const double hw = std::max(1.0, static_cast<double>(cut_width.value) / 2.0);

    const auto pt = [](double x, double y) {
        return Clipper2Lib::Point64{static_cast<std::int64_t>(std::llround(x)),
                                    static_cast<std::int64_t>(std::llround(y))};
    };
    Clipper2Lib::Path64 cutBand{
        pt(e0x + nx * hw, e0y + ny * hw),
        pt(e1x + nx * hw, e1y + ny * hw),
        pt(e1x - nx * hw, e1y - ny * hw),
        pt(e0x - nx * hw, e0y - ny * hw),
    };

    Clipper2Lib::Paths64 subject;
    subject.push_back(to_clipper(region.outer));
    for (const auto& hole : region.holes) {
        subject.push_back(to_clipper(hole));
    }
    const Clipper2Lib::Paths64 clip{cutBand};

    Clipper2Lib::Clipper64 clipper;
    clipper.AddSubject(subject);
    clipper.AddClip(clip);
    Clipper2Lib::PolyTree64 tree;
    if (!clipper.Execute(Clipper2Lib::ClipType::Difference, Clipper2Lib::FillRule::EvenOdd, tree)) {
        return fail(ErrorCategory::Internal, "Échec de la découpe géométrique",
                    "Clipper64::Execute a renvoyé false");
    }
    std::vector<PathSet> out;
    for (const auto& top : tree) {
        collect(*top, out);
    }
    return out;
}

}  // namespace openstitch::geometry
