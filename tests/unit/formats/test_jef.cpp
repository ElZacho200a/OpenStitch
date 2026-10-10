// SPDX-License-Identifier: Apache-2.0
// HP-FMT-004 : codec JEF (Janome). Fichiers de référence 100 % synthétiques.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "machine_test_helpers.hpp"
#include "openstitch/formats/jef.hpp"

using namespace openstitch;
using namespace openstitch::formats;
using namespace machine_test;

namespace {

constexpr std::array<std::uint8_t, 3> kRed{255, 0, 0};    // entrée JEF 10
constexpr std::array<std::uint8_t, 3> kBlue{11, 47, 132}; // entrée JEF 12

MachineExportOptions two_colors() {
    MachineExportOptions o;
    o.block_colors = {kRed, kBlue, kBlue};
    return o;
}

std::uint32_t u32(const std::vector<std::uint8_t>& b, std::size_t at) {
    return static_cast<std::uint32_t>(b[at]) | (static_cast<std::uint32_t>(b[at + 1]) << 8) |
           (static_cast<std::uint32_t>(b[at + 2]) << 16) |
           (static_cast<std::uint32_t>(b[at + 3]) << 24);
}

} // namespace

TEST_CASE("jef : octets exacts d'un motif minimal (en-tete, table de fils, flux, fin)") {
    const auto seq = make({
        {um(0, 0), CommandType::Stitch},
        {um(1'000, 0), CommandType::Stitch},
        {um(1'000, 0), CommandType::End},
    });
    const auto bytes = encode_jef(seq);
    REQUIRE(bytes.has_value());
    REQUIRE(bytes->size() == 0x74 + 8 + 10);
    CHECK(u32(*bytes, 0) == 0x74 + 8); // decalage des points
    CHECK(u32(*bytes, 4) == 0x14);
    CHECK(std::string(bytes->begin() + 8, bytes->begin() + 22) == "20000101000000");
    CHECK(u32(*bytes, 24) == 1);   // couleurs
    CHECK(u32(*bytes, 28) == 5);   // paires d'octets du flux
    CHECK(u32(*bytes, 32) == 1);   // cadre 50 x 50
    CHECK(u32(*bytes, 36) == 5);   // demi-largeur
    CHECK(u32(*bytes, 40) == 0);   // demi-hauteur
    CHECK(u32(*bytes, 0x74) == 1); // premier fil : noir = entree 1
    CHECK(u32(*bytes, 0x78) == 0x0D);
    const std::vector<std::uint8_t> stream(bytes->begin() + 0x7C, bytes->end());
    const std::vector<std::uint8_t> expected = {
        0x80, 0x02, 0xFB, 0x00, // positionnement depuis le centre : -0,5 mm
        0x00, 0x00,             // premier point
        0x0A, 0x00,             // +1 mm
        0x80, 0x10,             // fin
    };
    CHECK(stream == expected);
}

TEST_CASE("jef : aller-retour exact (types, positions, couleurs, coupe = 3 sauts nuls, arret)") {
    const auto seq = rich_short();
    const auto bytes = encode_jef(seq, two_colors());
    REQUIRE(bytes.has_value());
    const auto decoded = decode_jef(*bytes);
    REQUIRE(decoded.has_value());
    check_same_shape(rebased(seq), decoded->sequence);
    REQUIRE(decoded->block_colors.size() == 3);
    CHECK(decoded->block_colors[0] == kRed);
    CHECK(decoded->block_colors[1] == kBlue);
    CHECK(decoded->block_colors[2] == kBlue); // arret : meme fil
    CHECK(decoded->sequence.commands[6].type == CommandType::Trim);
    CHECK(decoded->sequence.commands[13].type == CommandType::Stop);

    // Table : 3 entrees, la derniere = 0 (arret).
    CHECK(u32(*bytes, 24) == 3);
    CHECK(u32(*bytes, 0x74) == 10);
    CHECK(u32(*bytes, 0x78) == 12);
    CHECK(u32(*bytes, 0x7C) == 0);
}

TEST_CASE("jef : deterministe, idempotent apres un tour, grands sauts decoupes a 12,7 mm") {
    const auto seq = rich_long_jump();
    MachineExportOptions options;
    options.block_colors = {kRed, kBlue};
    const auto first = encode_jef(seq, options);
    const auto again = encode_jef(seq, options);
    REQUIRE(first.has_value());
    CHECK(*first == *again);

    const auto decoded = decode_jef(*first);
    REQUIRE(decoded.has_value());
    CHECK(decoded->sequence.commands.size() > seq.commands.size()); // saut de 55 mm decoupe
    options.block_colors = decoded->block_colors;
    const auto second = encode_jef(decoded->sequence, options);
    REQUIRE(second.has_value());
    CHECK(*second == *first);

    // Position finale exacte malgre le decoupage.
    const auto& last = decoded->sequence.commands[decoded->sequence.commands.size() - 2];
    CHECK(last.pos == um(59'000, 20'000));
}

TEST_CASE(
    "jef : options machine : coupes natives ou supprimees, arrets en changements de couleur") {
    const auto seq = rich_short();
    MachineExportOptions noTrims = two_colors();
    noTrims.trims = TrimMode::Drop;
    const auto a = decode_jef(*encode_jef(seq, noTrims));
    REQUIRE(a.has_value());
    for (const auto& c : a->sequence.commands) {
        CHECK(c.type != CommandType::Trim);
    }

    MachineExportOptions stopAsChange = two_colors();
    stopAsChange.stops = StopMode::AsColorChange;
    stopAsChange.block_colors = {kRed, kBlue, kRed};
    const auto bytes = encode_jef(seq, stopAsChange);
    REQUIRE(bytes.has_value());
    CHECK(u32(*bytes, 0x74 + 8) != 0); // plus d'entree 0
    const auto b = decode_jef(*bytes);
    REQUIRE(b.has_value());
    for (const auto& c : b->sequence.commands) {
        CHECK(c.type != CommandType::Stop);
    }

    MachineExportOptions mono = two_colors();
    mono.color_changes = ColorChangeMode::Drop;
    mono.stops = StopMode::Drop;
    const auto d = decode_jef(*encode_jef(seq, mono));
    REQUIRE(d.has_value());
    CHECK(d->block_colors.size() == 1);
}

TEST_CASE("jef : cadre selon la taille et marges, code -1 si le motif depasse le cadre") {
    StitchSequence big;
    big.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{}},
        {um(60'000, 0), CommandType::Stitch, ObjectId{}},
        {um(60'000, 60'000), CommandType::Stitch, ObjectId{}},
        {um(60'000, 60'000), CommandType::End, ObjectId{}},
    };
    const auto bytes = encode_jef(big);
    REQUIRE(bytes.has_value());
    CHECK(u32(*bytes, 32) == 3); // 126 x 110 (600 < 1260, 600 < 1100)
    // Marges du cadre 110 x 110 : 550 - 300 = 250 partout.
    CHECK(u32(*bytes, 52) == 250);
    // Cadre 50 x 50 : motif plus grand -> -1.
    CHECK(static_cast<std::int32_t>(u32(*bytes, 68)) == -1);
}

TEST_CASE("jef : decodage tolerant -- fichiers tronques ou corrompus ne plantent jamais") {
    const auto bytes = *encode_jef(rich_short(), two_colors());
    for (std::size_t n = 0; n < bytes.size(); n += 3) {
        (void)decode_jef(std::vector<std::uint8_t>(bytes.begin(),
                                                   bytes.begin() + static_cast<std::ptrdiff_t>(n)));
    }
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        auto copy = bytes;
        copy[i] = static_cast<std::uint8_t>(copy[i] ^ 0x5A);
        (void)decode_jef(copy);
    }
    for (std::uint32_t seed = 1; seed <= 50; ++seed) {
        (void)decode_jef(lcg_bytes(600, seed));
    }
    const auto tiny = decode_jef(std::vector<std::uint8_t>{1, 2, 3});
    REQUIRE_FALSE(tiny.has_value());
    CHECK(tiny.error().category == ErrorCategory::InvalidFile);
}
