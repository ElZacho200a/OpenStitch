// SPDX-License-Identifier: Apache-2.0
#include "openstitch/optimization/order.hpp"

#include <algorithm>
#include <limits>

namespace openstitch::optimization {

namespace {

double distance(Vec2um a, Vec2um b) {
    return length_um(a - b);
}

struct Pick {
    double d{0.0};
    bool reversed{false};
};

// Coût d'enchaînement de `from` (sortie du précédent) vers `cand`, dans le meilleur sens permis.
// À égalité le sens naturel gagne (déterminisme).
Pick best_next(Vec2um from, const OrderItem& cand) {
    const double fwd = distance(from, item_entry(cand));
    if (cand.reversible) {
        const double rev = distance(from, item_exit(cand));
        if (rev < fwd) {
            return {rev, true};
        }
    }
    return {fwd, false};
}

OrderItem oriented(OrderItem it, bool reverse) {
    if (reverse) {
        const Vec2um in = item_entry(it);
        const Vec2um out = item_exit(it);
        it.entry = out;
        it.exit = in;
        it.reversed = !it.reversed;
    }
    return it;
}

// Plus proche voisin (extrémités ; centres si aucune n'est déclarée) à partir de `from`.
std::vector<OrderItem> nearest_chain(Vec2um from, const std::vector<OrderItem>& rest) {
    std::vector<OrderItem> out;
    out.reserve(rest.size());
    std::vector<bool> used(rest.size(), false);
    for (std::size_t step = 0; step < rest.size(); ++step) {
        double best = std::numeric_limits<double>::max();
        std::size_t bestIdx = 0;
        bool bestRev = false;
        for (std::size_t j = 0; j < rest.size(); ++j) {
            if (used[j]) {
                continue;
            }
            const Pick p = best_next(from, rest[j]);
            if (p.d < best) {
                best = p.d;
                bestIdx = j;
                bestRev = p.reversed;
            }
        }
        used[bestIdx] = true;
        out.push_back(oriented(rest[bestIdx], bestRev));
        from = item_exit(out.back());
    }
    return out;
}

std::vector<OrderItem> arrange_free(const std::vector<OrderItem>& items, OrderStrategy strategy,
                                    const OrderOptions& options);

// Couches (Lot C) : couleurs par aire max décroissante, puis grandes zones
// par aire décroissante, puis petites par proximité. Tris stables : à
// égalité, l'ordre d'origine décide (déterminisme).
std::vector<OrderItem> arrange_layered(const std::vector<OrderItem>& items,
                                       const OrderOptions& options) {
    const std::vector<OrderItem> byColor = arrange_free(items, OrderStrategy::ByColor, options);
    std::vector<std::vector<OrderItem>> groups;
    for (const auto& it : byColor) {
        if (groups.empty() || groups.back().front().rgb != it.rgb) {
            groups.emplace_back();
        }
        groups.back().push_back(it);
    }
    const auto maxArea = [](const std::vector<OrderItem>& g) {
        double m = 0.0;
        for (const auto& it : g) {
            m = std::max(m, it.area_mm2);
        }
        return m;
    };
    std::stable_sort(groups.begin(), groups.end(),
                     [&](const auto& a, const auto& b) { return maxArea(a) > maxArea(b); });

    std::vector<OrderItem> out;
    out.reserve(items.size());
    for (const auto& g : groups) {
        const double threshold = maxArea(g) * options.layer_large_area_ratio;
        std::vector<OrderItem> large;
        std::vector<OrderItem> small;
        for (const auto& it : g) {
            (it.area_mm2 >= threshold ? large : small).push_back(it);
        }
        std::stable_sort(large.begin(), large.end(), [](const OrderItem& a, const OrderItem& b) {
            return a.area_mm2 > b.area_mm2;
        });
        out.insert(out.end(), large.begin(), large.end());
        if (!small.empty()) {
            const Vec2um from = large.empty() ? item_entry(small.front()) : item_exit(large.back());
            const auto chained = nearest_chain(from, small);
            out.insert(out.end(), chained.begin(), chained.end());
        }
    }
    return out;
}

// Ordonne des items libres selon la stratégie (sans contrainte de verrou).
std::vector<OrderItem> arrange_free(const std::vector<OrderItem>& items, OrderStrategy strategy,
                                    const OrderOptions& options) {
    if (strategy == OrderStrategy::Document || items.size() < 2) {
        return items;
    }

    if (strategy == OrderStrategy::ByColor) {
        // Regroupe les couleurs dans l'ordre de première apparition (stable).
        std::vector<OrderItem> out;
        std::vector<bool> used(items.size(), false);
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (used[i]) {
                continue;
            }
            for (std::size_t j = i; j < items.size(); ++j) {
                if (!used[j] && items[j].rgb == items[i].rgb) {
                    out.push_back(items[j]);
                    used[j] = true;
                }
            }
        }
        return out;
    }

