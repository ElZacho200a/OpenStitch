// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <set>
#include <string>
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

namespace {

// Image 40 x 20 px, dégradé horizontal kWarm -> kCool, 0,5 mm par pixel (20 x 10 mm).
image::Image gradient_image() {
    image::Image img;
    img.width = 40;
    img.height = 20;
    img.rgba.resize(40 * 20 * 4);
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 40; ++x) {
            const Rgb c = lerp(kWarm, kCool, x / 39.0);
            std::uint8_t* px = img.rgba.data() + (static_cast<std::size_t>(y) * 40 + x) * 4;
            px[0] = c[0];
            px[1] = c[1];
            px[2] = c[2];
            px[3] = 255;
        }
    }
    return img;
}

geometry::PathSet rect_um(int x0, int y0, int x1, int y1) {
    geometry::Path p;
    p.closed = true;
    for (const Vec2um v :
         {Vec2um{Micrometers{x0}, Micrometers{y0}}, Vec2um{Micrometers{x1}, Micrometers{y0}},
          Vec2um{Micrometers{x1}, Micrometers{y1}}, Vec2um{Micrometers{x0}, Micrometers{y1}}}) {
        p.nodes.push_back({v, geometry::NodeType::Corner, {}, {}});
    }
    return geometry::PathSet{p, {}};
}

thread_palette::ThreadChart colour_chart() {
    thread_palette::ThreadChart chart;
    chart.chart_id = "test";
    const std::vector<Rgb> colors{{255, 255, 255}, kWarm, {0, 0, 0}, kCool, {120, 120, 120}};
    for (std::size_t i = 0; i < colors.size(); ++i) {
        thread_palette::Thread t;
        t.key = {"test", std::to_string(i)};
        t.rgb = colors[i];
        chart.threads.push_back(t);
    }
    return chart;
}

} // namespace

TEST_CASE("region blend: samples follow the image under the shape") {
    const auto img = gradient_image();
    // Moitié gauche de l'image : x de -10 mm à 0 (la forme est centrée sur l'image).
    const auto left =
        sample_region_colors(img, Millimeters{0.5}, {rect_um(-10'000, -5'000, 0, 5'000)});
    const auto all =
        sample_region_colors(img, Millimeters{0.5}, {rect_um(-10'000, -5'000, 10'000, 5'000)});
    REQUIRE(!left.empty());
    CHECK(all.size() > left.size());
    CHECK(all.size() <= 20'000);
    // Les pixels de gauche sont plus chauds (rouge élevé) que la moyenne de la forme entière.
    double leftRed = 0.0;
    for (const auto& s : left) {
        leftRed += s.rgb[0];
        CHECK(s.pos.x.value <= 0);
    }
    double allRed = 0.0;
    for (const auto& s : all) {
        allRed += s.rgb[0];
    }
    CHECK(leftRed / static_cast<double>(left.size()) > allRed / static_cast<double>(all.size()));
    // Forme hors de l'image ou image vide : rien.
    CHECK(sample_region_colors(img, Millimeters{0.5}, {rect_um(50'000, 50'000, 60'000, 60'000)})
              .empty());
    CHECK(sample_region_colors({}, Millimeters{0.5}, {rect_um(0, 0, 1'000, 1'000)}).empty());
}

TEST_CASE("region blend: full chain finds two threads and a horizontal density ramp") {
    const auto img = gradient_image();
    const auto chart = colour_chart();
    const auto r = analyze_region_blend(img, Millimeters{0.5},
                                        {rect_um(-10'000, -5'000, 10'000, 5'000)}, &chart);
    REQUIRE(r.has_value());
    CHECK(r->blend.foreground_density.from.y == r->blend.foreground_density.to.y);
    CHECK(std::abs(r->blend.foreground_density.to.x.value -
                   r->blend.foreground_density.from.x.value) > 10'000);
    REQUIRE(r->threads.has_value());
    // Les fils choisis sont le rouge et le bleu du nuancier, dans un ordre ou dans l'autre.
    const std::set<std::string> codes{r->threads->first.code, r->threads->second.code};
    CHECK(codes == std::set<std::string>{"1", "3"});
    // Sans nuancier : pas de fils, mais l'analyse est la même.
    const auto free =
        analyze_region_blend(img, Millimeters{0.5}, {rect_um(-10'000, -5'000, 10'000, 5'000)});
    REQUIRE(free.has_value());
    CHECK_FALSE(free->threads.has_value());
    CHECK(free->blend.background == r->blend.background);
}

TEST_CASE("region blend: uniform or empty regions are refused with a clear error") {
    image::Image flat = gradient_image();
    for (std::size_t i = 0; i < flat.rgba.size(); i += 4) {
        flat.rgba[i] = 90;
        flat.rgba[i + 1] = 90;
        flat.rgba[i + 2] = 90;
    }
    const auto uniform =
        analyze_region_blend(flat, Millimeters{0.5}, {rect_um(-10'000, -5'000, 10'000, 5'000)});
    REQUIRE_FALSE(uniform.has_value());
    CHECK(uniform.error().category == ErrorCategory::UserInput);
    CHECK_FALSE(
        analyze_region_blend({}, Millimeters{0.5}, {rect_um(0, 0, 1'000, 1'000)}).has_value());
    CHECK_FALSE(analyze_region_blend(gradient_image(), Millimeters{0.5},
                                     {rect_um(50'000, 50'000, 60'000, 60'000)})
                    .has_value());
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
