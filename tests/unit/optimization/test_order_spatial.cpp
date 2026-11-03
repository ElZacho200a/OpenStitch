// SPDX-License-Identifier: Apache-2.0
// HP-PERF-011 : l'index spatial du plus proche voisin doit reproduire exactement le balayage
// lineaire historique (meme ordre, meme sens), y compris sur des egalites exactes.
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <limits>
#include <optional>
#include <random>
#include <vector>

#include "openstitch/optimization/order.hpp"

using namespace openstitch;
using namespace openstitch::optimization;

namespace {

struct RefPick {
    double d;
    bool rev;
};

RefPick ref_best_next(Vec2um from, const OrderItem& c) {
    const double fwd = length_um(from - item_entry(c));
    if (c.reversible) {
        const double rev = length_um(from - item_exit(c));
        if (rev < fwd) {
            return {rev, true};
        }
    }
    return {fwd, false};
}

OrderItem ref_oriented(OrderItem it, bool reverse) {
    if (reverse) {
        const Vec2um in = item_entry(it);
        const Vec2um out = item_exit(it);
        it.entry = out;
        it.exit = in;
        it.reversed = !it.reversed;
    }
    return it;
}

// Copie de l'ancien ByProximity (O(n^2)).
std::vector<OrderStep> ref_proximity(const std::vector<OrderItem>& items,
                                     const std::optional<Vec2um>& start) {
    std::vector<OrderItem> out;
    std::vector<bool> used(items.size(), false);
    std::size_t first = 0;
    bool firstRev = false;
    if (start) {
        double best = std::numeric_limits<double>::max();
        for (std::size_t j = 0; j < items.size(); ++j) {
            const RefPick p = ref_best_next(*start, items[j]);
            if (p.d < best) {
                best = p.d;
                first = j;
                firstRev = p.rev;
            }
        }
    }
    used[first] = true;
    out.push_back(ref_oriented(items[first], firstRev));
    for (std::size_t step = 1; step < items.size(); ++step) {
        const Vec2um from = item_exit(out.back());
        double best = std::numeric_limits<double>::max();
        std::size_t bestIdx = 0;
        bool bestRev = false;
        for (std::size_t j = 0; j < items.size(); ++j) {
            if (used[j]) {
                continue;
            }
            const RefPick p = ref_best_next(from, items[j]);
            if (p.d < best) {
                best = p.d;
                bestIdx = j;
                bestRev = p.rev;
            }
        }
        used[bestIdx] = true;
        out.push_back(ref_oriented(items[bestIdx], bestRev));
    }
    std::vector<OrderStep> steps;
    for (const auto& it : out) {
        steps.push_back({it.id, it.reversed});
    }
    return steps;
}

std::vector<OrderItem> synthetic(std::size_t n, std::uint32_t seed, std::int32_t extent,
                                 bool lattice, bool reversible) {
    std::mt19937 rng(seed);
    std::vector<OrderItem> items;
    for (std::size_t i = 0; i < n; ++i) {
        OrderItem it;
        it.id = ObjectId{i + 1};
        const auto c = static_cast<std::uint8_t>(rng() % 5);
        it.rgb = {c, c, c};
        const auto coord = [&] {
            const auto v = static_cast<std::int32_t>(rng() % static_cast<std::uint32_t>(extent));
            return lattice ? (v / 1000) * 1000 : v; // lattice : beaucoup d'egalites exactes
        };
        it.centroid = Vec2um{Micrometers{coord()}, Micrometers{coord()}};
        it.area_mm2 = static_cast<double>(rng() % 1000) / 10.0;
        if (reversible) {
            it.reversible = true;
            it.entry = Vec2um{Micrometers{coord()}, Micrometers{coord()}};
            it.exit = Vec2um{Micrometers{coord()}, Micrometers{coord()}};
        }
        items.push_back(it);
    }
    return items;
}

OrderItem point_item(std::uint64_t id, std::int32_t x, std::int32_t y) {
    OrderItem it;
    it.id = ObjectId{id};
    it.rgb = {200, 0, 0};
    it.centroid = Vec2um{Micrometers{x}, Micrometers{y}};
    return it;
}

} // namespace

