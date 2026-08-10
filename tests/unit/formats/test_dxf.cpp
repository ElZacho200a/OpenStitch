// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>

#include "openstitch/formats/dxf.hpp"
#include "openstitch/geometry/primitives.hpp"

using namespace openstitch;
using namespace openstitch::formats;
using openstitch::geometry::NodeType;
using openstitch::geometry::Path;
using openstitch::geometry::PathNode;

namespace {

std::vector<std::uint8_t> to_bytes(const std::string& s) {
    return std::vector<std::uint8_t>(s.begin(), s.end());
}

Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

PathNode corner(Vec2um pos) {
    return PathNode{pos, NodeType::Corner, std::nullopt, std::nullopt};
}

}  // namespace

TEST_CASE("dxf : aucune section ENTITIES -> erreur propre") {
    const auto result = decode_dxf(to_bytes(
        "0\r\nSECTION\r\n2\r\nHEADER\r\n0\r\nENDSEC\r\n0\r\nEOF\r\n"));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().category == ErrorCategory::InvalidFile);
}

TEST_CASE("dxf : ENTITIES sans entite prise en charge -> erreur propre") {
    const auto result = decode_dxf(to_bytes(
        "0\r\nSECTION\r\n2\r\nENTITIES\r\n"
        "0\r\nTEXT\r\n1\r\nBonjour\r\n"
        "0\r\nENDSEC\r\n0\r\nEOF\r\n"));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().category == ErrorCategory::UnsupportedFormat);
}

TEST_CASE("dxf : entite non prise en charge ignoree sans faire echouer le fichier") {
    const auto result = decode_dxf(to_bytes(
        "0\r\nSECTION\r\n2\r\nENTITIES\r\n"
        "0\r\nTEXT\r\n1\r\nBonjour\r\n"
        "0\r\nLINE\r\n10\r\n0.0\r\n20\r\n0.0\r\n11\r\n10.0\r\n21\r\n0.0\r\n"
        "0\r\nENDSEC\r\n0\r\nEOF\r\n"));
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    CHECK((*result)[0].nodes.size() == 2);
}

TEST_CASE("dxf : LINE devient un chemin ouvert a deux noeuds") {
    const auto result = decode_dxf(to_bytes(
        "0\r\nSECTION\r\n2\r\nENTITIES\r\n"
        "0\r\nLINE\r\n10\r\n1.0\r\n20\r\n2.0\r\n11\r\n5.0\r\n21\r\n2.0\r\n"
        "0\r\nENDSEC\r\n0\r\nEOF\r\n"));
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    const Path& path = (*result)[0];
    CHECK_FALSE(path.closed);
    REQUIRE(path.nodes.size() == 2);
    CHECK(path.nodes[0].pos == um(1'000, 2'000));
    CHECK(path.nodes[1].pos == um(5'000, 2'000));
}

TEST_CASE("dxf : CIRCLE devient un chemin ferme a quatre noeuds") {
    const auto result = decode_dxf(to_bytes(
        "0\r\nSECTION\r\n2\r\nENTITIES\r\n"
        "0\r\nCIRCLE\r\n10\r\n3.0\r\n20\r\n4.0\r\n40\r\n2.0\r\n"
        "0\r\nENDSEC\r\n0\r\nEOF\r\n"));
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    const Path& path = (*result)[0];
    CHECK(path.closed);
    REQUIRE(path.nodes.size() == 4);
    // Sommet est du cercle : centre (3,4) mm + rayon 2 mm -> (5,4) mm.
    CHECK(path.nodes[0].pos == um(5'000, 4'000));
}

