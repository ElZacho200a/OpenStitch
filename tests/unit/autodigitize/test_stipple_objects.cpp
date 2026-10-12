// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "openstitch/autodigitize/stipple_objects.hpp"

using namespace openstitch;
using namespace openstitch::autodigitize;

namespace {

// 120x80 : noir a gauche, blanc a droite.
image::Image half_dark_image() {
    image::Image img;
    img.width = 120;
    img.height = 80;
    img.rgba.assign(static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height) * 4,
                    255);
    for (int y = 0; y < img.height; ++y) {
        for (int x = 0; x < 60; ++x) {
            std::uint8_t* p = img.rgba.data() + (static_cast<std::size_t>(y) * 120 + x) * 4;
            p[0] = p[1] = p[2] = 0;
        }
    }
    return img;
}

} // namespace

TEST_CASE("stipple objets : un trait ouvert Running, points a gauche, deterministe") {
    const image::Image img = half_dark_image();
    StippleObjectOptions opt;
    opt.stipple.point_count = 600;
    opt.stipple.iterations = 10;

    IdGenerator<ObjectId> ids1;
    StippleMetrics m;
    const auto a = auto_digitize_stipple(img, ids1, opt, &m);
    REQUIRE(a.has_value());
    REQUIRE(a->vectors.size() == 1);
    REQUIRE(a->embroideries.size() == 1);
    CHECK(std::holds_alternative<document::RunningStitchParams>(a->embroideries[0].params));
    CHECK(a->embroideries[0].source_vector == a->vectors[0].id);
    CHECK(m.points > 500);
    CHECK(m.tour_length_mm <= m.initial_tour_length_mm);

    std::size_t left = 0;
    std::size_t total = 0;
    for (const auto& ps : a->vectors[0].paths) {
        CHECK_FALSE(ps.outer.closed);
        for (const auto& n : ps.outer.nodes) {
            ++total;
            left += n.pos.x.value < 0 ? 1 : 0; // l'origine est le centre de l'image
        }
    }
    CHECK(left > total * 9 / 10);

    IdGenerator<ObjectId> ids2;
    const auto b = auto_digitize_stipple(img, ids2, opt);
    REQUIRE(b.has_value());
    CHECK(a->vectors[0].paths == b->vectors[0].paths);
}

TEST_CASE("stipple objets : image blanche refusee proprement") {
    image::Image img;
    img.width = 40;
    img.height = 40;
    img.rgba.assign(40 * 40 * 4, 255);
    IdGenerator<ObjectId> ids;
    CHECK_FALSE(auto_digitize_stipple(img, ids, {}).has_value());
}
