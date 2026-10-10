// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "openstitch/autodigitize/two_color_blend.hpp"
#include "openstitch/stitch_generation/directional_fill.hpp"

using namespace openstitch;
using namespace openstitch::autodigitize;

namespace {

using Rgb = std::array<std::uint8_t, 3>;

constexpr Rgb kWarm{200, 30, 30};
constexpr Rgb kCool{30, 30, 200};

Vec2um at_mm(double x, double y) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(x * 1000.0))},
                  Micrometers{static_cast<std::int32_t>(std::lround(y * 1000.0))}};
}

Rgb lerp(Rgb a, Rgb b, double t) {
    Rgb out{};
    for (std::size_t k = 0; k < 3; ++k) {
        out[k] = static_cast<std::uint8_t>(std::lround(a[k] + (b[k] - a[k]) * t));
    }
    return out;
}

// Grille 21 x 11 pixels sur 20 x 10 mm ; `color(i, j)` donne la couleur du pixel.
template <class F> std::vector<ColorSample> grid(F color) {
    std::vector<ColorSample> samples;
    for (int j = 0; j <= 10; ++j) {
        for (int i = 0; i <= 20; ++i) {
            samples.push_back({at_mm(i, j), color(i, j)});
        }
    }
    return samples;
}

double color_distance(Rgb a, Rgb b) {
    double sum = 0.0;
    for (std::size_t k = 0; k < 3; ++k) {
        const double d = static_cast<double>(a[k]) - static_cast<double>(b[k]);
        sum += d * d;
    }
    return std::sqrt(sum);
}

bool near(Rgb a, Rgb b, double tolerance = 14.0) {
    return color_distance(a, b) <= tolerance;
}

} // namespace

TEST_CASE("blend: a horizontal colour ramp gives two threads and a horizontal density ramp") {
    const auto samples = grid([](int i, int) { return lerp(kWarm, kCool, i / 20.0); });
    const auto blend = analyze_two_color_blend(samples);
    REQUIRE(blend.has_value());

    // Les deux teintes extrêmes sont retrouvées (dans un sens ou dans l'autre).
    const bool direct = near(blend->background, kWarm) && near(blend->foreground, kCool);
    const bool swapped = near(blend->background, kCool) && near(blend->foreground, kWarm);
    CHECK((direct || swapped));

    // L'axe de densité est horizontal et couvre la région.
    const auto& d = blend->foreground_density;
    CHECK(d.from.y == d.to.y);
    CHECK(std::abs(d.to.x.value - d.from.x.value) > 15'000);
    // Densité maximale (écart 0,4 mm) d'un côté, minimale (4 mm) de l'autre.
    CHECK(std::min(d.spacing_from.value, d.spacing_to.value) == 400);
    CHECK(std::max(d.spacing_from.value, d.spacing_to.value) == 4'000);

    // Le côté serré est celui qui ressemble le plus au fil de dessus.
    const Rgb nearFrom = lerp(kWarm, kCool, d.from.x.value < d.to.x.value ? 0.0 : 1.0);
    const bool fromIsForeground =
        color_distance(nearFrom, blend->foreground) < color_distance(nearFrom, blend->background);
    CHECK((d.spacing_from.value < d.spacing_to.value) == fromIsForeground);
}

TEST_CASE("blend: a vertical ramp gives a vertical axis") {
    const auto samples = grid([](int, int j) { return lerp(kWarm, kCool, j / 10.0); });
    const auto blend = analyze_two_color_blend(samples);
    REQUIRE(blend.has_value());
    const auto& d = blend->foreground_density;
    CHECK(d.from.x == d.to.x);
    CHECK(std::abs(d.to.y.value - d.from.y.value) > 7'000);
}

TEST_CASE("blend: interleaved colours give a uniform density") {
    const auto samples =
        grid([](int i, int j) { return (i + j) % 2 == 0 ? kWarm : kCool; }); // damier
    const auto blend = analyze_two_color_blend(samples);
    REQUIRE(blend.has_value());
    const auto& d = blend->foreground_density;
    CHECK(d.from == d.to); // pas de dégradé net : axe nul
    CHECK(d.spacing_from == d.spacing_to);
    // ~ la moitié du fond recouvert : t ~ 0,5, écart ~ b / t = 0,8 mm.
    CHECK(std::abs(blend->mean_t - 0.5) < 0.1);
    CHECK(std::abs(d.spacing_from.value - 800) < 160);
}

TEST_CASE("blend: the dominant colour becomes the background") {
    // 20 % de pixels chauds répartis, 80 % froids.
    const auto samples =
        grid([](int i, int j) { return (i * 7 + j * 13) % 5 == 0 ? kWarm : kCool; });
    const auto blend = analyze_two_color_blend(samples);
    REQUIRE(blend.has_value());
    CHECK(near(blend->background, kCool));
    CHECK(near(blend->foreground, kWarm));
    CHECK(blend->mean_t < 0.35);
    // Peu de dessus : écart large (proche du plafond de 4 mm).
    CHECK(blend->foreground_density.spacing_from.value > 1'500);
}

TEST_CASE("blend: nothing to blend returns nothing") {
    CHECK_FALSE(analyze_two_color_blend({}).has_value());
    CHECK_FALSE(analyze_two_color_blend({{at_mm(0, 0), kWarm}}).has_value());
    const auto flat = grid([](int, int) { return kWarm; });
    CHECK_FALSE(analyze_two_color_blend(flat).has_value());
}

TEST_CASE("blend: same input, same result") {
    const auto samples = grid([](int i, int j) { return lerp(kWarm, kCool, (i + 2 * j) / 40.0); });
    const auto a = analyze_two_color_blend(samples);
    const auto b = analyze_two_color_blend(samples);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(a->background == b->background);
    CHECK(a->foreground == b->foreground);
    CHECK(a->mean_t == b->mean_t);
    CHECK(a->foreground_density == b->foreground_density);
}

TEST_CASE("blend: the density ramp drives the directional fill") {
    // Rampe verticale de couleur + rangées horizontales : le dégradé issu de
    // l'analyse espace les lignes, donc il y en a moins qu'à densité uniforme.
    const auto samples = grid([](int, int j) { return lerp(kWarm, kCool, j / 10.0); });
    const auto blend = analyze_two_color_blend(samples);
    REQUIRE(blend.has_value());

    geometry::Path square;
    square.closed = true;
    for (const Vec2um p : {at_mm(0, 0), at_mm(20, 0), at_mm(20, 10), at_mm(0, 10)}) {
        square.nodes.push_back({p, geometry::NodeType::Corner, {}, {}});
    }
    geometry::Path guide;
    guide.closed = false;
    for (const Vec2um p : {at_mm(0, 5), at_mm(20, 5)}) {
        guide.nodes.push_back({p, geometry::NodeType::Corner, {}, {}});
    }
    const geometry::PathSet region{square, {}};

    document::DirectionalFillParams uniform;
    uniform.row_spacing = Micrometers{400};
    uniform.inset = Micrometers{0};
    uniform.guides.push_back(guide);
    auto graded = uniform;
    graded.density_gradient = blend->foreground_density;

    const auto dense = stitch_generation::trace_directional_streamlines(region, uniform);
    const auto sparse = stitch_generation::trace_directional_streamlines(region, graded);
    REQUIRE(!dense.empty());
    REQUIRE(!sparse.empty());
    CHECK(sparse.size() < dense.size());
}
