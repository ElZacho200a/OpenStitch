// SPDX-License-Identifier: Apache-2.0
#include "chord.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace openstitch::auto_satin::detail {

namespace {

// Produit vectoriel u × v.
double cross2(P2 u, P2 v) {
    return u.x * v.y - u.y * v.x;
}

} // namespace

std::vector<ChordInterval> line_intervals(const std::vector<Poly>& polys, P2 p, P2 u) {
    std::vector<double> ts;
    for (const auto& poly : polys) {
        const std::size_t n = poly.size();
        for (std::size_t i = 0; i < n; ++i) {
            const P2 a = poly[i];
            const P2 b = poly[(i + 1) % n];
            // Côté signé de chaque extrémité par rapport à la droite. Règle
            // demi-ouverte : « > 0 » contre « <= 0 ». Le même sommet, évalué par
            // ses deux arêtes, donne le même signe : le nombre de croisements est
            // toujours pair.
            const double sa = cross2(u, a - p);
            const double sb = cross2(u, b - p);
            if ((sa > 0.0) == (sb > 0.0)) {
                continue;
            }
            const double lambda = sa / (sa - sb);
            const P2 q = a + (b - a) * lambda;
            ts.push_back(dot(u, q - p));
        }
    }
    std::sort(ts.begin(), ts.end());
    std::vector<ChordInterval> out;
    out.reserve(ts.size() / 2);
    for (std::size_t i = 0; i + 1 < ts.size(); i += 2) {
        if (ts[i + 1] - ts[i] > 1e-9) {
            out.push_back({ts[i], ts[i + 1]});
        }
    }
    return out;
}

std::optional<ChordInterval> chord_through(const std::vector<Poly>& polys, P2 p, P2 u,
                                           double tolerance_um) {
    const auto intervals = line_intervals(polys, p, u);
    constexpr double kEps = 1e-9;
    for (const auto& iv : intervals) {
        if (iv.t_lo <= kEps && iv.t_hi >= -kEps) {
            return iv;
        }
    }
    if (tolerance_um <= 0.0) {
        return std::nullopt;
    }
    std::optional<ChordInterval> best;
    double bestDist = std::numeric_limits<double>::max();
    for (const auto& iv : intervals) {
        // Distance de t = 0 à l'intervalle (qui ne le contient pas).
        const double d = iv.t_lo > 0.0 ? iv.t_lo : -iv.t_hi;
        if (d < bestDist) {
            bestDist = d;
            best = iv;
        }
    }
    if (best && bestDist <= tolerance_um) {
        return best;
    }
    return std::nullopt;
}

} // namespace openstitch::auto_satin::detail
