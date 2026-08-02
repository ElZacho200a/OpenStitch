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

const std::optional<std::uint32_t>& entry_junction_of(const RouteColumn& c, bool reversed) {
    return reversed ? c.end_junction : c.start_junction;
}

const std::optional<std::uint32_t>& exit_junction_of(const RouteColumn& c, bool reversed) {
    return reversed ? c.start_junction : c.end_junction;
}

std::optional<std::uint32_t> shared_junction(const RouteColumn& previous, bool previousReversed,
                                             const RouteColumn& next, bool nextReversed,
                                             Micrometers maxGap) {
    const auto& out = exit_junction_of(previous, previousReversed);
    const auto& in = entry_junction_of(next, nextReversed);
    if (!out || !in || *out != *in) {
        return std::nullopt;
    }
    if (length_um(entry_of(next, nextReversed) - exit_of(previous, previousReversed)) >
        static_cast<double>(maxGap.value)) {
        return std::nullopt;
    }
    return out;
}

struct OrientationCost {
    std::size_t junction_links{};
    double travel_um{std::numeric_limits<double>::infinity()};
};

bool better(const OrientationCost& candidate, const OrientationCost& reference,
            double travelMargin = 0.0) {
    if (candidate.junction_links != reference.junction_links) {
        return candidate.junction_links > reference.junction_links;
    }
    return candidate.travel_um + travelMargin < reference.travel_um;
}

// Orientation optimale d'un ordre donné (programmation dynamique sur les deux
// extrémités possibles de chaque colonne). Maximise les jonctions admissibles,
// minimise ensuite le déplacement et remplit `reversed` (parallèle à `order`).
OrientationCost best_orientation(const std::vector<RouteColumn>& cols,
                                 const std::vector<std::size_t>& order, Vec2um origin,
                                 Micrometers junctionMaxGap, std::vector<char>& reversed) {
    const std::size_t n = order.size();
    reversed.assign(n, 0);
    if (n == 0) {
        return {0, 0.0};
    }
    // dp[r] = coût minimal pour arriver à la colonne courante orientée `r`.
    std::array<OrientationCost, 2> dp{};
    // back[i][r] = orientation choisie pour la colonne i-1 menant à (i, r).
    std::vector<std::array<char, 2>> back(n, std::array<char, 2>{0, 0});

    for (int r = 0; r < 2; ++r) {
        dp[r] = {0, length_um(entry_of(cols[order[0]], r != 0) - origin)};
    }
    for (std::size_t i = 1; i < n; ++i) {
        std::array<OrientationCost, 2> next{};
        for (int r = 0; r < 2; ++r) {
            const Vec2um in = entry_of(cols[order[i]], r != 0);
            for (int pr = 0; pr < 2; ++pr) {
                const Vec2um out = exit_of(cols[order[i - 1]], pr != 0);
                OrientationCost candidate = dp[pr];
                candidate.travel_um += length_um(in - out);
                if (shared_junction(cols[order[i - 1]], pr != 0, cols[order[i]],
                                    r != 0, junctionMaxGap)) {
                    ++candidate.junction_links;
                }
                if (better(candidate, next[r])) {
                    next[r] = candidate;
                    back[i][r] = static_cast<char>(pr);
                }
            }
        }
        dp = next;
    }
    const int last = better(dp[1], dp[0]) ? 1 : 0;
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

// Ordre glouton : une vraie jonction admissible prime sur une proximité
// fortuite, puis l'extrémité libre la plus proche est retenue.
std::vector<std::size_t> greedy_order(const std::vector<RouteColumn>& cols, Vec2um origin,
                                      Micrometers junctionMaxGap) {
    const std::size_t n = cols.size();
    std::vector<std::size_t> order;
    order.reserve(n);
    std::vector<char> used(n, 0);
    Vec2um cur = origin;
    std::optional<std::uint32_t> curJunction;
    for (std::size_t k = 0; k < n; ++k) {
        std::size_t best = n;
        double bestDist = std::numeric_limits<double>::infinity();
        bool bestRev = false;
        bool bestSharesJunction = false;
        for (std::size_t i = 0; i < n; ++i) {
            if (used[i]) {
                continue;
            }
            for (int r = 0; r < 2; ++r) {
                const bool reversed = r != 0;
                const double distance = length_um(entry_of(cols[i], reversed) - cur);
                const auto& entryJunction = entry_junction_of(cols[i], reversed);
                const bool sharesJunction = curJunction && entryJunction &&
                                            *curJunction == *entryJunction &&
                                            distance <= static_cast<double>(junctionMaxGap.value);
                if (best == n || (sharesJunction && !bestSharesJunction) ||
                    (sharesJunction == bestSharesJunction && distance < bestDist)) {
                    bestDist = distance;
                    best = i;
                    bestRev = reversed;
                    bestSharesJunction = sharesJunction;
                }
            }
        }
        used[best] = 1;
        order.push_back(best);
        cur = exit_of(cols[best], bestRev);
        curJunction = exit_junction_of(cols[best], bestRev);
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

    std::vector<std::size_t> order = greedy_order(columns, origin, config.underpath_max);
    std::vector<char> reversed;
    OrientationCost cost =
        best_orientation(columns, order, origin, config.underpath_max, reversed);

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
                    const OrientationCost c2 =
                        best_orientation(columns, cand, origin, config.underpath_max, rev2);
                    if (better(c2, cost, 1.0)) {  // marge anti-oscillation (1 µm)
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
    plan.travel_um = cost.travel_um;
    plan.junction_links = cost.junction_links;
    Vec2um cur = origin;
    const RouteColumn* previous = nullptr;
    bool previousReversed = false;
    for (std::size_t i = 0; i < n; ++i) {
        const RouteColumn& col = columns[order[i]];
        const bool rev = reversed[i] != 0;
        RouteStep step;
        step.column_index = order[i];
        step.reversed = rev;
        const double gap = length_um(entry_of(col, rev) - cur);
        if (previous != nullptr) {
            step.junction =
                shared_junction(*previous, previousReversed, col, rev, config.underpath_max);
        }
        if (i == 0) {
            step.connector = ConnectorKind::Start;
        } else {
            // Une jonction commune validée garantit que l'espace entre les
            // deux colonnes appartient au même réseau (tissu réel) : le
            // trajet caché tolère alors jusqu'à `underpath_max`. Sans elle,
            // aucune garantie géométrique n'existe — une simple proximité
            // peut être fortuite (deux formes disjointes rapprochées par
            // hasard) — le trajet caché n'est accordé que pour un quasi-contact
            // (`underpath_max_without_junction`, bien plus strict).
            const double limit = step.junction.has_value()
                                     ? static_cast<double>(config.underpath_max.value)
                                     : static_cast<double>(
                                           config.underpath_max_without_junction.value);
            if (gap <= limit) {
                step.connector = ConnectorKind::Underpath;
                ++plan.underpaths;
            } else {
                step.connector = ConnectorKind::Jump;
                ++plan.jumps;
            }
        }
        plan.steps.push_back(step);
        cur = exit_of(col, rev);
        previous = &col;
        previousReversed = rev;
    }
    return plan;
}

}  // namespace openstitch::stitch_generation
