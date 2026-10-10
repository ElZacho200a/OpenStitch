// SPDX-License-Identifier: Apache-2.0
// HP-THR-011 : « limiter à N fils » dans l'auto-numérisation.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <set>
#include <vector>

#include "openstitch/autodigitize/autodigitize.hpp"

using namespace openstitch;
using namespace openstitch::autodigitize;

namespace {

AutoOptions opts() {
    AutoOptions o;
    o.mm_per_px = Millimeters{1.0};
    o.min_fill_area_mm2 = 20.0;
    return o;
}

std::size_t distinct_colors(const AutoResult& r) {
    std::set<std::array<std::uint8_t, 3>> s;
    for (const auto& e : r.embroideries) {
        s.insert(e.rgb);
    }
    return s.size();
}

} // namespace

TEST_CASE("max_threads fusionne les couleurs proches et reste deterministe") {
    // Deux rouges proches et deux bleus proches, cote a cote.
    image::Image img;
    img.width = 64;
    img.height = 16;
    img.rgba.assign(64 * 16 * 4, 0);
    const std::array<std::array<std::uint8_t, 3>, 4> colors = {
        {{220, 30, 30}, {210, 50, 40}, {30, 30, 220}, {40, 50, 210}}};
    for (std::size_t block = 0; block < 4; ++block) {
        for (std::size_t y = 1; y < 15; ++y) {
            for (std::size_t x = block * 16 + 1; x < block * 16 + 15; ++x) {
                std::uint8_t* px = img.rgba.data() + (y * 64 + x) * 4;
                px[0] = colors[block][0];
                px[1] = colors[block][1];
                px[2] = colors[block][2];
                px[3] = 255;
            }
        }
    }
    const auto seg = segmentation::segment(img, {.max_colors = 4, .min_region_px = 1});
    REQUIRE(seg.has_value());

    IdGenerator<ObjectId> ids;
    const auto unlimited = auto_digitize(*seg, ids, opts());
    REQUIRE(unlimited.has_value());
    CHECK(distinct_colors(*unlimited) == 4);

    AutoOptions limited = opts();
    limited.max_threads = 2;
    IdGenerator<ObjectId> ids2;
    const auto two = auto_digitize(*seg, ids2, limited);
    REQUIRE(two.has_value());
    CHECK(two->embroideries.size() == unlimited->embroideries.size()); // geometrie intacte
    CHECK(distinct_colors(*two) == 2);
    // Les couleurs retenues sont des couleurs de la segmentation d'origine.
    for (const auto& e : two->embroideries) {
        bool found = false;
        for (const auto& slot : seg->region_slots) {
            found = found || (slot && slot->rgb == e.rgb);
        }
        CHECK(found);
    }

    IdGenerator<ObjectId> ids3;
    const auto again = auto_digitize(*seg, ids3, limited);
    REQUIRE(again.has_value());
    REQUIRE(again->embroideries.size() == two->embroideries.size());
    for (std::size_t i = 0; i < again->embroideries.size(); ++i) {
        CHECK(again->embroideries[i].rgb == two->embroideries[i].rgb);
    }
}
