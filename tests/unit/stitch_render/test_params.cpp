// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <limits>

#include "openstitch/stitch_render/params.hpp"

using namespace openstitch::stitch_render;

TEST_CASE("sanitized clamps intensities and thread width", "[stitch_render][params]") {
    RenderParams p;
    p.thread_width_mm = 12.0;
    p.relief = -3.0;
    p.sheen = 7.0;
    p.twist = std::numeric_limits<double>::quiet_NaN();
    p.shadow = 0.25;
    p.fabric_relief = 2.0;
    const RenderParams s = sanitized(p);
    CHECK(s.thread_width_mm == kMaxThreadWidthMm);
    CHECK(s.relief == 0.0);
    CHECK(s.sheen == 1.0);
    CHECK(s.twist == 0.0);
    CHECK(s.shadow == 0.25);
    CHECK(s.fabric_relief == 1.0);

    p.thread_width_mm = 0.0;
    CHECK(sanitized(p).thread_width_mm == kMinThreadWidthMm);
    p.thread_width_mm = std::numeric_limits<double>::infinity();
    CHECK(sanitized(p).thread_width_mm == RenderParams{}.thread_width_mm);
}

TEST_CASE("sanitized keeps valid parameters unchanged", "[stitch_render][params]") {
    const RenderParams p;
    CHECK(sanitized(p) == p);
}

TEST_CASE("hash_params is stable and sensitive to every field", "[stitch_render][params]") {
    const RenderParams base;
    CHECK(hash_params(base) == hash_params(RenderParams{}));

    RenderParams p = base;
    p.thread_width_mm = 0.4;
    CHECK(hash_params(p) != hash_params(base));
    p = base;
    p.relief = 0.1;
    CHECK(hash_params(p) != hash_params(base));
    p = base;
    p.sheen = 0.1;
    CHECK(hash_params(p) != hash_params(base));
    p = base;
    p.twist = 0.1;
    CHECK(hash_params(p) != hash_params(base));
    p = base;
    p.shadow = 0.1;
    CHECK(hash_params(p) != hash_params(base));
    p = base;
    p.fabric_rgb = {1, 2, 3};
    CHECK(hash_params(p) != hash_params(base));
    p = base;
    p.fabric_opaque = false;
    CHECK(hash_params(p) != hash_params(base));
    p = base;
    p.fabric_texture = FabricTexture::Felt;
    CHECK(hash_params(p) != hash_params(base));
    p = base;
    p.fabric_relief = 0.9;
    CHECK(hash_params(p) != hash_params(base));
    p = base;
    p.quality = Quality::Fast;
    CHECK(hash_params(p) != hash_params(base));
}
