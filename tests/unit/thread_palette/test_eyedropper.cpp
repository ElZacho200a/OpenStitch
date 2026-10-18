// SPDX-License-Identifier: Apache-2.0
// HP-THR-012 : coeur de la pipette (echantillon de couleur -> fil le plus proche).
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "openstitch/thread_palette/eyedropper.hpp"

using namespace openstitch;
using namespace openstitch::thread_palette;

namespace {

// Image 3x2 : ligne 0 = rouge, vert, bleu ; ligne 1 = blanc, noir, gris.
const std::vector<std::uint8_t>& sample_pixels() {
    static const std::vector<std::uint8_t> px = {
        200, 16,  46,  0, 154, 68, 0,   114, 206, // ligne 0
        255, 255, 255, 0, 0,   0,  128, 128, 128, // ligne 1
    };
    return px;
}

RgbImageView view() {
    return RgbImageView{sample_pixels(), 3, 2};
}

} // namespace

TEST_CASE("pipette : un pixel exact avec radius 0") {
    const auto c = sample_color(view(), 1, 0);
    REQUIRE(c.has_value());
    CHECK(*c == std::array<std::uint8_t, 3>{0, 154, 68});
    const auto w = sample_color(view(), 0, 1);
    REQUIRE(w.has_value());
    CHECK(*w == std::array<std::uint8_t, 3>{255, 255, 255});
}

TEST_CASE("pipette : moyenne arrondie et rognee aux bords") {
    // Rayon 1 autour de (1,1) : couvre toute l'image (6 pixels, bords rognes).
    const auto c = sample_color(view(), 1, 1, 1);
    REQUIRE(c.has_value());
    // R : (200+0+0+255+0+128)/6 = 97,17 -> 97 ; G : (16+154+114+255+0+128)/6 = 111,17 -> 111
    // B : (46+68+206+255+0+128)/6 = 117,17 -> 117
    CHECK(*c == std::array<std::uint8_t, 3>{97, 111, 117});

    // Coin (0,0) rayon 1 : pixels (0,0),(1,0),(0,1),(1,1) seulement.
    const auto corner = sample_color(view(), 0, 0, 1);
    REQUIRE(corner.has_value());
    // R : (200+0+255+0)/4 = 113,75 -> 114
    CHECK((*corner)[0] == 114);
}

TEST_CASE("pipette : arrondi au plus proche sur .5") {
    const std::vector<std::uint8_t> px = {0, 0, 0, 1, 1, 1}; // moyenne 0,5 -> 1
    const auto c = sample_region(RgbImageView{px, 2, 1}, 0, 0, 2, 1);
    REQUIRE(c.has_value());
    CHECK(*c == std::array<std::uint8_t, 3>{1, 1, 1});
}

TEST_CASE("pipette : sample_region rogne le rectangle et refuse le vide") {
    const auto row = sample_region(view(), 0, 1, 10, 10); // rogne a la ligne 1
    REQUIRE(row.has_value());
    CHECK(*row == std::array<std::uint8_t, 3>{128, 128, 128}); // (255+0+128)/3 = 127,67 -> 128
    CHECK_FALSE(sample_region(view(), 5, 5, 2, 2).has_value());
    CHECK_FALSE(sample_region(view(), 0, 0, 0, 2).has_value());
    CHECK_FALSE(sample_region(view(), -3, -3, 2, 2).has_value());
}

TEST_CASE("pipette : points hors image et vue incoherente refuses") {
    CHECK_FALSE(sample_color(view(), 3, 0).has_value());
    CHECK_FALSE(sample_color(view(), 0, 2).has_value());
    CHECK_FALSE(sample_color(view(), -1, 0).has_value());
    CHECK_FALSE(sample_color(view(), 0, 0, -1).has_value());
    const std::vector<std::uint8_t> shortData(5, 0);
    CHECK_FALSE(sample_color(RgbImageView{shortData, 3, 2}, 0, 0).has_value());
    CHECK_FALSE(sample_color(RgbImageView{{}, 0, 0}, 0, 0).has_value());
    const auto err = sample_color(view(), 9, 9);
    REQUIRE_FALSE(err.has_value());
    CHECK(err.error().category == ErrorCategory::UserInput);
}

TEST_CASE("pipette : pick_thread propose le fil le plus proche du catalogue") {
    const auto lib = ThreadLibrary::with_builtin();
    // Pixel (0,0) = 200,16,46 = exactement G06 Rouge du nuancier generique.
    const auto r = pick_thread(view(), 0, 0, 0, lib, "generic", nullptr, 3);
    REQUIRE(r.has_value());
    CHECK(r->sampled == std::array<std::uint8_t, 3>{200, 16, 46});
    REQUIRE(r->matches.size() == 3);
    CHECK(r->matches[0].key.code == "G06");
    CHECK(r->matches[0].distance < 1e-6);
    CHECK(r->matches[0].distance <= r->matches[1].distance);
}

TEST_CASE("pipette : pick_thread restreint a Mes fils quand un stock est fourni") {
    const auto lib = ThreadLibrary::with_builtin();
    ThreadStock stock;
    REQUIRE(stock.own({"generic", "G01"})); // blanc
    REQUIRE(stock.own({"generic", "G21"})); // bleu marine
    // Pixel (0,0) rouge : G06 existe au catalogue mais n'est pas possede.
    const auto r = pick_thread(view(), 0, 0, 0, lib, "generic", &stock, 5);
    REQUIRE(r.has_value());
    REQUIRE(r->matches.size() == 2);
    for (const auto& m : r->matches) {
        CHECK(stock.owns(m.key));
    }
    // Deux appels identiques -> meme resultat.
    const auto again = pick_thread(view(), 0, 0, 0, lib, "generic", &stock, 5);
    REQUIRE(again.has_value());
    CHECK(again->sampled == r->sampled);
    REQUIRE(again->matches.size() == r->matches.size());
    for (std::size_t i = 0; i < r->matches.size(); ++i) {
        CHECK(again->matches[i].key == r->matches[i].key);
        CHECK(again->matches[i].distance == r->matches[i].distance);
    }
}

TEST_CASE("pipette : pick_thread propage l'erreur d'echantillonnage") {
    const auto lib = ThreadLibrary::with_builtin();
    CHECK_FALSE(pick_thread(view(), 7, 7, 0, lib, "", nullptr, 3).has_value());
}
