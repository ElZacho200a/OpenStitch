// SPDX-License-Identifier: Apache-2.0
#include "openstitch/optimization/order.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>

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

// Candidat retenu : coût minimal, puis plus petit indice (déterminisme, identique au balayage
// linéaire historique qui n'accepte qu'un coût strictement inférieur dans l'ordre croissant).
struct Best {
    double d{std::numeric_limits<double>::max()};
    std::size_t idx{std::numeric_limits<std::size_t>::max()};
    bool reversed{false};

    void offer(const Pick& p, std::size_t j) {
        if (p.d < d || (p.d == d && j < idx)) {
            d = p.d;
            idx = j;
            reversed = p.reversed;
        }
    }
};

// HP-PERF-011 : grille uniforme des extrémités (entrée, et sortie si l'objet est réversible).
// La recherche du plus proche voisin balaie des anneaux de cellules et s'arrête quand la borne
// inférieure de l'anneau dépasse le meilleur coût ; les égalités sont départagées par l'indice,
// donc le résultat est exactement celui du balayage linéaire.
class PointGrid {
public:
    explicit PointGrid(const std::vector<OrderItem>& items) : items_(items) {
        std::int64_t minX = std::numeric_limits<std::int64_t>::max();
        std::int64_t minY = minX;
        std::int64_t maxX = std::numeric_limits<std::int64_t>::min();
        std::int64_t maxY = maxX;
        std::size_t points = 0;
        const auto grow = [&](Vec2um p) {
            minX = std::min<std::int64_t>(minX, p.x.value);
            maxX = std::max<std::int64_t>(maxX, p.x.value);
            minY = std::min<std::int64_t>(minY, p.y.value);
            maxY = std::max<std::int64_t>(maxY, p.y.value);
            ++points;
        };
        for (const auto& it : items) {
            grow(item_entry(it));
            if (it.reversible) {
                grow(item_exit(it));
            }
        }
        minX_ = minX;
        minY_ = minY;
        const double w = static_cast<double>(maxX - minX) + 1.0;
        const double h = static_cast<double>(maxY - minY) + 1.0;
        const double target = std::max(1.0, static_cast<double>(points) / 2.0);
        std::int64_t cs = std::max<std::int64_t>(
            1, static_cast<std::int64_t>(std::ceil(std::sqrt(w * h / target))));
        const auto limit = static_cast<std::int64_t>(points) * 4 + 16;
        while (((maxX - minX) / cs + 1) * ((maxY - minY) / cs + 1) > limit) {
            cs *= 2;
        }
        cell_ = cs;
        gx_ = (maxX - minX) / cs + 1;
        gy_ = (maxY - minY) / cs + 1;
        cells_.resize(static_cast<std::size_t>(gx_ * gy_));
        for (std::size_t j = 0; j < items.size(); ++j) {
            const std::size_t a = cell_of(item_entry(items[j]));
            cells_[a].push_back(j);
            if (items[j].reversible) {
                const std::size_t b = cell_of(item_exit(items[j]));
                if (b != a) {
                    cells_[b].push_back(j);
                }
            }
        }
    }

    void remove(std::size_t j) {
        const auto drop = [&](std::size_t c) {
            auto& v = cells_[c];
            const auto it = std::find(v.begin(), v.end(), j);
            if (it != v.end()) {
                *it = v.back();
                v.pop_back();
            }
        };
        drop(cell_of(item_entry(items_[j])));
        if (items_[j].reversible) {
            drop(cell_of(item_exit(items_[j])));
        }
    }

    [[nodiscard]] Best nearest(Vec2um from) const {
        Best best;
        const std::int64_t cx =
            clamp_axis((static_cast<std::int64_t>(from.x.value) - minX_) / cell_, gx_);
        const std::int64_t cy =
            clamp_axis((static_cast<std::int64_t>(from.y.value) - minY_) / cell_, gy_);
        const std::int64_t maxR = std::max(gx_, gy_);
        for (std::int64_t r = 0; r <= maxR; ++r) {
            if (best.idx != std::numeric_limits<std::size_t>::max()) {
                // Borne inférieure de la distance à un anneau r (marge pour l'arrondi flottant).
                const double lb = static_cast<double>((r - 1) * cell_);
                if (lb > best.d * (1.0 + 1e-9) + 1.0) {
                    break;
                }
            }
            const std::int64_t y0 = cy - r;
            const std::int64_t y1 = cy + r;
            for (std::int64_t y = std::max<std::int64_t>(y0, 0); y <= std::min(y1, gy_ - 1); ++y) {
                const bool edgeRow = (y == y0 || y == y1);
                const std::int64_t step = edgeRow ? 1 : std::max<std::int64_t>(2 * r, 1);
                for (std::int64_t x = cx - r; x <= cx + r; x += step) {
                    if (x < 0 || x >= gx_) {
                        continue;
                    }
                    for (const std::size_t j : cells_[static_cast<std::size_t>(y * gx_ + x)]) {
                        best.offer(best_next(from, items_[j]), j);
                    }
                }
            }
        }
        return best;
    }

private:
    static std::int64_t clamp_axis(std::int64_t v, std::int64_t n) {
        return std::clamp<std::int64_t>(v, 0, n - 1);
    }
    [[nodiscard]] std::size_t cell_of(Vec2um p) const {
        const std::int64_t x =
            clamp_axis((static_cast<std::int64_t>(p.x.value) - minX_) / cell_, gx_);
        const std::int64_t y =
            clamp_axis((static_cast<std::int64_t>(p.y.value) - minY_) / cell_, gy_);
        return static_cast<std::size_t>(y * gx_ + x);
    }