    if (strategy == OrderStrategy::ByProximity) {
        // Plus proche voisin à partir du premier item.
        std::vector<OrderItem> out;
        std::vector<bool> used(items.size(), false);
        used[0] = true;
        out.push_back(items[0]);
        for (std::size_t step = 1; step < items.size(); ++step) {
            const Vec2um from = item_exit(out.back());
            double best = std::numeric_limits<double>::max();
            std::size_t bestIdx = 0;
            bool bestRev = false;
            for (std::size_t j = 0; j < items.size(); ++j) {
                if (used[j]) {
                    continue;
                }
                const Pick p = best_next(from, items[j]);
                if (p.d < best) {
                    best = p.d;
                    bestIdx = j;
                    bestRev = p.reversed;
                }
            }
            used[bestIdx] = true;
            out.push_back(oriented(items[bestIdx], bestRev));
        }
        return out;
    }

    if (strategy == OrderStrategy::LayeredColorThenProximity) {
        return arrange_layered(items, options);
    }

    // ColorThenProximity : groupes de couleur (ordre d'apparition), proximité
    // à l'intérieur de chaque groupe.
    std::vector<OrderItem> byColor = arrange_free(items, OrderStrategy::ByColor, options);
    std::vector<OrderItem> out;
    std::size_t i = 0;
    while (i < byColor.size()) {
        std::size_t j = i;
        while (j < byColor.size() && byColor[j].rgb == byColor[i].rgb) {
            ++j;
        }
        std::vector<OrderItem> group(byColor.begin() + static_cast<std::ptrdiff_t>(i),
                                     byColor.begin() + static_cast<std::ptrdiff_t>(j));
        const auto arranged = arrange_free(group, OrderStrategy::ByProximity, options);
        out.insert(out.end(), arranged.begin(), arranged.end());
        i = j;
    }
    return out;
}

} // namespace

Vec2um item_entry(const OrderItem& item) {
    return item.entry.value_or(item.centroid);
}

Vec2um item_exit(const OrderItem& item) {
    return item.exit.value_or(item.centroid);
}

std::size_t nearest_vertex_index(const std::vector<Vec2um>& ring, Vec2um from) {
    std::size_t best = 0;
    double bestD = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const double d = distance(ring[i], from);
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return best;
}

OrderCost compute_cost(const std::vector<OrderItem>& items) {
    OrderCost cost;
    for (std::size_t i = 1; i < items.size(); ++i) {
        cost.travel_um += distance(item_exit(items[i - 1]), item_entry(items[i]));
        if (items[i].rgb != items[i - 1].rgb) {
            ++cost.color_changes;
        }
    }
    return cost;
}

std::vector<OrderStep> plan_order(const std::vector<OrderItem>& items, OrderStrategy strategy,
                                  const OrderOptions& options) {
    // Items libres réarrangés ; les verrous gardent leur emplacement et leur sens.
    std::vector<OrderItem> free;
    for (const auto& item : items) {
        if (!item.locked) {
            free.push_back(item);
        }
    }
    const std::vector<OrderItem> arranged = arrange_free(free, strategy, options);

    std::vector<OrderStep> result;
    result.reserve(items.size());
    std::size_t freeIdx = 0;
    for (const auto& item : items) {
        if (item.locked) {
            result.push_back({item.id, false});
        } else {
            const OrderItem& placed = arranged[freeIdx++];
            result.push_back({placed.id, placed.reversed});
        }
    }
    return result;
}

std::vector<ObjectId> optimize_order(const std::vector<OrderItem>& items, OrderStrategy strategy,
                                     const OrderOptions& options) {
    std::vector<ObjectId> result;
    result.reserve(items.size());
    for (const OrderStep& step : plan_order(items, strategy, options)) {
        result.push_back(step.id);
    }
    return result;
}

} // namespace openstitch::optimization
