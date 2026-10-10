// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>

#include "openstitch/image/ops.hpp"

using namespace openstitch;
using namespace openstitch::image;

namespace {

// Image 4x4 : moitié gauche rouge opaque, moitié droite bleue semi-transparente.
Image make_test_image() {
    Image img;
    img.width = 4;
    img.height = 4;
    img.source_had_alpha = true;
    img.rgba.resize(4 * 4 * 4);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            std::uint8_t* px = img.rgba.data() + (y * 4 + x) * 4;
            if (x < 2) {
                px[0] = 255;
                px[1] = 0;
                px[2] = 0;
                px[3] = 255;
            } else {
                px[0] = 0;
                px[1] = 0;
                px[2] = 255;
                px[3] = 128;
            }
        }
    }
    return img;
}

const std::uint8_t* pixel(const Image& img, int x, int y) {
    return img.rgba.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width) +
                              static_cast<std::size_t>(x)) *
                                 4;
}

std::size_t distinct_rgb(const Image& img) {
    std::set<std::uint32_t> colors;
    for (std::size_t i = 0; i < img.rgba.size(); i += 4) {
        colors.insert(static_cast<std::uint32_t>(img.rgba[i]) << 16 |
                      static_cast<std::uint32_t>(img.rgba[i + 1]) << 8 | img.rgba[i + 2]);
    }
    return colors.size();
}

} // namespace

TEST_CASE("crop : dimensions et contenu") {
    const auto out = apply_op(make_test_image(), CropOp{2, 0, 2, 4});
    REQUIRE(out.has_value());
    CHECK(out->width == 2);
    CHECK(out->height == 4);
    CHECK(pixel(*out, 0, 0)[2] == 255); // zone bleue
}

TEST_CASE("crop hors de l'image : erreur propre") {
    CHECK_FALSE(apply_op(make_test_image(), CropOp{10, 10, 2, 2}).has_value());
    CHECK_FALSE(apply_op(make_test_image(), CropOp{0, 0, 0, 0}).has_value());
}

TEST_CASE("symetrie horizontale : gauche et droite echangees") {
    const auto out = apply_op(make_test_image(), FlipOp{true});
    REQUIRE(out.has_value());
    CHECK(pixel(*out, 0, 0)[2] == 255); // bleu maintenant a gauche
    CHECK(pixel(*out, 3, 0)[0] == 255); // rouge a droite
}

TEST_CASE("rotation 90 : dimensions echangees") {
    Image img = make_test_image();
    img.width = 4;
    const auto out = apply_op(img, Rotate90Op{1});
    REQUIRE(out.has_value());
    CHECK(out->width == 4);
    CHECK(out->height == 4);
    // 90 deg horaire : la colonne gauche (rouge) devient la ligne du haut.
    CHECK(pixel(*out, 3, 0)[0] == 255);
}

TEST_CASE("niveaux de gris : r=g=b, alpha conserve") {
    const auto out = apply_op(make_test_image(), GrayscaleOp{});
    REQUIRE(out.has_value());
    const auto* px = pixel(*out, 0, 0);
    CHECK(px[0] == px[1]);
    CHECK(px[1] == px[2]);
    CHECK(pixel(*out, 3, 0)[3] == 128); // alpha intact
}

TEST_CASE("luminosite : +100 eclaircit, alpha conserve") {
    const auto out = apply_op(make_test_image(), BrightnessContrastOp{100.0, 0.0});
    REQUIRE(out.has_value());
    CHECK(pixel(*out, 3, 0)[0] > 0);    // canal R du bleu remonte
    CHECK(pixel(*out, 3, 0)[3] == 128); // alpha intact
}

TEST_CASE("quantification : nombre de couleurs reduit, deterministe") {
    // Image 8x8 avec un degrade de 64 couleurs distinctes.
    Image img;
    img.width = 8;
    img.height = 8;
    img.rgba.resize(8 * 8 * 4);
    for (int i = 0; i < 64; ++i) {
        std::uint8_t* px = img.rgba.data() + static_cast<std::size_t>(i) * 4;
        px[0] = static_cast<std::uint8_t>(i * 4);
        px[1] = static_cast<std::uint8_t>(255 - i * 4);
        px[2] = 100;
        px[3] = 255;
    }
    REQUIRE(distinct_rgb(img) == 64);

    const auto a = apply_op(img, QuantizeOp{4});
    REQUIRE(a.has_value());
    CHECK(distinct_rgb(*a) <= 4);

    const auto b = apply_op(img, QuantizeOp{4});
    REQUIRE(b.has_value());
    CHECK(a->rgba == b->rgba); // determinisme (graine RNG fixee)
}