TEST_CASE("HP-PERF-011 : ByProximity identique au balayage lineaire (grille)") {
    for (const std::size_t n : {2U, 10U, 47U, 48U, 49U, 150U, 600U}) {
        for (const bool lattice : {false, true}) {
            for (const bool reversible : {false, true}) {
                for (const std::uint32_t seed : {1U, 7U}) {
                    const auto items = synthetic(n, seed, 120'000, lattice, reversible);
                    for (const bool withStart : {false, true}) {
                        OrderOptions opt;
                        if (withStart) {
                            opt.start = Vec2um{Micrometers{-30'000}, Micrometers{250'000}};
                        }
                        const auto got = plan_order(items, OrderStrategy::ByProximity, opt);
                        const auto want = ref_proximity(items, opt.start);
                        REQUIRE(got.size() == want.size());
                        for (std::size_t i = 0; i < got.size(); ++i) {
                            REQUIRE(got[i].id.value == want[i].id.value);
                            REQUIRE(got[i].reversed == want[i].reversed);
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("HP-PERF-011 : points confondus et alignes restent deterministes") {
    std::vector<OrderItem> items;
    for (std::uint64_t i = 0; i < 100; ++i) {
        items.push_back(point_item(i + 1, 0, 0)); // tous confondus
    }
    for (std::uint64_t i = 0; i < 100; ++i) {
        items.push_back(point_item(1000 + i, static_cast<std::int32_t>(i) * 500, 0)); // ligne
    }
    const auto got = plan_order(items, OrderStrategy::ByProximity);
    const auto want = ref_proximity(items, std::nullopt);
    REQUIRE(got.size() == want.size());
    for (std::size_t i = 0; i < got.size(); ++i) {
        REQUIRE(got[i].id.value == want[i].id.value);
    }
}

TEST_CASE("HP-PERF-011 : ByColor garde l'ordre de premiere apparition") {
    const auto items = synthetic(300, 5, 100'000, false, false);
    const auto got = optimize_order(items, OrderStrategy::ByColor);
    std::vector<std::uint64_t> want;
    std::vector<std::uint8_t> seen;
    for (const auto& a : items) {
        bool known = false;
        for (const auto c : seen) {
            known = known || c == a.rgb[0];
        }
        if (known) {
            continue;
        }
        seen.push_back(a.rgb[0]);
        for (const auto& b : items) {
            if (b.rgb == a.rgb) {
                want.push_back(b.id.value);
            }
        }
    }
    REQUIRE(got.size() == want.size());
    for (std::size_t i = 0; i < got.size(); ++i) {
        REQUIRE(got[i].value == want[i]);
    }
}

TEST_CASE("HP-PERF-011 : 1000 objets en ByProximity sous un seuil large") {
    const auto items = synthetic(1000, 3, 300'000, false, true);
    const auto t0 = std::chrono::steady_clock::now();
    const auto got = plan_order(items, OrderStrategy::ByProximity);
    const auto ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    CHECK(got.size() == 1000);
    INFO("duree ByProximity n=1000 : " << ms << " ms");
    CHECK(ms < 500.0);
}

TEST_CASE("HP-PERF-011 : mesure ByProximity 5000 objets") {
    const auto items = synthetic(5000, 11, 400'000, false, true);
    const auto t0 = std::chrono::steady_clock::now();
    const auto got = plan_order(items, OrderStrategy::ByProximity);
    const auto ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    CHECK(got.size() == 5000);
    const auto t1 = std::chrono::steady_clock::now();
    const auto want = ref_proximity(items, std::nullopt);
    const auto refMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
    for (std::size_t i = 0; i < got.size(); ++i) {
        REQUIRE(got[i].id.value == want[i].id.value);
    }
    WARN("ByProximity n=5000 : grille " << ms << " ms, balayage lineaire " << refMs << " ms");
}
