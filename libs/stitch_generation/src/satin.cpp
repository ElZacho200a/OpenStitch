// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/satin.hpp"

#include <algorithm>
#include <cmath>

namespace openstitch::stitch_generation {

namespace {

struct PointD {
    double x{0.0};
    double y{0.0};
};

PointD toD(Vec2um p) {
    return {static_cast<double>(p.x.value), static_cast<double>(p.y.value)};
}

Vec2um toUm(PointD p) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(p.x))},
                  Micrometers{static_cast<std::int32_t>(std::lround(p.y))}};
}

double dist(PointD a, PointD b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

// Longueurs cumulées d'une polyligne.
std::vector<double> cumulative(const std::vector<PointD>& pts) {
    std::vector<double> cum(pts.size(), 0.0);
    for (std::size_t i = 1; i < pts.size(); ++i) {
        cum[i] = cum[i - 1] + dist(pts[i - 1], pts[i]);
    }
    return cum;
}

// Point à l'abscisse curviligne `s` le long de la polyligne.
PointD point_at(const std::vector<PointD>& pts, const std::vector<double>& cum, double s) {
    if (pts.empty()) {
        return {};
    }
    if (s <= 0.0) {
        return pts.front();
    }
    if (s >= cum.back()) {
        return pts.back();
    }
    const auto it = std::upper_bound(cum.begin(), cum.end(), s);
    const std::size_t i = static_cast<std::size_t>(it - cum.begin());
    const double segLen = cum[i] - cum[i - 1];
    const double t = segLen > 0.0 ? (s - cum[i - 1]) / segLen : 0.0;
    return {pts[i - 1].x + t * (pts[i].x - pts[i - 1].x),
            pts[i - 1].y + t * (pts[i].y - pts[i - 1].y)};
}

std::vector<PointD> to_points(const geometry::Path& path) {
    std::vector<PointD> pts;
    pts.reserve(path.nodes.size());
    for (const auto& node : path.nodes) {
        pts.push_back(toD(node.pos));
    }
    return pts;
}

// Applique la compensation de tirage : éloigne a de b (et inversement) de
// `comp` micromètres le long de leur médiatrice.
std::pair<PointD, PointD> compensate(PointD a, PointD b, double comp) {
    if (comp == 0.0) {
        return {a, b};
    }
    const double d = dist(a, b);
    if (d < 1e-6) {
        return {a, b};
    }
    const double ux = (a.x - b.x) / d;
    const double uy = (a.y - b.y) / d;
    return {{a.x + ux * comp, a.y + uy * comp}, {b.x - ux * comp, b.y - uy * comp}};
}

}  // namespace

SatinResult fill_satin(const geometry::Path& rail_a, const geometry::Path& rail_b,
                       const SatinConfig& config) {
    SatinResult result;
    const auto a = to_points(rail_a);
    const auto b = to_points(rail_b);
    if (a.size() < 2 || b.size() < 2) {
        return result;
    }
    const auto cumA = cumulative(a);
    const auto cumB = cumulative(b);
    const double lenA = cumA.back();
    const double lenB = cumB.back();
    const double columnLen = std::max(lenA, lenB);
    const double density = static_cast<double>(std::max<std::int32_t>(1, config.density.value));
    const double comp = static_cast<double>(config.pull_compensation.value);

    const int steps = std::max(1, static_cast<int>(std::ceil(columnLen / density)));

    // Sous-couche : point droit sur l'axe central (aller simple, pas grossier).
    if (config.center_underlay) {
        const double uspace =
            static_cast<double>(std::max<std::int32_t>(1, config.underlay_spacing.value));
        const int usteps = std::max(1, static_cast<int>(std::ceil(columnLen / uspace)));
        for (int i = 0; i <= usteps; ++i) {
            const double f = static_cast<double>(i) / static_cast<double>(usteps);
            const PointD pa = point_at(a, cumA, f * lenA);
            const PointD pb = point_at(b, cumB, f * lenB);
            result.underlay.push_back(toUm({(pa.x + pb.x) / 2.0, (pa.y + pb.y) / 2.0}));
        }
    }

    // Satin : zigzag A0, B0, A1, B1, … en avançant par fraction d'abscisse.
    for (int i = 0; i <= steps; ++i) {
        const double f = static_cast<double>(i) / static_cast<double>(steps);
        PointD pa = point_at(a, cumA, f * lenA);
        PointD pb = point_at(b, cumB, f * lenB);
        result.max_width_um = std::max(result.max_width_um, dist(pa, pb));
        std::tie(pa, pb) = compensate(pa, pb, comp);
        // Alternance du bord de départ pour former le zigzag.
        if (i % 2 == 0) {
            result.satin.push_back(toUm(pa));
            result.satin.push_back(toUm(pb));
        } else {
            result.satin.push_back(toUm(pb));
            result.satin.push_back(toUm(pa));
        }
    }
    return result;
}

std::optional<std::pair<geometry::Path, geometry::Path>> rails_from_contour(
    const geometry::Path& contour) {
    const auto pts = to_points(contour);
    if (pts.size() < 4) {
        return std::nullopt;
    }

    // Deux sommets les plus éloignés = les « bouts » de la colonne.
    std::size_t iEnd0 = 0;
    std::size_t iEnd1 = 0;
    double best = -1.0;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        for (std::size_t j = i + 1; j < pts.size(); ++j) {
            const double d = dist(pts[i], pts[j]);
            if (d > best) {
                best = d;
                iEnd0 = i;
                iEnd1 = j;
            }
        }
    }
    if (best <= 0.0) {
        return std::nullopt;
    }

    // Les deux chaînes entre iEnd0 et iEnd1 forment les deux rails.
    geometry::Path railA;
    geometry::Path railB;
    railA.closed = false;
    railB.closed = false;
    for (std::size_t i = iEnd0; i != iEnd1; i = (i + 1) % pts.size()) {
        railA.nodes.push_back(contour.nodes[i]);
    }
    railA.nodes.push_back(contour.nodes[iEnd1]);
    for (std::size_t i = iEnd1; i != iEnd0; i = (i + 1) % pts.size()) {
        railB.nodes.push_back(contour.nodes[i]);
    }
    railB.nodes.push_back(contour.nodes[iEnd0]);
    // railB parcourt le contour dans l'autre sens : on l'inverse pour que les
    // deux rails aillent du même bout au même bout.
    std::reverse(railB.nodes.begin(), railB.nodes.end());

    if (railA.nodes.size() < 2 || railB.nodes.size() < 2) {
        return std::nullopt;
    }
    return std::pair{railA, railB};
}

}  // namespace openstitch::stitch_generation