TEST_CASE("dxf : ARC echantillonne un quart de cercle en polyligne ouverte") {
    const auto result = decode_dxf(to_bytes(
        "0\r\nSECTION\r\n2\r\nENTITIES\r\n"
        "0\r\nARC\r\n10\r\n0.0\r\n20\r\n0.0\r\n40\r\n10.0\r\n50\r\n0.0\r\n51\r\n90.0\r\n"
        "0\r\nENDSEC\r\n0\r\nEOF\r\n"));
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    const Path& path = (*result)[0];
    CHECK_FALSE(path.closed);
    REQUIRE(path.nodes.size() >= 3);
    // Premier point a 0 deg (10,0), dernier a 90 deg (0,10), rayon 10 mm.
    CHECK(path.nodes.front().pos == um(10'000, 0));
    CHECK(std::abs(path.nodes.back().pos.x.value - 0) <= 2);
    CHECK(std::abs(path.nodes.back().pos.y.value - 10'000) <= 2);
    // Tout point intermediaire reste a ~10 mm du centre (0,0).
    for (const auto& n : path.nodes) {
        const double dx = static_cast<double>(n.pos.x.value);
        const double dy = static_cast<double>(n.pos.y.value);
        const double r = std::sqrt(dx * dx + dy * dy) / 1000.0;
        CHECK(std::abs(r - 10.0) < 0.01);
    }
}

TEST_CASE("dxf : LWPOLYLINE fermee sans bulge reproduit les sommets exacts") {
    const auto result = decode_dxf(to_bytes(
        "0\r\nSECTION\r\n2\r\nENTITIES\r\n"
        "0\r\nLWPOLYLINE\r\n90\r\n4\r\n70\r\n1\r\n"
        "10\r\n0.0\r\n20\r\n0.0\r\n"
        "10\r\n10.0\r\n20\r\n0.0\r\n"
        "10\r\n10.0\r\n20\r\n10.0\r\n"
        "10\r\n0.0\r\n20\r\n10.0\r\n"
        "0\r\nENDSEC\r\n0\r\nEOF\r\n"));
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    const Path& path = (*result)[0];
    CHECK(path.closed);
    REQUIRE(path.nodes.size() == 4);
    CHECK(path.nodes[0].pos == um(0, 0));
    CHECK(path.nodes[1].pos == um(10'000, 0));
    CHECK(path.nodes[2].pos == um(10'000, 10'000));
    CHECK(path.nodes[3].pos == um(0, 10'000));
}

TEST_CASE("dxf : bulge produit un arc dont le centre et le sommet sont geometriquement corrects") {
    // Segment de (0,0) a (10,0) mm, bulge = tan(22,5 deg) : arc d'un quart de
    // cercle (angle inclus 90 deg). Centre et sommet attendus verifies a la
    // main par rotation complexe exacte (pas juste par construction du code
    // teste) : centre (5,5), rayon 5*sqrt(2), sommet vers (5, 5-5*sqrt(2)).
    // Un demi-cercle (bulge=1) ne permettrait PAS de distinguer un bug de
    // signe ici : le centre tombe alors exactement sur la corde quel que
    // soit le sens choisi (defaut trouve en verifiant ce test a la main).
    const double bulge = std::tan(22.5 * std::numbers::pi / 180.0);
    const auto result = decode_dxf(to_bytes(
        "0\r\nSECTION\r\n2\r\nENTITIES\r\n"
        "0\r\nLWPOLYLINE\r\n90\r\n2\r\n70\r\n0\r\n"
        "10\r\n0.0\r\n20\r\n0.0\r\n42\r\n" + std::to_string(bulge) + "\r\n"
        "10\r\n10.0\r\n20\r\n0.0\r\n"
        "0\r\nENDSEC\r\n0\r\nEOF\r\n"));
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    const Path& path = (*result)[0];
    CHECK_FALSE(path.closed);
    REQUIRE(path.nodes.size() > 2);  // sommets d'origine + points d'arc intercales

    const double expectedRadius = 5.0 * std::sqrt(2.0);
    const double centerX = 5.0;
    const double centerY = 5.0;
    for (const auto& n : path.nodes) {
        const double dx = static_cast<double>(n.pos.x.value) / 1000.0 - centerX;
        const double dy = static_cast<double>(n.pos.y.value) / 1000.0 - centerY;
        const double r = std::sqrt(dx * dx + dy * dy);
        CHECK(std::abs(r - expectedRadius) < 0.02);
    }
    // Le sommet (point le plus eloigne de la corde) doit etre du cote y < 0.
    const auto& apex = path.nodes[path.nodes.size() / 2];
    CHECK(static_cast<double>(apex.pos.y.value) / 1000.0 < -1.0);
}

TEST_CASE("dxf : export sans aucun trace -> erreur propre") {
    const auto result = encode_dxf({});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().category == ErrorCategory::UserInput);
}

TEST_CASE("dxf : aller-retour d'un rectangle (noeuds Coin, sans courbe)") {
    Path rect;
    rect.closed = true;
    rect.nodes = {corner(um(0, 0)), corner(um(10'000, 0)), corner(um(10'000, 5'000)),
                 corner(um(0, 5'000))};

    const auto bytes = encode_dxf({rect});
    REQUIRE(bytes.has_value());

    const auto decoded = decode_dxf(*bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == 1);
    const Path& back = (*decoded)[0];
    CHECK(back.closed);
    REQUIRE(back.nodes.size() == 4);
    for (std::size_t i = 0; i < 4; ++i) {
        // Aller-retour via texte (%.6f) : tolerance de 1 um, largement sous
        // toute precision utile en broderie.
        CHECK(std::abs(back.nodes[i].pos.x.value - rect.nodes[i].pos.x.value) <= 1);
        CHECK(std::abs(back.nodes[i].pos.y.value - rect.nodes[i].pos.y.value) <= 1);
    }
}

TEST_CASE("dxf : aller-retour d'une ellipse (noeuds Lisses) aplatit en polyligne dense") {
    const Path ellipse = geometry::ellipse_path(um(0, 0), um(20'000, 10'000));
    const auto bytes = encode_dxf({ellipse});
    REQUIRE(bytes.has_value());

    const auto decoded = decode_dxf(*bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == 1);
    const Path& back = (*decoded)[0];
    CHECK(back.closed);
    // Aplatie en polyligne dense : bien plus de 4 noeuds, tous en Coin (DXF
    // n'a pas de courbe de Bezier native pour un LWPOLYLINE).
    CHECK(back.nodes.size() > 4);
    for (const auto& n : back.nodes) {
        CHECK(n.type == NodeType::Corner);
    }
}
