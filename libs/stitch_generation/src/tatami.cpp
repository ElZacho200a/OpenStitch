// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/tatami.hpp"

#include <algorithm>
#include <cmath>

namespace openstitch::stitch_generation {

namespace {

struct PointD {
    double x{0.0};
    double y{0.0};
};

// Rotation d'un point du repère modèle vers le repère « rangées horizontales »
// (rotation de -angle) et retour (+angle).
PointD rotate(PointD p, double cosA, double sinA) {
    return {p.x * cosA - p.y * sinA, p.x * sinA + p.y * cosA};
}

// Intersections d'une horizontale y=scanY avec un polygone fermé (liste
// d'arêtes), ajoutées à `xs`. Convention demi-ouverte sur y pour ne pas
// compter deux fois un sommet.
void scan_polygon(const std::vector<PointD>& poly, double scanY, std::vector<double>& xs) {
    const std::size_t n = poly.size();
    for (std::size_t i = 0; i < n; ++i) {
        const PointD a = poly[i];
        const PointD b = poly[(i + 1) % n];
        const double y0 = a.y;
        const double y1 = b.y;
        if ((y0 <= scanY && y1 > scanY) || (y1 <= scanY && y0 > scanY)) {
            const double t = (scanY - y0) / (y1 - y0);
            xs.push_back(a.x + t * (b.x - a.x));
        }
    }
}

Vec2um to_um(PointD p) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(p.x))},
                  Micrometers{static_cast<std::int32_t>(std::lround(p.y))}};
}

// Point dans polygone (lancer de rayon horizontal, règle pair-impair).
bool point_in_poly(const std::vector<PointD>& poly, PointD p) {
    bool inside = false;
    const std::size_t n = poly.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const PointD a = poly[i];
        const PointD b = poly[j];
        if (((a.y > p.y) != (b.y > p.y)) &&
            (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)) {
            inside = !inside;
        }
    }
    return inside;
}

// Dans la région = dans l'extérieur (polys[0]) ET hors de tous les trous.
bool in_region(const std::vector<std::vector<PointD>>& polys, PointD p) {
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

// Le segment [a,b] reste-t-il intégralement dans la région ? (échantillonnage)
bool connector_inside(const std::vector<std::vector<PointD>>& polys, PointD a, PointD b) {
    constexpr int samples = 10;
    for (int k = 1; k < samples; ++k) {
        const double t = static_cast<double>(k) / samples;
        const PointD p{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
        if (!in_region(polys, p)) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::vector<FillStitch> fill_tatami(const geometry::PathSet& region,
                                    const document::TatamiParams& params) {
    std::vector<FillStitch> out;
    const double spacing = static_cast<double>(std::max<std::int32_t>(1, params.row_spacing.value));
    const double stitchLen =
        static_cast<double>(std::max<std::int32_t>(1, params.stitch_length.value));
    const int stagger = std::max(1, params.stagger);

    const double cosA = std::cos(-params.angle.radians);
    const double sinA = std::sin(-params.angle.radians);
    const double cosB = std::cos(params.angle.radians);
    const double sinB = std::sin(params.angle.radians);

    // Polygones (extérieur + trous) exprimés dans le repère des rangées.
    std::vector<std::vector<PointD>> polys;
    const auto addPoly = [&](const geometry::Path& path) {
        std::vector<PointD> poly;
        poly.reserve(path.nodes.size());
        for (const auto& node : path.nodes) {
            poly.push_back(rotate({static_cast<double>(node.pos.x.value),
                                   static_cast<double>(node.pos.y.value)},
                                  cosA, sinA));
        }
        if (poly.size() >= 3) {
            polys.push_back(std::move(poly));
        }
    };
    addPoly(region.outer);
    for (const auto& hole : region.holes) {
        addPoly(hole);
    }
    if (polys.empty()) {
        return out;
    }

    double minY = polys[0][0].y;
    double maxY = minY;
    for (const auto& poly : polys) {
        for (const PointD& p : poly) {
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }
    }

    // Génère les rangées ; chaque rangée est une liste de segments [x0,x1].
    // `prev` (repère rangées) suit la dernière pénétration émise : la liaison
    // vers le début du segment suivant est classée couture ou déplacement.
    bool hasPrev = false;
    PointD prev{};
    int rowIndex = 0;
    bool reverse = false;
    for (double y = minY + spacing / 2.0; y < maxY; y += spacing, ++rowIndex) {
        std::vector<double> xs;
        for (const auto& poly : polys) {
            scan_polygon(poly, y, xs);
        }
        std::sort(xs.begin(), xs.end());

        // Décalage de phase des pénétrations : les rangées ne s'alignent pas.
        const double phase = stitchLen * (static_cast<double>(rowIndex % stagger) /
                                          static_cast<double>(stagger));

        // Paires d'intersections = segments intérieurs (règle pair-impair).
        std::vector<std::pair<double, double>> spans;
        for (std::size_t i = 0; i + 1 < xs.size(); i += 2) {
            spans.emplace_back(xs[i], xs[i + 1]);
        }
        if (reverse) {
            std::reverse(spans.begin(), spans.end());
        }

        for (const auto& [xa, xb] : spans) {
            const double lo = std::min(xa, xb);
            const double hi = std::max(xa, xb);

            // Pénétrations d'aiguille : les deux bords + les points d'une grille
            // fixe de pas `stitchLen` ancrée sur `phase`. La grille absolue fait
            // que deux rangées de phases différentes ne s'alignent pas.
            std::vector<double> penetrations;
            penetrations.push_back(lo);
            const double firstK = std::ceil((lo - phase) / stitchLen);
            for (double x = phase + firstK * stitchLen; x < hi; x += stitchLen) {
                if (x > lo) {
                    penetrations.push_back(x);
                }
            }
            penetrations.push_back(hi);
            if (reverse) {
                std::reverse(penetrations.begin(), penetrations.end());
            }

            for (std::size_t p = 0; p < penetrations.size(); ++p) {
                const PointD rp{penetrations[p], y};
                bool travel = false;
                if (p == 0) {
                    // Début de segment : approche par déplacement si la liaison
                    // depuis la dernière pénétration sort de la région/trou
                    // (ou tout premier point du remplissage).
                    travel = !hasPrev || !connector_inside(polys, prev, rp);
                }
                out.push_back({to_um(rotate(rp, cosB, sinB)), travel});
                prev = rp;
                hasPrev = true;
            }
        }
        reverse = !reverse;
    }
    return out;
}

}  // namespace openstitch::stitch_generation
