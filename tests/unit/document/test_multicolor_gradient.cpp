// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "openstitch/document/embroidery_object.hpp"

using namespace openstitch;
using namespace openstitch::document;

TEST_CASE("multicolor gradient: method id and parse round trip for every value") {
    for (const auto method : {GradientMethod::Directional, GradientMethod::SatinInterlaced,
                              GradientMethod::LongAndShort, GradientMethod::ControlledScatter,
                              GradientMethod::CrossHatching}) {
        const auto id = gradient_method_id(method);
        CHECK_FALSE(id.empty());
        const auto parsed = parse_gradient_method(id);
        REQUIRE(parsed.has_value());
        CHECK(*parsed == method);
    }
    CHECK_FALSE(parse_gradient_method("inconnu").has_value());
}

TEST_CASE("multicolor gradient: an embroidery object holding it is recognized, others are not") {
    EmbroideryObject anchor;
    anchor.params = MulticolorGradientParams{};
    CHECK(anchor.is_multicolor_gradient());
    CHECK_FALSE(anchor.is_tatami());
    CHECK_FALSE(anchor.is_satin());
    CHECK_FALSE(anchor.is_directional());

    EmbroideryObject tatami;
    tatami.params = TatamiParams{};
    CHECK_FALSE(tatami.is_multicolor_gradient());
}

TEST_CASE("multicolor gradient: default params hold a sane method and no palette") {
    MulticolorGradientParams params;
    CHECK(params.method == GradientMethod::Directional);
    CHECK(params.palette.empty());
    CHECK(params.complementary);
    CHECK(params.crosshatch_layers == 2);
}

TEST_CASE("multicolor gradient: equality follows every field, including the palette") {
    MulticolorGradientParams a;
    a.method = GradientMethod::SatinInterlaced;
    a.palette.push_back(GradientThread{{255, 0, 0}, std::nullopt, DensityCurve::Linear});
    a.palette.push_back(GradientThread{{0, 0, 255}, std::nullopt, DensityCurve::Smooth});

    MulticolorGradientParams b = a;
    CHECK(a == b);

    b.palette[1].curve = DensityCurve::Steep;
    CHECK_FALSE(a == b);

    StitchParams variant_a = a;
    StitchParams variant_b = b;
    CHECK_FALSE(variant_a == variant_b);
}
