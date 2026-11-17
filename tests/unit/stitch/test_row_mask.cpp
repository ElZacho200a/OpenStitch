// SPDX-License-Identifier: Apache-2.0
// Masque de rangées du tatami (fondu à la frontière, lot L1a) :
// docs/studio-fondu-frontiere-spec.md §6.1.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <set>
#include <vector>

#include "openstitch/stitch_generation/row_mask.hpp"
#include "openstitch/stitch_generation/tatami.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

namespace {

geometry::PathSet rect(std::int32_t x0, std::int32_t y0, std::int32_t w, std::int32_t h) {
    geometry::Path p;
    p.closed = true;
    const auto node = [](std::int32_t x, std::int32_t y) {
        return geometry::PathNode{
            Vec2um{Micrometers{x}, Micrometers{y}}, geometry::NodeType::Corner, {}, {}};
    };
    p.nodes = {node(x0, y0), node(x0 + w, y0), node(x0 + w, y0 + h), node(x0, y0 + h)};
    geometry::PathSet set;
    set.outer = p;
    return set;
}

document::TatamiParams flat_params() {
    document::TatamiParams p;
    p.angle = Angle{0.0};
    p.row_spacing = Micrometers{400};
    p.stitch_length = Micrometers{3'000};
    p.inset = Micrometers{0};
    return p;
}

DensityRowMask uniform(double d, DensityRowMask::Slice slice) {
    return DensityRowMask([d](Vec2um) { return d; }, slice);
}

// Points cousus le long d'une rangée (angle 0 : même y, ni saut ni trajet caché).
struct RowStitch {
    double y;
    double x0;
    double x1;
};

std::vector<RowStitch> row_stitches(const std::vector<FillStitch>& f) {
    std::vector<RowStitch> out;
    for (std::size_t i = 1; i < f.size(); ++i) {
        const FillStitch& a = f[i - 1];
        const FillStitch& b = f[i];
        if (b.jump || b.travel || a.travel || a.pos.y.value != b.pos.y.value) {
            continue;
        }
        const double xa = a.pos.x.value;
        const double xb = b.pos.x.value;
        out.push_back({static_cast<double>(a.pos.y.value), std::min(xa, xb), std::max(xa, xb)});
    }
    return out;
}

// Longueur cousue le long des rangées dans la bande [lo ; hi) en x.
double covered(const std::vector<RowStitch>& rows, double lo, double hi) {
    double sum = 0.0;
    for (const RowStitch& r : rows) {
        sum += std::max(0.0, std::min(r.x1, hi) - std::max(r.x0, lo));
    }
    return sum;
}

std::set<std::int32_t> row_ys(const std::vector<FillStitch>& f) {
    std::set<std::int32_t> ys;
    for (const RowStitch& r : row_stitches(f)) {
        ys.insert(static_cast<std::int32_t>(r.y));
    }
    return ys;
}

} // namespace

TEST_CASE("row_threshold: golden Weyl sequence in [0,1)", "[row_mask]") {
    CHECK(row_threshold(0) == 0.0);
    CHECK(row_threshold(1) == Catch::Approx(0.6180339887));
    CHECK(row_threshold(-1) == Catch::Approx(1.0 - 0.6180339887));
    int below = 0;
    for (std::int64_t k = -500; k < 500; ++k) {
        const double t = row_threshold(k);
        REQUIRE(t >= 0.0);
        REQUIRE(t < 1.0);
        below += t < 0.3 ? 1 : 0;
    }
    // Répartition quasi uniforme : 30 % des seuils sous 0,3 (à quelques rangées près).
    CHECK(std::abs(below - 300) <= 5);
}

TEST_CASE("row mask: null mask is byte identical to plain tatami", "[row_mask]") {
    const auto region = rect(-1'700, -2'300, 23'000, 17'000);
    const auto p = flat_params();
    CHECK(fill_tatami(region, p, nullptr) == fill_tatami(region, p));
}

TEST_CASE("row mask: full mask on a grid-aligned region equals plain tatami", "[row_mask]") {
    // minY = 0 : la grille ancrée (k + 1/2) × écart coïncide avec la grille relative.
    const auto region = rect(0, 0, 20'000, 20'000);
    const auto p = flat_params();
    const auto full = uniform(1.0, DensityRowMask::Slice::Lower);
    CHECK(fill_tatami(region, p, &full) == fill_tatami(region, p));
}

TEST_CASE("row mask: complementary masks share every row segment exactly once", "[row_mask]") {
    const auto region = rect(0, 0, 30'000, 20'000);
    const auto p = flat_params();
    const auto ramp = [](Vec2um q) { return static_cast<double>(q.x.value) / 30'000.0; };
    const DensityRowMask maskA(ramp, DensityRowMask::Slice::Lower);
    const DensityRowMask maskB([&](Vec2um q) { return 1.0 - ramp(q); },
                               DensityRowMask::Slice::Upper);
    const auto full = uniform(1.0, DensityRowMask::Slice::Lower);

    const auto rowsA = row_stitches(fill_tatami(region, p, &maskA));
    const auto rowsB = row_stitches(fill_tatami(region, p, &maskB));
    const auto rowsFull = row_stitches(fill_tatami(region, p, &full));

    for (double lo = 0.0; lo < 30'000.0; lo += 3'000.0) {
        const double a = covered(rowsA, lo, lo + 3'000.0);
        const double b = covered(rowsB, lo, lo + 3'000.0);
        const double f = covered(rowsFull, lo, lo + 3'000.0);
        INFO("band " << lo << " : A " << a << " B " << b << " full " << f);
        REQUIRE(f > 0.0);
        // Partition : chaque intervalle de pénétrations est cousu par une seule couche
        // (écart dû au seul arrondi au µm des positions).
        CHECK(std::abs(a + b - f) <= 0.005 * f);
        // Proportion de A ≈ densité au milieu de la bande.
        const double d = (lo + 1'500.0) / 30'000.0;
        CHECK(a / f == Catch::Approx(d).margin(0.1));
    }
}

TEST_CASE("row mask: anchored rows coincide across different regions", "[row_mask]") {
    const auto p = flat_params();
    const auto full = uniform(1.0, DensityRowMask::Slice::Lower);
    const auto inner = rect(0, 0, 20'000, 20'000);
    const auto taller = rect(0, -3'700, 20'000, 27'400); // minY négatif, non aligné

    const auto ysInner = row_ys(fill_tatami(inner, p, &full));
    const auto ysTaller = row_ys(fill_tatami(taller, p, &full));
    for (const std::int32_t y : ysInner) {
        CHECK(ysTaller.count(y) == 1);
    }
    // Sans masque, la grille relative de la grande région est décalée : l'ancrage est utile.
    const auto ysPlain = row_ys(fill_tatami(taller, p));
    CHECK(ysPlain.count(*ysInner.begin()) == 0);

    // Rangées d'indice négatif : briques valides, aucun point hors de la région.
    for (const FillStitch& s : fill_tatami(taller, p, &full)) {
        CHECK(s.pos.y.value >= -3'700 - 2);
        CHECK(s.pos.y.value <= 23'700 + 2);
    }
}

TEST_CASE("row mask: thinned rows stay linked without extra jumps", "[row_mask]") {
    const auto region = rect(0, 0, 20'000, 20'000);
    const auto p = flat_params();
    const auto half = uniform(0.5, DensityRowMask::Slice::Lower);
    const auto stitches = fill_tatami(region, p, &half);
    const auto ys = row_ys(stitches);
    // Environ une rangée sur deux (50 rangées dans 20 mm).
    CHECK(ys.size() >= 22);
    CHECK(ys.size() <= 28);
    std::size_t jumps = 0;
    for (const FillStitch& s : stitches) {
        jumps += s.jump ? 1 : 0;
    }
    CHECK(jumps <= 2);
}

TEST_CASE("row mask: masked tatami is deterministic", "[row_mask]") {
    const auto region = rect(-5'000, 1'234, 25'000, 18'000);
    auto p = flat_params();
    p.angle = Angle{0.6};
    const DensityRowMask mask([](Vec2um q) { return 0.5 + 0.5 * std::sin(q.x.value / 4'000.0); },
                              DensityRowMask::Slice::Upper);
    CHECK(fill_tatami(region, p, &mask) == fill_tatami(region, p, &mask));
}
