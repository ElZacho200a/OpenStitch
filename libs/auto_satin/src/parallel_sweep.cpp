// SPDX-License-Identifier: Apache-2.0
#include "openstitch/auto_satin/parallel_sweep.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>
#include <vector>

#include "chord.hpp"
#include "coverage.hpp"
#include "geometry_detail.hpp"
#include "openstitch/geometry/moments.hpp"
#include "openstitch/geometry/polyline.hpp"

namespace openstitch::auto_satin {

namespace {

using detail::ChordInterval;
using detail::P2;

constexpr double kMinOverlapUm = 1.0; // recouvrement minimal pour relier deux intervalles

Vec2um to_um(P2 p) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(p.x))},
                  Micrometers{static_cast<std::int32_t>(std::lround(p.y))}};
}

double overlap(const ChordInterval& a, const ChordInterval& b) {
    return std::min(a.t_hi, b.t_hi) - std::max(a.t_lo, b.t_lo);
}

// Angle des traversées : explicite, ou perpendiculaire à l'axe principal d'inertie quand la
// forme est assez allongée (les fils traversent sa largeur), horizontal sinon.
double sweep_angle(const geometry::PathSet& region, const SkeletonSatinParameters& params) {
    if (!params.sweep_auto_angle) {
        return params.sweep_angle_rad;
    }
    if (const auto pa = geometry::principal_axis({region}); pa && pa->anisotropy > 1.3) {
        return pa->angle.radians + std::numbers::pi / 2.0;
    }
    return 0.0;
}

struct Band {
    std::vector<SkeletonSatinCrossing> crossings;
    std::vector<Vec2um> axis;
};

} // namespace

Result<SkeletonSatinResult> generate_parallel_sweep(const geometry::PathSet& curved,
                                                    const SkeletonSatinParameters& params) {
    // Les courbes sont aplaties (50 µm, comme la génération de points) : la région est ensuite
    // un polygone, quelle que soit l'origine de l'appelant (aperçu, API, export).
    const geometry::PathSet region = geometry::flatten_to_polygon(curved, Micrometers{50});
    SkeletonSatinResult result;
    auto& diag = result.diagnostics;
    diag.orphan_guides = static_cast<int>(params.guides.size());
    if (!params.guides.empty()) {
        diag.messages.push_back(
            "balayage parallèle : les guides locaux ne sont pas appliqués (angle global)");
    }

    const std::vector<detail::Poly> polys = detail::region_polys(region);
    if (polys.empty()) {
        diag.messages.push_back("région vide : aucune traversée");
        return result;
    }
    const double spacing = std::max(50.0, static_cast<double>(params.spacing.value));
    const double minThread = static_cast<double>(params.min_thread_length.value);

    const double angle = sweep_angle(region, params);
    const P2 u{std::cos(angle), std::sin(angle)}; // direction des traversées
    const P2 n{-u.y, u.x};                        // direction du balayage

    double smin = 0.0;
    double smax = 0.0;
    bool first = true;
    for (const auto& p : polys.front()) { // l'extérieur borne la région
        const double s = detail::dot(p, n);
        smin = first ? s : std::min(smin, s);
        smax = first ? s : std::max(smax, s);
        first = false;
    }
    const double range = smax - smin;
    if (!(range > 0.0)) {
        diag.messages.push_back("région dégénérée : aucune traversée");
        return result;
    }
    const int lines = std::max(1, static_cast<int>(std::ceil(range / spacing)));
    const double step = range / lines; // espacement réel, un peu inférieur à `spacing`

    std::vector<Band> bands;
    std::vector<ChordInterval> prev;
    std::vector<int> prevBand;
    for (int k = 0; k < lines; ++k) {
        const double s = smin + (k + 0.5) * step;
        const P2 base = n * s;
        std::vector<ChordInterval> cur;
        for (const auto& iv : detail::line_intervals(polys, base, u)) {
            if (iv.length() < minThread) {
                ++diag.too_short;
                continue;
            }
            cur.push_back(iv);
        }
        // Appariement un à un avec la ligne précédente ; toute division ou fusion ferme les
        // bandes concernées et ouvre de nouvelles bandes (pas de couture à travers le vide).
        std::vector<int> prevLinks(prev.size(), 0);
        std::vector<int> curLinks(cur.size(), 0);
        std::vector<int> curMatch(cur.size(), -1);
        for (std::size_t j = 0; j < cur.size(); ++j) {
            for (std::size_t i = 0; i < prev.size(); ++i) {
                if (overlap(prev[i], cur[j]) > kMinOverlapUm) {
                    ++prevLinks[i];
                    ++curLinks[j];
                    curMatch[j] = static_cast<int>(i);
                }
            }
        }
        std::vector<int> curBand(cur.size(), -1);
        for (std::size_t j = 0; j < cur.size(); ++j) {
            const int i = curMatch[j];
            if (i >= 0 && curLinks[j] == 1 && prevLinks[static_cast<std::size_t>(i)] == 1) {
                curBand[j] = prevBand[static_cast<std::size_t>(i)];
            } else {
                curBand[j] = static_cast<int>(bands.size());
                bands.emplace_back();
            }
            Band& band = bands[static_cast<std::size_t>(curBand[j])];
            const P2 a = base + u * cur[j].t_lo;
            const P2 b = base + u * cur[j].t_hi;
            band.crossings.push_back({to_um(a), to_um(b)});
            band.axis.push_back(to_um((a + b) * 0.5));
        }
        prev = std::move(cur);
        prevBand = std::move(curBand);
    }

    int singles = 0;
    for (auto& band : bands) {
        if (band.crossings.size() < 2) {
            ++singles; // un seul fil : pas de zigzag possible
            continue;
        }
        SkeletonSatinColumn column;
        column.crossings = std::move(band.crossings);
        result.columns.push_back(std::move(column));
        result.axes.push_back(std::move(band.axis));
    }
    diag.pieces = static_cast<int>(result.columns.size());
    if (singles > 0) {
        diag.messages.push_back("balayage parallèle : " + std::to_string(singles) +
                                " bande(s) d'un seul fil ignorée(s)");
    }
    if (result.columns.empty()) {
        diag.messages.push_back("balayage parallèle : aucune bande continue");
    }
    if (params.measure_coverage) {
        detail::measure_coverage(region, result);
    }
    return result;
}

} // namespace openstitch::auto_satin
