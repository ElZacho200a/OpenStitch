// SPDX-License-Identifier: Apache-2.0
// HP-FMT-006 : codec VP3 (Husqvarna / Pfaff). Fichiers de reference 100 % synthetiques.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "machine_test_helpers.hpp"
#include "openstitch/formats/vp3.hpp"

using namespace openstitch;
using namespace openstitch::formats;
using namespace machine_test;

namespace {

constexpr std::array<std::uint8_t, 3> kRed{255, 0, 0};
constexpr std::array<std::uint8_t, 3> kBlue{11, 47, 132};

MachineExportOptions two_colors() {
    MachineExportOptions o;
    o.block_colors = {kRed, kBlue, kBlue};
    return o;
}

} // namespace

TEST_CASE("vp3 : octets exacts d'un motif minimal (marqueur, en-tete, segment, flux)") {
    const auto seq = make({
        {um(0, 0), CommandType::Stitch},
        {um(1'000, 0), CommandType::Stitch},
        {um(1'000, 0), CommandType::End},
    });
    const auto bytes = encode_vp3(seq);
    REQUIRE(bytes.has_value());
    const std::vector<std::uint8_t> expected = {
        0x25, 0x76, 0x73, 0x6D, 0x25, 0x00,                                 // %vsm% + NUL
        0x00, 0x0A, 'O',  'p',  'e',  'n',  'S',  't',  'i', 't', 'c', 'h', // producteur
        0x00, 0x02, 0x00,                                                   // version
        0x00, 0x00, 0x00, 0x3E,                                             // reste
        0x00, 0x0A, 'O',  'P',  'E',  'N',  'S',  'T',  'I', 'T', 'C', 'H', // nom motif
        0xFF, 0xFF, 0xFF, 0xCE,                                             // min x
        0x00, 0x00, 0x00, 0x00,                                             // min y
        0x00, 0x00, 0x00, 0x32,                                             // max x
        0x00, 0x00, 0x00, 0x00,                                             // max y
        0x00, 0x01,                                                         // 1 segment
        0x00, 0x00, 0x00, 0x1C,                                             // taille segment
        0xFF, 0xFF, 0xFF, 0xCE, 0x00, 0x00, 0x00, 0x00,                     // depart
        0x00, 0x00, 0x00,                                                   // fil noir
        0x00, 0x07, '#',  '0',  '0',  '0',  '0',  '0',  '0',                // nom du fil
        0x00, 0x00, 0x00, 0x04,                                             // flux
        0x00, 0x00, 0x64, 0x00,                                             // +1 mm
    };
    CHECK(*bytes == expected);
}

TEST_CASE("vp3 : point long (> 1,27 mm) en code 0x80 0x01 et saut en 0x80 0x02") {
    const auto seq = make({
        {um(0, 0), CommandType::Stitch},
        {um(60'000, 0), CommandType::Stitch},
        {um(60'000, -2'000), CommandType::Jump},
        {um(60'000, -2'000), CommandType::End},
    });
    const auto bytes = encode_vp3(seq);
    REQUIRE(bytes.has_value());
    const std::vector<std::uint8_t> tail = {0x00, 0x00, 0x80, 0x01, 0x17, 0x70, 0x00,
                                            0x00, 0x80, 0x02, 0x00, 0x00, 0xFF, 0x38};
    REQUIRE(bytes->size() > tail.size());
    CHECK(std::equal(tail.begin(), tail.end(),
                     bytes->end() - static_cast<std::ptrdiff_t>(tail.size())));
    const auto decoded = decode_vp3(*bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->sequence.commands.size() == seq.commands.size());
    CHECK(decoded->sequence.commands[1].pos - decoded->sequence.commands[0].pos == um(60'000, 0));
}

TEST_CASE("vp3 : aller-retour (types, couleurs, coupe, arret)") {
    const auto seq = rich_short();
    const auto bytes = encode_vp3(seq, two_colors());
    REQUIRE(bytes.has_value());
    const auto decoded = decode_vp3(*bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->name == "OPENSTITCH");
    // VP3 ne distingue pas arret et changement de couleur : l'arret revient en ColorChange.
    REQUIRE(decoded->block_colors.size() == 3);
    CHECK(decoded->block_colors[0] == kRed);
    CHECK(decoded->block_colors[1] == kBlue);
    CHECK(decoded->block_colors[2] == kBlue);
    REQUIRE(decoded->sequence.commands.size() == seq.commands.size());
    CHECK(decoded->sequence.commands[6].type == CommandType::Trim);
    CHECK(decoded->sequence.commands[13].type == CommandType::ColorChange);
    CHECK(decoded->sequence.commands[14].pos - decoded->sequence.commands[0].pos ==
          um(7'000, 2'000));
}

TEST_CASE("vp3 : deterministe et idempotent apres un tour, grand saut conserve") {
    const auto seq = rich_long_jump();
    MachineExportOptions options;
    options.block_colors = {kRed, kBlue};
    const auto first = encode_vp3(seq, options);
    const auto again = encode_vp3(seq, options);
    REQUIRE(first.has_value());
    CHECK(*first == *again);
    const auto decoded = decode_vp3(*first);
    REQUIRE(decoded.has_value());
    options.block_colors = decoded->block_colors;
    options.design_name = decoded->name;
    const auto second = encode_vp3(decoded->sequence, options);
    REQUIRE(second.has_value());
    CHECK(*second == *first);
}

TEST_CASE("vp3 : decodage tolerant -- fichiers tronques ou corrompus ne plantent jamais") {
    const auto bytes = *encode_vp3(rich_short(), two_colors());
    for (std::size_t n = 0; n < bytes.size(); ++n) {
        (void)decode_vp3(std::vector<std::uint8_t>(bytes.begin(),
                                                   bytes.begin() + static_cast<std::ptrdiff_t>(n)));
    }
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        auto copy = bytes;
        copy[i] = static_cast<std::uint8_t>(copy[i] ^ 0x5A);
        (void)decode_vp3(copy);
    }
    for (std::uint32_t seed = 1; seed <= 50; ++seed) {
        (void)decode_vp3(lcg_bytes(600, seed));
    }
    const auto tiny = decode_vp3(std::vector<std::uint8_t>{1, 2, 3});
    REQUIRE_FALSE(tiny.has_value());
    CHECK(tiny.error().category == ErrorCategory::InvalidFile);
}