    const std::vector<OrderItem>& items_;
    std::int64_t minX_{0};
    std::int64_t minY_{0};
    std::int64_t cell_{1};
    std::int64_t gx_{1};
    std::int64_t gy_{1};
    std::vector<std::vector<std::size_t>> cells_;
};

// En deçà de ce nombre d'objets le balayage linéaire est aussi rapide que la grille.
constexpr std::size_t kGridThreshold = 48;

// Plus proche voisin (extrémités ; centres si aucune n'est déclarée) à partir de `from`.
std::vector<OrderItem> nearest_chain(Vec2um from, const std::vector<OrderItem>& rest) {
    std::vector<OrderItem> out;
    out.reserve(rest.size());
    if (rest.size() >= kGridThreshold) {
        PointGrid grid(rest);
        for (std::size_t step = 0; step < rest.size(); ++step) {
            const Best b = grid.nearest(from);
            grid.remove(b.idx);
            out.push_back(oriented(rest[b.idx], b.reversed));
            from = item_exit(out.back());
        }
        return out;
    }
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
        std::map<std::array<std::uint8_t, 3>, std::size_t> groupOf;
        std::vector<std::vector<std::size_t>> groups;
        for (std::size_t i = 0; i < items.size(); ++i) {
            const auto [pos, inserted] = groupOf.try_emplace(items[i].rgb, groups.size());
            if (inserted) {
                groups.emplace_back();
            }
            groups[pos->second].push_back(i);
        }
        std::vector<OrderItem> out;
        out.reserve(items.size());
        for (const auto& g : groups) {
            for (const std::size_t i : g) {
                out.push_back(items[i]);
            }
        }
        return out;
    }

    if (strategy == OrderStrategy::ByProximity) {
        // Plus proche voisin à partir du premier item.
        std::size_t first = 0;
        bool firstRev = false;
        if (options.start) {
            // HP-SEQ-008 : le premier objet est le plus proche du point de départ du design.
            double best = std::numeric_limits<double>::max();
            for (std::size_t j = 0; j < items.size(); ++j) {
                const Pick p = best_next(*options.start, items[j]);
                if (p.d < best) {
                    best = p.d;
                    first = j;
                    firstRev = p.reversed;
                }
            }
        }
        std::vector<OrderItem> out;
        out.reserve(items.size());
        out.push_back(oriented(items[first], firstRev));
        std::vector<OrderItem> rest;
        rest.reserve(items.size() - 1);
        for (std::size_t j = 0; j < items.size(); ++j) {
            if (j != first) {
                rest.push_back(items[j]);
            }
        }
        const std::vector<OrderItem> chained = nearest_chain(item_exit(out.back()), rest);
        out.insert(out.end(), chained.begin(), chained.end());
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

// HP-SEQ-001 : amélioration locale déterministe. `slots` = indices (dans `full`) des items
// libres, dans l'ordre. Chaque candidat (inversion de segment 2-opt, déplacement de segment
// Or-opt, retournement d'un objet) est évalué sur la séquence complète ; premier gain strict
// accepté ; arrêt quand plus aucun gain ou quand `budget` candidats ont été évalués.
void improve_sequence(std::vector<OrderItem>& full, const std::vector<std::size_t>& slots,
                      const OrderOptions& options) {
    const std::size_t m = slots.size();
    if (options.improve_budget == 0 || m < 2) {
        return;
    }
    std::size_t budget = options.improve_budget;
    std::vector<OrderItem> fv;
    fv.reserve(m);
    for (const std::size_t s : slots) {
        fv.push_back(full[s]);
    }
    const auto place = [&](const std::vector<OrderItem>& cand) {
        std::vector<OrderItem> out = full;
        for (std::size_t k = 0; k < m; ++k) {
            out[slots[k]] = cand[k];
        }
        return out;
    };
    const auto scoreOf = [&](const std::vector<OrderItem>& cand) {
        return compute_cost(place(cand), options.start, options.end).score();
    };
    double best = scoreOf(fv);
    constexpr double kEps = 1e-6;
    const auto flip = [](OrderItem& it) {
        if (it.reversible) {
            it = oriented(it, true);
        }
    };

    bool improved = true;
    while (improved && budget > 0) {
        improved = false;
        // Retournement d'un objet.
        for (std::size_t i = 0; i < m && budget > 0; ++i) {
            if (!fv[i].reversible) {
                continue;
            }
            --budget;
            auto cand = fv;
            flip(cand[i]);
            const double sc = scoreOf(cand);
            if (sc + kEps < best) {
                fv = std::move(cand);
                best = sc;
                improved = true;
            }
        }
        // 2-opt : inversion du segment [i, j].
        for (std::size_t i = 0; i + 1 < m && budget > 0; ++i) {
            for (std::size_t j = i + 1; j < m && budget > 0; ++j) {
                --budget;
                auto cand = fv;
                std::reverse(cand.begin() + static_cast<std::ptrdiff_t>(i),
                             cand.begin() + static_cast<std::ptrdiff_t>(j) + 1);
                for (std::size_t k = i; k <= j; ++k) {
                    flip(cand[k]);
                }
                const double sc = scoreOf(cand);
                if (sc + kEps < best) {
                    fv = std::move(cand);
                    best = sc;
                    improved = true;
                }
            }
        }
        // Or-opt : déplace un segment de 1 à 3 objets vers une autre position.
        for (std::size_t len = 1; len <= 3 && len < m; ++len) {
            for (std::size_t i = 0; i + len <= m && budget > 0; ++i) {
                for (std::size_t to = 0; to + len <= m && budget > 0; ++to) {
                    if (to == i) {
                        continue;
                    }
                    --budget;
                    auto cand = fv;
                    const auto b = cand.begin() + static_cast<std::ptrdiff_t>(i);
                    std::vector<OrderItem> seg(b, b + static_cast<std::ptrdiff_t>(len));
                    cand.erase(b, b + static_cast<std::ptrdiff_t>(len));
                    cand.insert(cand.begin() + static_cast<std::ptrdiff_t>(to), seg.begin(),
                                seg.end());
                    const double sc = scoreOf(cand);
                    if (sc + kEps < best) {
                        fv = std::move(cand);
                        best = sc;
                        improved = true;
                    }
                }
            }
        }
    }
    full = place(fv);
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
    return compute_cost(items, std::nullopt, std::nullopt);
}

OrderCost compute_cost(const std::vector<OrderItem>& items, const std::optional<Vec2um>& start,
                       const std::optional<Vec2um>& end) {
    OrderCost cost;
    if (!items.empty()) {
        if (start) {
            cost.travel_um += distance(*start, item_entry(items.front()));
        }
        if (end) {
            cost.travel_um += distance(item_exit(items.back()), *end);
        }
    }
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

    // Séquence complète (verrous à leur emplacement) puis amélioration locale éventuelle.
    std::vector<OrderItem> full;
    std::vector<std::size_t> slots;
    full.reserve(items.size());
    std::size_t freeIdx = 0;
    for (const auto& item : items) {
        if (item.locked) {
            full.push_back(item);
        } else {
            slots.push_back(full.size());
            full.push_back(arranged[freeIdx++]);
        }
    }
    improve_sequence(full, slots, options);

    std::vector<OrderStep> result;
    result.reserve(items.size());
    for (const auto& item : full) {
        result.push_back({item.id, item.locked ? false : item.reversed});
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

std::vector<JumpSegment> jump_segments(const std::vector<SewnSpan>& spans) {
    std::vector<JumpSegment> out;
    for (std::size_t i = 1; i < spans.size(); ++i) {
        if (spans[i - 1].last == spans[i].first) {
            continue;
        }
        out.push_back({spans[i - 1].last, spans[i].first});
    }
    return out;
}

std::int64_t jump_length_um(const std::vector<JumpSegment>& jumps) {
    std::int64_t total = 0;
    for (const JumpSegment& j : jumps) {
        total += static_cast<std::int64_t>(std::llround(distance(j.from, j.to)));
    }
    return total;
}

} // namespace openstitch::optimization
