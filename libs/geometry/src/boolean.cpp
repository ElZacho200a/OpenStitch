// SPDX-License-Identifier: Apache-2.0
#include "openstitch/geometry/boolean.hpp"

#include <clipper2/clipper.h>

#include <algorithm>
#include <cmath>

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

// Même parcours d'arbre que clean.cpp/cut.cpp (chaque .cpp de geometry reste
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

// Réoriente `path` pour que son aire signée ait le signe de `wantPositive`
// (vrai -> CCW, faux -> CW). Nécessaire pour la règle NonZero : contrairement
// à EvenOdd (indifférent à l'orientation), NonZero encode un trou par une
// orientation OPPOSÉE à son contour extérieur -- une orientation d'entrée non
// garantie (le reste de geometry/ utilise EvenOdd précisément pour s'en
// affranchir) romprait ce contrat silencieusement.
Path oriented_as(const Path& path, bool wantPositive) {
    if ((signed_area_um2(path) >= 0.0) == wantPositive) {
        return path;
    }
    Path reversed = path;
    std::reverse(reversed.nodes.begin(), reversed.nodes.end());
    return reversed;
}

}  // namespace

double path_set_area_um2(const PathSet& set) {
    double area = std::abs(signed_area_um2(set.outer));
    for (const auto& hole : set.holes) {
        area -= std::abs(signed_area_um2(hole));
    }
    return std::max(0.0, area);
}

namespace {

// Ajoute `set` (extérieur + trous) à `paths`, orienté pour la règle NonZero :
// extérieur CCW, trous CW -- même convention que `oriented_as` ci-dessus,
// appliquée ici aux DEUX côtés d'une opération (contrairement à
// `subtract_polygons`, dont les `cutouts` sont de simples contours sans trou).
void append_oriented(const PathSet& set, Clipper2Lib::Paths64& paths) {
    if (set.outer.nodes.size() >= 3) {
        paths.push_back(to_clipper(oriented_as(set.outer, true)));
    }
    for (const auto& hole : set.holes) {
        if (hole.nodes.size() >= 3) {
            paths.push_back(to_clipper(oriented_as(hole, false)));
        }
    }
}

Result<std::vector<PathSet>> boolean_op(const std::vector<PathSet>& a, const std::vector<PathSet>& b,
                                         Clipper2Lib::ClipType op) {
    Clipper2Lib::Paths64 subject;
    for (const auto& set : a) {
        append_oriented(set, subject);
    }
    Clipper2Lib::Paths64 clip;
    for (const auto& set : b) {
        append_oriented(set, clip);
    }
    if (subject.empty()) {
        return std::vector<PathSet>{};
    }
    if (clip.empty()) {
        return op == Clipper2Lib::ClipType::Intersection ? std::vector<PathSet>{} : a;
    }

    Clipper2Lib::Clipper64 clipper;
    clipper.AddSubject(subject);
    clipper.AddClip(clip);
    Clipper2Lib::PolyTree64 tree;
    if (!clipper.Execute(op, Clipper2Lib::FillRule::NonZero, tree)) {
        return fail(ErrorCategory::Internal, "Échec de l'opération géométrique",
                    "Clipper64::Execute a renvoyé false");
    }
    std::vector<PathSet> out;
    for (const auto& top : tree) {
        collect(*top, out);
    }
    return out;
}

}  // namespace

Result<std::vector<PathSet>> intersect_polygons(const std::vector<PathSet>& a, const std::vector<PathSet>& b) {
    return boolean_op(a, b, Clipper2Lib::ClipType::Intersection);
}

Result<std::vector<PathSet>> difference_polygons(const std::vector<PathSet>& a, const std::vector<PathSet>& b) {
    return boolean_op(a, b, Clipper2Lib::ClipType::Difference);
}

Result<std::vector<PathSet>> subtract_polygons(const PathSet& base, const std::vector<Path>& cutouts) {
    if (cutouts.empty()) {
        return std::vector<PathSet>{base};
    }

    Clipper2Lib::Paths64 subject;
    subject.push_back(to_clipper(oriented_as(base.outer, true)));
    for (const auto& hole : base.holes) {
        subject.push_back(to_clipper(oriented_as(hole, false)));
    }

    Clipper2Lib::Paths64 clip;
    clip.reserve(cutouts.size());
    for (const auto& cutout : cutouts) {
        if (cutout.nodes.size() < 3) {
            continue;
        }
        // Toutes CCW (règle NonZero) : deux contours qui se chevauchent mais
        // d'orientation opposée s'annuleraient localement sous NonZero,
        // exactement le défaut qu'on cherche à éviter en préférant NonZero à
        // EvenOdd pour CE côté (des bandes de colonnes satin voisines qui se
        // recouvrent légèrement, ex. au niveau d'un barreau partagé).
        clip.push_back(to_clipper(oriented_as(cutout, true)));
    }
    if (clip.empty()) {
        return std::vector<PathSet>{base};
    }

    Clipper2Lib::Clipper64 clipper;
    clipper.AddSubject(subject);
    clipper.AddClip(clip);
    Clipper2Lib::PolyTree64 tree;
    if (!clipper.Execute(Clipper2Lib::ClipType::Difference, Clipper2Lib::FillRule::NonZero, tree)) {
        return fail(ErrorCategory::Internal, "Échec de la soustraction géométrique",
                    "Clipper64::Execute a renvoyé false");
    }
    std::vector<PathSet> out;
    for (const auto& top : tree) {
        collect(*top, out);
    }
    return out;
}

}  // namespace openstitch::geometry
