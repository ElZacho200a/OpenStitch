// SPDX-License-Identifier: Apache-2.0
// HP-FMT-009 / HP-FMT-005 : fichier compagnon de couleurs (.inf / .csv). 100 % synthétique.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "openstitch/formats/color_companion.hpp"

using namespace openstitch;
using namespace openstitch::formats;

namespace {

std::vector<ThreadColor> sample() {
    return {{255, 0, 0}, {0, 128, 255}, {7, 8, 9}};
}

std::filesystem::path scratch_dir(const std::string& name) {
    auto dir = std::filesystem::temp_directory_path() / ("openstitch_companion_" + name);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

} // namespace

TEST_CASE("companion : chemin derive du fichier de motif") {
    CHECK(companion_path("dir/logo.dst", CompanionKind::Inf) ==
          std::filesystem::path("dir/logo.inf"));
    CHECK(companion_path("dir/logo.exp", CompanionKind::Csv) ==
          std::filesystem::path("dir/logo.csv"));
}

TEST_CASE("companion : octets exacts du .inf") {
    const std::vector<ThreadColor> one{{1, 2, 3}};
    const auto bytes = encode_color_companion(one, CompanionKind::Inf);
    REQUIRE(bytes.has_value());
    const std::vector<std::uint8_t> expected = {
        0,   0,   0,   1,                    // version
        0,   0,   0,   1,                    // nombre de fils
        0,   18,                             // longueur de l'enregistrement (10 + "Color 1\0")
        0,   0,                              // reserve
        1,   2,   3,   0,                    // RVB + 0
        0,   1,                              // index (base 1)
        'C', 'o', 'l', 'o', 'r', ' ', '1', 0 // nom
    };
    CHECK(*bytes == expected);
}

TEST_CASE("companion : texte exact du .csv") {
    const auto bytes = encode_color_companion(sample(), CompanionKind::Csv);
    REQUIRE(bytes.has_value());
    const std::string text(bytes->begin(), bytes->end());
    CHECK(text == "index,r,g,b,hex\n1,255,0,0,#FF0000\n2,0,128,255,#0080FF\n3,7,8,9,#070809\n");
}

TEST_CASE("companion : aller-retour inf et csv") {
    for (const auto kind : {CompanionKind::Inf, CompanionKind::Csv}) {
        const auto bytes = encode_color_companion(sample(), kind);
        REQUIRE(bytes.has_value());
        const auto back = decode_color_companion(*bytes);
        REQUIRE(back.has_value());
        CHECK(*back == sample());
    }
}

TEST_CASE("companion : determinisme des octets") {
    for (const auto kind : {CompanionKind::Inf, CompanionKind::Csv}) {
        const auto a = encode_color_companion(sample(), kind);
        const auto b = encode_color_companion(sample(), kind);
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        CHECK(*a == *b);
    }
}

TEST_CASE("companion : csv tolerant (CRLF, lignes vides, sans en-tete)") {
    const std::string text = "1, 10, 20, 30,x\r\n\r\n2,40,50,60\r\n";
    const auto back = decode_color_companion(
        std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
    REQUIRE(back.has_value());
    CHECK(*back == std::vector<ThreadColor>{{10, 20, 30}, {40, 50, 60}});
}

TEST_CASE("companion : contenus invalides refuses sans planter") {
    CHECK_FALSE(encode_color_companion({}, CompanionKind::Inf).has_value());
    CHECK_FALSE(decode_color_companion({}).has_value());
    const std::string bad = "index,r,g,b,hex\n1,300,0,0,#x\n";
    CHECK_FALSE(decode_color_companion(
                    std::span(reinterpret_cast<const std::uint8_t*>(bad.data()), bad.size()))
                    .has_value());
    const std::vector<std::uint8_t> huge = {0, 0, 0, 1, 0xFF, 0xFF, 0xFF, 0xFF};
    CHECK_FALSE(decode_color_companion(huge).has_value());
    const std::vector<std::uint8_t> truncated = {0, 0, 0, 1, 0, 0, 0, 2, 0, 12, 0, 0, 1, 2, 3};
    CHECK_FALSE(decode_color_companion(truncated).has_value());
}

TEST_CASE("companion : ecriture puis relecture a cote du motif") {
    const auto dir = scratch_dir("rw");
    const auto dst = dir / "motif.dst";

    const auto none = read_color_companion_for(dst);
    REQUIRE(none.has_value());
    CHECK_FALSE(none->has_value());

    REQUIRE(write_color_companion_file(dst, sample(), CompanionKind::Csv).has_value());
    CHECK(std::filesystem::exists(dir / "motif.csv"));
    auto found = read_color_companion_for(dst);
    REQUIRE(found.has_value());
    REQUIRE(found->has_value());
    CHECK(**found == sample());

    // Le .inf a priorite sur le .csv quand les deux existent.
    const std::vector<ThreadColor> other{{9, 9, 9}};
    REQUIRE(write_color_companion_file(dst, other, CompanionKind::Inf).has_value());
    found = read_color_companion_for(dst);
    REQUIRE(found.has_value());
    REQUIRE(found->has_value());
    CHECK(**found == other);

    // Un compagnon present mais corrompu est une erreur, pas une absence.
    { std::ofstream(dir / "motif.inf", std::ios::binary | std::ios::trunc) << '\0' << "junk"; }
    CHECK_FALSE(read_color_companion_for(dst).has_value());
    std::filesystem::remove_all(dir);
}