// SPDX-License-Identifier: Apache-2.0
// Couche de traits sombres (HP-AUTO-005) : les traits fins sont séparés des remplissages, qui
// passent dessous sans trou, et cousus en dernier en point droit.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <variant>

#include "openstitch/autodigitize/line_layer.hpp"
#include "openstitch/segmentation/segmentation.hpp"

using namespace openstitch;
using namespace openstitch::autodigitize;

namespace {

void fill_rect(image::Image& img, int x0, int y0, int x1, int y1, std::uint8_t r, std::uint8_t g,
               std::uint8_t b) {
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            std::uint8_t* px = img.rgba.data() + (static_cast<std::size_t>(y) * img.width + x) * 4;
            px[0] = r;
            px[1] = g;
            px[2] = b;
            px[3] = 255;
        }
    }
}

// 120 x 80 : fond beige, moitié gauche rouge, moitié droite bleue, un trait noir horizontal de
// 3 px qui traverse tout, un bloc noir épais de 20 x 20 px.
image::Image scene() {
    image::Image img;
    img.width = 120;
    img.height = 80;
    img.rgba.assign(static_cast<std::size_t>(img.width) * img.height * 4, 0);
    fill_rect(img, 0, 0, 120, 80, 230, 220, 200);
    fill_rect(img, 10, 10, 60, 70, 200, 60, 40);
    fill_rect(img, 60, 10, 110, 70, 30, 80, 170);
    fill_rect(img, 10, 38, 110, 41, 5, 5, 15); // trait
    fill_rect(img, 85, 48, 105, 68, 5, 5, 15); // bloc épais
    return img;
}

AutoOptions options() {
    AutoOptions o;
    o.mm_per_px = Millimeters{0.25};
    o.skip_largest_region = true;
    o.min_fill_area_mm2 = 2.0;
    o.min_region_area_mm2 = 0.0;
    return o;
}

// Les couleurs des régions sont des moyennes : comparaison à une tolérance.
bool near_rgb(std::array<std::uint8_t, 3> a, std::array<std::uint8_t, 3> b) {
    int d = 0;
    for (int k = 0; k < 3; ++k) {
        d += std::abs(static_cast<int>(a[k]) - static_cast<int>(b[k]));
    }
    return d <= 30;
}

std::size_t count_rgb(const AutoResult& r, std::array<std::uint8_t, 3> rgb) {
    return static_cast<std::size_t>(
        std::count_if(r.embroideries.begin(), r.embroideries.end(), [&](const auto& e) {
            return near_rgb(e.rgb, rgb) && std::holds_alternative<document::TatamiParams>(e.params);
        }));
}

} // namespace

TEST_CASE("dark_thin_mask : le trait fin est trouve, le bloc epais non") {
    const auto img = scene();
    std::array<std::uint8_t, 3> mean{};
    const auto mask = segmentation::dark_thin_mask(img, 25.0, 6, &mean);
    REQUIRE(mask.size() == static_cast<std::size_t>(img.width) * img.height);
    const auto at = [&](int x, int y) { return mask[static_cast<std::size_t>(y) * img.width + x]; };
    CHECK(at(30, 39) == 1);  // milieu du trait
    CHECK(at(100, 59) == 0); // coeur du bloc épais
    CHECK(at(30, 20) == 0);  // aplat rouge, pas sombre
    CHECK(mean[0] < 40);
    // Largeur minimale < 2 : sans effet.
    CHECK(segmentation::dark_thin_mask(img, 25.0, 1).empty());
}

TEST_CASE("couche de traits : desactivee = auto_digitize") {
    const auto img = scene();
    const auto seg = segmentation::segment(img, {.max_colors = 4, .min_region_px = 4});
    REQUIRE(seg.has_value());
    IdGenerator<ObjectId> idsA;
    IdGenerator<ObjectId> idsB;
    const auto plain = auto_digitize(*seg, idsA, options());
    LineLayerOptions off; // enabled = false
    const auto layered =
        auto_digitize_with_lines(*seg, img, idsB, options(), off, ContourOptions{});
    REQUIRE(plain.has_value());
    REQUIRE(layered.has_value());
    CHECK(layered->embroideries.size() == plain->embroideries.size());
    CHECK(layered->vectors.size() == plain->vectors.size());
}

TEST_CASE("couche de traits : remplissages sans trou, traits cousus en dernier") {
    const auto img = scene();
    const auto seg = segmentation::segment(img, {.max_colors = 4, .min_region_px = 4});
    REQUIRE(seg.has_value());

    IdGenerator<ObjectId> idsA;
    const auto plain = auto_digitize(*seg, idsA, options());
    REQUIRE(plain.has_value());
    // Sans la couche, le trait noir coupe chaque aplat en deux morceaux.
    const std::array<std::uint8_t, 3> red = {200, 60, 40};
    INFO("plain rouge=" << count_rgb(*plain, red) << " objets=" << plain->embroideries.size());
    CHECK(count_rgb(*plain, red) == 2);

    IdGenerator<ObjectId> ids;
    LineLayerOptions lines;
    lines.enabled = true;
    LineLayerMetrics metrics;
    ContourOptions co;
    co.technique = ContourTechnique::Running;
    const auto result = auto_digitize_with_lines(*seg, img, ids, options(), lines, co, &metrics);
    REQUIRE(result.has_value());
    CHECK(metrics.line_pixels >= 250); // le trait : 100 x 3 px, aux bords près
    CHECK(metrics.line_objects >= 1);
    // Les aplats passent sous le trait : un seul morceau rouge, un seul bleu.
    INFO("layered rouge=" << count_rgb(*result, red)
                          << " bleu=" << count_rgb(*result, {30, 80, 170})
                          << " objets=" << result->embroideries.size());
    CHECK(count_rgb(*result, red) == 1);
    CHECK(count_rgb(*result, {30, 80, 170}) == 1);
    // Les objets de ligne viennent APRÈS tous les remplissages et sont des points droits.
    const std::size_t lineObjects = metrics.line_objects;
    REQUIRE(result->embroideries.size() >= lineObjects);
    const std::size_t firstLine = result->embroideries.size() - lineObjects;
    for (std::size_t i = 0; i < result->embroideries.size(); ++i) {
        const bool isRunning =
            std::holds_alternative<document::RunningStitchParams>(result->embroideries[i].params);
        if (i >= firstLine) {
            CHECK(isRunning);
        }
    }
    CHECK(std::any_of(
        result->embroideries.begin(), result->embroideries.begin() + firstLine,
        [](const auto& e) { return std::holds_alternative<document::TatamiParams>(e.params); }));
    // Le bloc noir épais reste un remplissage, pas un trait.
    CHECK(count_rgb(*result, {5, 5, 15}) >= 1);

    // Déterminisme.
    IdGenerator<ObjectId> ids2;
    const auto again = auto_digitize_with_lines(*seg, img, ids2, options(), lines, co);
    REQUIRE(again.has_value());
    CHECK(again->embroideries.size() == result->embroideries.size());
    CHECK(again->vectors.size() == result->vectors.size());
}
