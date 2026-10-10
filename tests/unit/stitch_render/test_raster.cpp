// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <vector>

#include "openstitch/stitch_render/raster.hpp"

using namespace openstitch::stitch_render;

namespace {

struct Px {
    int r, g, b, a;
};

Px pixel(const RasterImage& img, int x, int y) {
    const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width) +
                           static_cast<std::size_t>(x)) *
                          4;
    return {img.rgba[i], img.rgba[i + 1], img.rgba[i + 2], img.rgba[i + 3]};
}

int luma(const Px& p) {
    return (p.r * 3 + p.g * 6 + p.b) / 10;
}

// Fenêtre 40 x 40 mm à 10 px/mm (400 x 400 px) ; un fil horizontal en y = 20.
RasterView view400() {
    return plan_view(RectMm{0, 0, 40, 40}, 10.0, 10'000'000);
}

std::vector<ThreadSegment> horizontal_thread() {
    return {ThreadSegment{5.0F, 20.0F, 35.0F, 20.0F, {200, 30, 30}}};
}

RenderParams plain() {
    RenderParams p;
    p.fabric_texture = FabricTexture::Plain;
    p.thread_width_mm = 0.6;
    return p;
}

} // namespace

TEST_CASE("choose_detail falls back to lines when the thread is sub-pixel", "[stitch_render]") {
    RenderParams p;
    p.thread_width_mm = 0.35;
    CHECK(choose_detail(2.0, p) == Detail::Lines);
    CHECK(choose_detail(10.0, p) == Detail::Realistic);
    // Seuil exact : 0,35 mm * ppm >= 1,6 px.
    CHECK(choose_detail(1.6 / 0.35 + 0.01, p) == Detail::Realistic);
    CHECK(choose_detail(1.6 / 0.35 - 0.1, p) == Detail::Lines);
}

TEST_CASE("plan_view caps memory and keeps pixel/mm mapping exact", "[stitch_render]") {
    const RasterView v = plan_view(RectMm{0, 0, 100, 50}, 8.0, 10'000'000);
    CHECK(v.width == 800);
    CHECK(v.height == 400);
    CHECK(v.px_per_mm == 8.0);

    const RasterView capped = plan_view(RectMm{0, 0, 1000, 1000}, 20.0, 1'000'000);
    CHECK(static_cast<std::size_t>(capped.width) * static_cast<std::size_t>(capped.height) <=
          1'000'000U);
    CHECK(capped.px_per_mm < 20.0);
    CHECK(std::abs(capped.rect.width() * capped.px_per_mm - capped.width) < 1e-6);

    CHECK(plan_view(RectMm{}, 10.0, 1000).width == 0);
    CHECK(plan_view(RectMm{0, 0, 1, 1}, 0.0, 1000).width == 0);
}

TEST_CASE("opaque fabric fills the whole image", "[stitch_render]") {
    RenderParams p = plain();
    p.fabric_rgb = {10, 120, 240};
    const RasterImage img = render_threads({}, p, view400());
    REQUIRE(img.width == 400);
    REQUIRE(img.height == 400);
    const Px px = pixel(img, 200, 200);
    CHECK(px.r == 10);
    CHECK(px.g == 120);
    CHECK(px.b == 240);
    CHECK(px.a == 255);
}

TEST_CASE("transparent fabric leaves untouched pixels empty", "[stitch_render]") {
    RenderParams p = plain();
    p.fabric_opaque = false;
    const RasterImage img = render_threads(horizontal_thread(), p, view400());
    CHECK(pixel(img, 5, 5).a == 0);
    CHECK(pixel(img, 200, 200).a == 255); // centre du fil : opaque
}

TEST_CASE("empty or invalid view yields an empty image", "[stitch_render]") {
    CHECK(render_threads({}, RenderParams{}, RasterView{}).rgba.empty());
}

TEST_CASE("thread covers about its width and shows a cylindrical profile", "[stitch_render]") {
    const RenderParams p = plain();
    const RasterImage img = render_threads(horizontal_thread(), p, view400());
    // Largeur 0,6 mm = 6 px : le centre est le fil, 8 px plus loin c'est le tissu.
    const Px centre = pixel(img, 200, 200);
    const Px far = pixel(img, 200, 215);
    CHECK(centre.r > centre.g + 60); // rouge dominant
    CHECK(far.r == p.fabric_rgb[0]);
    CHECK(far.g == p.fabric_rgb[1]);

    // Éclairage depuis le haut : le haut du fil est plus clair que le bas.
    const Px top = pixel(img, 200, 198);
    const Px bottom = pixel(img, 200, 202);
    CHECK(luma(top) != luma(bottom));
}

TEST_CASE("relief zero gives a flat thread, relief one a shaded one", "[stitch_render]") {
    RenderParams flat = plain();
    flat.relief = 0.0;
    flat.sheen = 0.0;
    flat.twist = 0.0;
    flat.shadow = 0.0;
    RenderParams shaded = flat;
    shaded.relief = 1.0;

    const auto spread = [](const RasterImage& img) {
        int lo = 255;
        int hi = 0;
        for (int y = 198; y <= 201; ++y) {
            const int l = luma(pixel(img, 200, y));
            lo = std::min(lo, l);
            hi = std::max(hi, l);
        }
        return hi - lo;
    };
    const int flat_spread = spread(render_threads(horizontal_thread(), flat, view400()));
    const int shaded_spread = spread(render_threads(horizontal_thread(), shaded, view400()));
    CHECK(shaded_spread > flat_spread + 20);
}