namespace {

// 32x16 : moitié gauche rouge, moitié droite bleue, bruit pseudo-aléatoire
// reproductible de +-8 par canal ; alpha = 100 + x (pour vérifier sa conservation).
Image make_noisy_two_tone() {
    Image img;
    img.width = 32;
    img.height = 16;
    img.source_had_alpha = true;
    img.rgba.resize(32 * 16 * 4);
    std::uint32_t state = 12345;
    const auto noise = [&state] {
        state = state * 1'664'525U + 1'013'904'223U;
        return static_cast<int>((state >> 24) % 17U) - 8;
    };
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 32; ++x) {
            std::uint8_t* px = img.rgba.data() + (y * 32 + x) * 4;
            const bool left = x < 16;
            const int base[3] = {left ? 200 : 40, 40, left ? 40 : 200};
            for (int c = 0; c < 3; ++c) {
                px[c] = static_cast<std::uint8_t>(std::clamp(base[c] + noise(), 0, 255));
            }
            px[3] = static_cast<std::uint8_t>(100 + x);
        }
    }
    return img;
}

double red_spread(const Image& img, int x0, int x1) {
    double sum = 0.0;
    double sum2 = 0.0;
    int n = 0;
    for (int y = 0; y < img.height; ++y) {
        for (int x = x0; x < x1; ++x) {
            const double r = pixel(img, x, y)[0];
            sum += r;
            sum2 += r * r;
            ++n;
        }
    }
    const double mean = sum / n;
    return sum2 / n - mean * mean; // variance
}

} // namespace

TEST_CASE("lissage bilateral : bruit reduit, contour conserve, alpha intact, deterministe") {
    const Image img = make_noisy_two_tone();
    const auto a = apply_op(img, BilateralDenoiseOp{2});
    REQUIRE(a.has_value());
    REQUIRE(a->width == 32);
    REQUIRE(a->height == 16);

    // Zone intérieure gauche (loin du contour) : la variance du rouge baisse nettement.
    CHECK(red_spread(*a, 2, 12) < 0.5 * red_spread(img, 2, 12));
    // Le contour reste net : pas de dégradé de 16 px entre rouge et bleu.
    CHECK(pixel(*a, 14, 8)[0] > 150);
    CHECK(pixel(*a, 14, 8)[2] < 90);
    CHECK(pixel(*a, 17, 8)[2] > 150);
    CHECK(pixel(*a, 17, 8)[0] < 90);
    // Alpha recollé tel quel.
    for (int x = 0; x < 32; ++x) {
        CHECK(pixel(*a, x, 5)[3] == 100 + x);
    }

    const auto b = apply_op(img, BilateralDenoiseOp{2});
    REQUIRE(b.has_value());
    CHECK(a->rgba == b->rgba);
}

TEST_CASE("lissage bilateral : force hors plage refusee, nom lisible") {
    const Image img = make_noisy_two_tone();
    CHECK_FALSE(apply_op(img, BilateralDenoiseOp{0}).has_value());
    CHECK_FALSE(apply_op(img, BilateralDenoiseOp{4}).has_value());
    CHECK(op_name(BilateralDenoiseOp{1}) == "Lissage bilatéral");
}

TEST_CASE("pipeline : l'original n'est jamais modifie") {
    const Image original = make_test_image();
    const std::vector<ImageOp> ops{GrayscaleOp{}, FlipOp{true}, CropOp{0, 0, 2, 2}};
    const auto out = apply_pipeline(original, ops);
    REQUIRE(out.has_value());
    CHECK(out->width == 2);
    CHECK(original.rgba == make_test_image().rgba); // source intacte
}

TEST_CASE("pipeline : une erreur au milieu est propagee") {
    const std::vector<ImageOp> ops{CropOp{0, 0, 2, 2}, CropOp{5, 5, 2, 2}};
    CHECK_FALSE(apply_pipeline(make_test_image(), ops).has_value());
}
