// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/routing.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace openstitch::stitch_generation {

namespace {

// Extrémité d'entrée d'une colonne selon son orientation.
Vec2um entry_of(const RouteColumn& c, bool reversed) {
    return reversed ? c.end : c.start;
}
// Extrémité de sortie d'une colonne selon son orientation.
Vec2um exit_of(const RouteColumn& c, bool reversed) {
    return reversed ? c.start : c.end;
}

// Orientation optimale d'un ordre donné (programmation dynamique sur les deux
// extrémités possibles de chaque colonne). Renvoie le déplacement total et
// remplit `reversed` (parallèle à `order`).
double best_orientation(const std::vector<RouteColumn>& cols, const std::vector<std::size_t>& order,
                        Vec2um origin, std::vector<char>& reversed) {
    const std::size_t n = order.size();
    reversed.assign(n, 0);
    if (n == 0) {
        return 0.0;
    }
    constexpr double kInf = std::numeric_limits<double>::infinity();
    // dp[r] = coût minimal pour arriver à la colonne courante orientée `r`.
    std::array<double, 2> dp{};
    // back[i][r] = orientation choisie pour la colonne i-1 menant à (i, r).
    std::vector<std::array<char, 2>> back(n, std::array<char, 2>{0, 0});

    for (int r = 0; r < 2; ++r) {
        dp[r] = length_um(entry_of(cols[order[0]], r != 0) - origin);
    }
    for (std::size_t i = 1; i < n; ++i) {
        std::array<double, 2> next{kInf, kInf};
        for (int r = 0; r < 2; ++r) {
            const Vec2um in = entry_of(cols[order[i]], r != 0);
            for (int pr = 0; pr < 2; ++pr) {
                const Vec2um out = exit_of(cols[order[i - 1]], pr != 0);
                const double cost = dp[pr] + length_um(in - out);
                if (cost < next[r]) {
                    next[r] = cost;
                    back[i][r] = static_cast<char>(pr);
                }
            }
        }
        dp = next;
    }
    const int last = dp[1] < dp[0] ? 1 : 0;
    // Remontée pour reconstituer les orientations.
    int r = last;
    for (std::size_t i = n; i-- > 0;) {
        reversed[i] = static_cast<char>(r);
        if (i > 0) {
            r = back[i][r];
        }
    }
    return dp[last];
}

// Ordre glouton plus proche voisin : à chaque étape, l'extrémité libre la plus
// proche de la position courante.
std::vector<std::size_t> greedy_order(const std::vector<RouteColumn>& cols, Vec2um origin) {
    const std::size_t n = cols.size();
    std::vector<std::size_t> order;
    order.reserve(n);
    std::vector<char> used(n, 0);
    Vec2um cur = origin;
    for (std::size_t k = 0; k < n; ++k) {
        std::size_t best = n;
        double bestDist = std::numeric_limits<double>::infinity();
        bool bestRev = false;
        for (std::size_t i = 0; i < n; ++i) {
            if (used[i]) {
                continue;
            }
            const double ds = length_um(cols[i].start - cur);
            const double de = length_um(cols[i].end - cur);
            if (ds < bestDist) {
                bestDist = ds;
                best = i;
                bestRev = false;
            }
            if (de < bestDist) {
                bestDist = de;
                best = i;
                bestRev = true;
            }
        }
        used[best] = 1;
        order.push_back(best);
        cur = exit_of(cols[best], bestRev);
    }
    return order;
}

}  // namespace

RoutePlan route_columns(const std::vector<RouteColumn>& columns, Vec2um origin,
                        const RoutingConfig& config) {
    RoutePlan plan;
    const std::size_t n = columns.size();
    if (n == 0) {
        return plan;
    }

    std::vector<std::size_t> order = greedy_order(columns, origin);
    std::vector<char> reversed;
    double cost = best_orientation(columns, order, origin, reversed);

    // 2-opt : inversion de sous-segments tant que le coût diminue (borné).
    if (config.two_opt && n >= 3) {
        bool improved = true;
        int guard = 0;
        while (improved && guard++ < 64) {
            improved = false;
            for (std::size_t i = 0; i + 1 < n; ++i) {
                for (std::size_t j = i + 1; j < n; ++j) {
                    std::vector<std::size_t> cand = order;
                    std::reverse(cand.begin() + static_cast<std::ptrdiff_t>(i),
                                 cand.begin() + static_cast<std::ptrdiff_t>(j) + 1);
                    std::vector<char> rev2;
                    const double c2 = best_orientation(columns, cand, origin, rev2);
                    if (c2 + 1.0 < cost) {  // marge anti-oscillation (1 µm)
                        order = std::move(cand);
                        reversed = std::move(rev2);
                        cost = c2;
                        improved = true;
                    }
                }
            }
        }
    }

    plan.steps.reserve(n);
    plan.travel_um = cost;
    Vec2um cur = origin;
    for (std::size_t i = 0; i < n; ++i) {
        const RouteColumn& col = columns[order[i]];
        const bool rev = reversed[i] != 0;
        RouteStep step;
        step.column_index = order[i];
        step.reversed = rev;
        const double gap = length_um(entry_of(col, rev) - cur);
        if (i == 0) {
            step.connector = ConnectorKind::Start;
        } else if (gap <= static_cast<double>(config.underpath_max.value)) {
            step.connector = ConnectorKind::Underpath;
            ++plan.underpaths;
        } else {
            step.connector = ConnectorKind::Jump;
            ++plan.jumps;
        }
        plan.steps.push_back(step);
        cur = exit_of(col, rev);
    }
    return plan;
}

}  // namespace openstitch::stitch_generation