TEST_CASE("shadow darkens fabric beside the thread", "[stitch_render]") {
    RenderParams none = plain();
    none.shadow = 0.0;
    RenderParams strong = plain();
    strong.shadow = 1.0;
    // Ombre vers le bas-droite : juste sous le fil.
    const auto below = [](const RasterImage& img) { return luma(pixel(img, 200, 205)); };
    const int without = below(render_threads(horizontal_thread(), none, view400()));
    const int with = below(render_threads(horizontal_thread(), strong, view400()));
    CHECK(with < without - 10);
    // Au-dessus (côté lumière) rien ne change.
    const auto above = [](const RasterImage& img) { return luma(pixel(img, 200, 190)); };
    CHECK(above(render_threads(horizontal_thread(), strong, view400())) ==
          above(render_threads(horizontal_thread(), none, view400())));
}

TEST_CASE("later segments are stacked over earlier ones", "[stitch_render]") {
    RenderParams p = plain();
    p.shadow = 0.0;
    std::vector<ThreadSegment> segs{
        ThreadSegment{5.0F, 20.0F, 35.0F, 20.0F, {250, 0, 0}},
        ThreadSegment{20.0F, 5.0F, 20.0F, 35.0F, {0, 0, 250}},
    };
    const RasterImage img = render_threads(segs, p, view400());
    const Px crossing = pixel(img, 200, 200);
    CHECK(crossing.b > crossing.r);
    std::swap(segs[0], segs[1]);
    const RasterImage swapped = render_threads(segs, p, view400());
    const Px crossing2 = pixel(swapped, 200, 200);
    CHECK(crossing2.r > crossing2.b);
}

TEST_CASE("rendering is deterministic", "[stitch_render]") {
    std::vector<ThreadSegment> segs;
    for (int i = 0; i < 300; ++i) {
        const float f = static_cast<float>(i);
        segs.push_back(ThreadSegment{2.0F + std::fmod(f * 1.7F, 36.0F),
                                     2.0F + std::fmod(f * 2.9F, 36.0F),
                                     2.0F + std::fmod(f * 3.1F, 36.0F),
                                     2.0F + std::fmod(f * 0.7F, 36.0F),
                                     {static_cast<std::uint8_t>(i * 7), 90, 160}});
    }
    RenderParams p;
    const RasterImage a = render_threads(segs, p, view400());
    const RasterImage b = render_threads(segs, p, view400());
    CHECK(a.rgba == b.rgba);
    p.quality = Quality::Fast;
    const RasterImage c = render_threads(segs, p, view400());
    const RasterImage d = render_threads(segs, p, view400());
    CHECK(c.rgba == d.rgba);
    CHECK(a.rgba != c.rgba);
}

TEST_CASE("segments outside the view are culled without effect", "[stitch_render]") {
    const RenderParams p = plain();
    std::vector<ThreadSegment> segs = horizontal_thread();
    const RasterImage base = render_threads(segs, p, view400());
    segs.push_back(ThreadSegment{500.0F, 500.0F, 600.0F, 600.0F, {0, 255, 0}});
    segs.push_back(ThreadSegment{-90.0F, -90.0F, -50.0F, -50.0F, {0, 255, 0}});
    CHECK(render_threads(segs, p, view400()).rgba == base.rgba);
}

TEST_CASE("weave and felt fabrics add deterministic texture", "[stitch_render]") {
    RenderParams plainP = plain();
    RenderParams weave = plain();
    weave.fabric_texture = FabricTexture::Weave;
    weave.fabric_relief = 1.0;
    RenderParams felt = weave;
    felt.fabric_texture = FabricTexture::Felt;

    const auto distinct = [](const RasterImage& img) {
        int lo = 255;
        int hi = 0;
        for (int x = 0; x < 100; ++x) {
            const int l = luma(pixel(img, x, 10));
            lo = std::min(lo, l);
            hi = std::max(hi, l);
        }
        return hi - lo;
    };
    CHECK(distinct(render_threads({}, plainP, view400())) == 0);
    CHECK(distinct(render_threads({}, weave, view400())) > 5);
    CHECK(distinct(render_threads({}, felt, view400())) > 3);
    CHECK(render_threads({}, weave, view400()).rgba == render_threads({}, weave, view400()).rgba);
}

TEST_CASE("large render with many segments completes and is deterministic across runs",
          "[stitch_render][perf]") {
    std::vector<ThreadSegment> segs;
    segs.reserve(50'000);
    for (int i = 0; i < 50'000; ++i) {
        const float x = static_cast<float>((i * 37) % 3000) * 0.05F;
        const float y = static_cast<float>((i * 91) % 2000) * 0.05F;
        segs.push_back(
            ThreadSegment{x, y, x + 2.5F, y + 0.4F, {static_cast<std::uint8_t>(i), 80, 200}});
    }
    const RasterView view = plan_view(RectMm{0, 0, 160, 110}, 8.0, 4'000'000);
    const RenderParams p;
    const RasterImage a = render_threads(segs, p, view);
    CHECK(a.width == view.width);
    CHECK(a.rgba == render_threads(segs, p, view).rgba);
}
