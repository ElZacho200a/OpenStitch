// SPDX-License-Identifier: Apache-2.0
// En-tête interne à libs/auto_satin : primitives géométriques (point/polygone
// en µm double, requêtes région/contour) partagées entre satin_column.cpp et
// les futurs medial_field.cpp/corridor.cpp (HP-STI-018) — extraites de
// l'espace de noms anonyme de satin_column.cpp où elles vivaient seules
// jusqu'ici (§ Phase A0, specs/plans/hp-sti-018-turning-satin.md).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include "openstitch/geometry/path.hpp"

namespace openstitch::auto_satin::detail {

// Travail en µm double, repère modèle (Y vers le haut).
struct P2 {
    double x{0.0};
    double y{0.0};
};

inline P2 operator+(P2 a, P2 b) {
    return {a.x + b.x, a.y + b.y};
}
inline P2 operator-(P2 a, P2 b) {
    return {a.x - b.x, a.y - b.y};
}
inline P2 operator*(P2 a, double s) {
    return {a.x * s, a.y * s};
}
inline double dot(P2 a, P2 b) {
    return a.x * b.x + a.y * b.y;
}
inline double norm(P2 a) {
    return std::sqrt(dot(a, a));
}

using Poly = std::vector<P2>;

inline std::vector<Poly> region_polys(const geometry::PathSet& region) {
    std::vector<Poly> polys;
    const auto add = [&](const geometry::Path& path) {
        Poly poly;
        poly.reserve(path.nodes.size());
        for (const auto& n : path.nodes) {
            poly.push_back(
                {static_cast<double>(n.pos.x.value), static_cast<double>(n.pos.y.value)});
        }
        if (poly.size() >= 3) {
            polys.push_back(std::move(poly));
        }
    };
    add(region.outer);
    for (const auto& h : region.holes) {
        add(h);
    }
    return polys;
}

inline bool point_in_poly(const Poly& poly, P2 p) {
    bool inside = false;
    const std::size_t n = poly.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const P2 a = poly[i];
        const P2 b = poly[j];
        if (((a.y > p.y) != (b.y > p.y)) && (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)) {
            inside = !inside;
        }
    }
    return inside;
}

inline bool in_region(const std::vector<Poly>& polys, P2 p) {
    if (polys.empty() || !point_in_poly(polys[0], p)) {
        return false;
    }
    for (std::size_t i = 1; i < polys.size(); ++i) {
        if (point_in_poly(polys[i], p)) {
            return false;
        }
    }
    return true;
}

// Distance de `p` au bord le plus proche (tous polygones confondus) — utilisé
// pour tolérer un point de rail exactement SUR le contour (§ validation
// paramétrique, étape 12) : `in_region` seul est ambigu pile sur une arête.
inline double distance_to_polys(const std::vector<Poly>& polys, P2 p) {
    double best = std::numeric_limits<double>::max();
    for (const auto& poly : polys) {
        const std::size_t n = poly.size();
        for (std::size_t i = 0; i < n; ++i) {
            const P2 a = poly[i];
            const P2 b = poly[(i + 1) % n];
            const P2 ab = b - a;
            const double len2 = dot(ab, ab);
            const double t = len2 > 1e-12 ? std::clamp(dot(p - a, ab) / len2, 0.0, 1.0) : 0.0;
            const P2 proj = a + ab * t;
            best = std::min(best, norm(p - proj));
        }
    }
    return best;
}

// --- Représentation paramétrique du contour extérieur -----------------------
//
// Utilisée UNIQUEMENT en diagnostic (calcul de `JunctionCore`, rendu SVG) :
// jamais pour construire ou déplacer un rail. Un polygone fermé + longueur
// curviligne cumulée à chaque sommet, permettant de projeter un point
// quelconque sur le contour et d'en extraire un arc entre deux abscisses.
// `make_contour_polyline`/`locate_arc`/`extract_contour_arc` (qui construisent
// et consomment ce type plus finement) restent dans satin_column.cpp — seul
// le type et la projection la plus proche sont partagés ici.
struct ContourPolyline {
    Poly points;                    // sommets du polygone, dans leur ordre d'origine
    std::vector<double> cumulative; // longueur cumulée jusqu'au sommet i (cumulative[0] = 0)
    double total_length{0.0};
};

struct ContourProjection {
    std::size_t segment_index{0}; // arête [i, i+1) du polygone
    double segment_t{0.0};        // 0..1 le long de cette arête
    double arc_length{0.0};       // abscisse curviligne du point projeté
    P2 point{};
    double distance{0.0}; // distance point -> contour
};

// Projette `p` sur le contour : plus proche point parmi tous les segments.
inline ContourProjection project_to_contour(const ContourPolyline& contour, P2 p) {
    ContourProjection best;
    best.distance = std::numeric_limits<double>::max();
    const std::size_t n = contour.points.size();
    for (std::size_t i = 0; i < n; ++i) {
        const P2 a = contour.points[i];
        const P2 b = contour.points[(i + 1) % n];
        const P2 ab = b - a;
        const double len2 = dot(ab, ab);
        const double t = len2 > 1e-12 ? std::clamp(dot(p - a, ab) / len2, 0.0, 1.0) : 0.0;
        const P2 proj = a + ab * t;
        const double d = norm(p - proj);
        if (d < best.distance) {
            best.distance = d;
            best.segment_index = i;
            best.segment_t = t;
            best.point = proj;
            const double segLen = norm(ab);
            best.arc_length = contour.cumulative[i] + t * segLen;
        }
    }
    return best;
}

} // namespace openstitch::auto_satin::detail
