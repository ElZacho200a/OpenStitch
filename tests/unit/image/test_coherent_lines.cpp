// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>

#include "openstitch/image/ops.hpp"

using namespace openstitch;
using namespace openstitch::image;

namespace {

Image make_flat(int w, int h, std::uint8_t v) {
    Image img;
    img.width = w;
    img.height = h;
    img.rgba.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, v);
    for (std::size_t i = 3; i < img.rgba.size(); i += 4) {
        img.rgba[i] = 255;
    }
    return img;
}

void paint(Image& img, int x, int y, std::uint8_t v) {
    std::uint8_t* p = img.rgba.data() + (static_cast<std::size_t>(y) * img.width + x) * 4;
    p[0] = p[1] = p[2] = v;
}

bool is_line(const Image& img, int x, int y) {
    return img.rgba[(static_cast<std::size_t>(y) * img.width + x) * 4] == 0;
}

// Fond blanc, bande sombre horizontale de 5 px au milieu.
Image make_band(int w = 64, int h = 64) {
    Image img = make_flat(w, h, 255);
    for (int y = h / 2 - 2; y <= h / 2 + 2; ++y) {
        for (int x = 0; x < w; ++x) {
            paint(img, x, y, 40);
        }
    }
    return img;
}

std::size_t line_count(const Image& img, int y0, int y1) {
    std::size_t n = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = 0; x < img.width; ++x) {
            n += is_line(img, x, y) ? 1U : 0U;
        }
    }
    return n;
}

} // namespace

TEST_CASE("lignes coherentes : image uniforme -> aucun trait") {
    const auto out = apply_op(make_flat(32, 32, 200), CoherentLinesOp{});
    REQUIRE(out.has_value());
    CHECK(line_count(*out, 0, 32) == 0);
}

TEST_CASE("lignes coherentes : une bande sombre donne des traits sur ses bords seulement") {
    const Image img = make_band();
    const auto out = apply_op(img, CoherentLinesOp{});
    REQUIRE(out.has_value());
    REQUIRE(out->width == 64);
    REQUIRE(out->height == 64);
    // Aucun trait loin de la bande.
    CHECK(line_count(*out, 0, 24) == 0);
    CHECK(line_count(*out, 40, 64) == 0);
    // Des traits dans la zone de la bande, sur toute la largeur (continuite).
    const std::size_t near = line_count(*out, 26, 38);
    CHECK(near >= 64);
    int columns_with_line = 0;
    for (int x = 4; x < 60; ++x) {
        bool any = false;
        for (int y = 26; y < 38; ++y) {
            any = any || is_line(*out, x, y);
        }
        columns_with_line += any ? 1 : 0;
    }
    CHECK(columns_with_line == 56);
}

TEST_CASE("lignes coherentes : le lissage le long du flux ignore le bruit isole") {
    Image img = make_band();
    // Poivre : quelques pixels sombres isoles loin de la bande.
    for (const auto& [x, y] : {std::pair{10, 8}, {40, 12}, {22, 55}, {50, 58}}) {
        paint(img, x, y, 0);
    }
    const auto smooth = apply_op(img, CoherentLinesOp{.sigma_m = 4.0});
    REQUIRE(smooth.has_value());
    const std::size_t noise = line_count(*smooth, 0, 24) + line_count(*smooth, 40, 64);
    // Un point isole ne forme pas de trait : tres peu de pixels residuels.
    CHECK(noise <= 40);
}

TEST_CASE("lignes coherentes : alpha conserve, fond transparent jamais trace") {
    Image img = make_band();
    for (int x = 0; x < 64; ++x) {
        img.rgba[(static_cast<std::size_t>(31) * 64 + x) * 4 + 3] = 0; // rangee transparente
    }
    const auto out = apply_op(img, CoherentLinesOp{});
    REQUIRE(out.has_value());
    for (int x = 0; x < 64; ++x) {
        CHECK(out->rgba[(static_cast<std::size_t>(31) * 64 + x) * 4 + 3] == 0);
        CHECK_FALSE(is_line(*out, x, 31));
    }
}

TEST_CASE("lignes coherentes : deterministe et binaire") {
    const Image img = make_band();
    const auto a = apply_op(img, CoherentLinesOp{});
    const auto b = apply_op(img, CoherentLinesOp{});
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(a->rgba == b->rgba);
    for (std::size_t i = 0; i < a->rgba.size(); i += 4) {
        CHECK((a->rgba[i] == 0 || a->rgba[i] == 255));
        CHECK(a->rgba[i] == a->rgba[i + 1]);
        CHECK(a->rgba[i] == a->rgba[i + 2]);
    }
}

TEST_CASE("lignes coherentes : tau plus bas -> jamais plus de traits (monotone)") {
    const Image img = make_band();
    const auto loose = apply_op(img, CoherentLinesOp{.tau = 0.99});
    const auto tight = apply_op(img, CoherentLinesOp{.tau = 0.90});
    REQUIRE(loose.has_value());
    REQUIRE(tight.has_value());
    CHECK(line_count(*tight, 0, 64) <= line_count(*loose, 0, 64));
}

TEST_CASE("lignes coherentes : parametres invalides refuses, nom lisible") {
    const Image img = make_flat(8, 8, 128);
    CHECK_FALSE(apply_op(img, CoherentLinesOp{.sigma_c = 0.0}).has_value());
    CHECK_FALSE(apply_op(img, CoherentLinesOp{.sigma_m = -1.0}).has_value());
    CHECK_FALSE(apply_op(img, CoherentLinesOp{.rho = 0.5}).has_value());
    CHECK_FALSE(apply_op(img, CoherentLinesOp{.tau = 1.5}).has_value());
    CHECK_FALSE(apply_op(img, CoherentLinesOp{.etf_radius = 0}).has_value());
    CHECK(op_name(CoherentLinesOp{}) == "Lignes cohérentes");
}
