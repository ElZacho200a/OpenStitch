// SPDX-License-Identifier: Apache-2.0
// HP-FMT-005 : codec EXP (Melco / Bernina). Fichiers de référence 100 % synthétiques.
#include <catch2/catch_test_macros.hpp>

#include "machine_test_helpers.hpp"
#include "openstitch/formats/exp.hpp"

using namespace openstitch;
using namespace openstitch::formats;
using namespace machine_test;

TEST_CASE("exp : octets exacts (axe Y vers le haut, saut, coupe, changement de couleur)") {
    const auto seq = make({
        {um(0, 0), CommandType::Stitch},
        {um(1'000, 0), CommandType::Stitch},
        {um(1'000, 5'000), CommandType::Stitch},
        {um(1'000, 5'000), CommandType::Trim},
        {um(-1'000, 5'000), CommandType::Jump},
        {um(-1'000, 5'000), CommandType::ColorChange},
        {um(-1'000, 5'000), CommandType::End},
    });
    const auto bytes = encode_exp(seq);
    REQUIRE(bytes.has_value());
    const std::vector<std::uint8_t> expected = {
        0x00, 0x00,             // (0,0)
        0x0A, 0x00,             // +1 mm
        0x00, 0x32,             // +5 mm vers le haut : dy = +50, sans inversion
        0x80, 0x80, 0x07, 0x00, // coupe
        0x80, 0x04, 0xEC, 0x00, // saut de -20 unites (0xEC)
        0x80, 0x01, 0x00, 0x00, // changement de couleur
    };
    CHECK(*bytes == expected);
}

TEST_CASE("exp : aller-retour exact (l'arret devient un changement de couleur)") {
    const auto seq = rich_short();
    const auto bytes = encode_exp(seq);
    REQUIRE(bytes.has_value());
    const auto decoded = decode_exp(*bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->block_colors.empty()); // EXP ne porte aucune couleur

    auto expected = rebased(seq);
    for (auto& c : expected.commands) {
        if (c.type == CommandType::Stop) {
            c.type = CommandType::ColorChange;
        }
    }
    check_same_shape(expected, decoded->sequence);
}

TEST_CASE("exp : deterministe, grands sauts decoupes a 12,7 mm, idempotent apres un tour") {
    const auto seq = rich_long_jump();
    const auto first = encode_exp(seq);
    REQUIRE(first.has_value());
    CHECK(*first == *encode_exp(seq));
    const auto decoded = decode_exp(*first);
    REQUIRE(decoded.has_value());
    CHECK(decoded->sequence.commands.size() > seq.commands.size());
    const auto second = encode_exp(decoded->sequence);
    REQUIRE(second.has_value());
    CHECK(*second == *first);
    const auto& last = decoded->sequence.commands[decoded->sequence.commands.size() - 2];
    CHECK(last.pos == um(59'000, 20'000));
}

TEST_CASE("exp : tous les deltas de -127 a +127 font l'aller-retour") {
    StitchSequence seq;
    std::int32_t x = 0;
    std::int32_t y = 0;
    seq.commands.push_back({um(0, 0), CommandType::Stitch, ObjectId{}});
    for (int d = -127; d <= 127; ++d) {
        x += d * 100;
        y -= (d / 2) * 100;
        seq.commands.push_back({um(x, y), CommandType::Stitch, ObjectId{}});
    }
    seq.commands.push_back({um(x, y), CommandType::End, ObjectId{}});
    const auto decoded = decode_exp(*encode_exp(seq));
    REQUIRE(decoded.has_value());
    check_same_shape(rebased(seq), decoded->sequence);
}

TEST_CASE("exp : options machine : coupes supprimees, changements de couleur supprimes") {
    MachineExportOptions options;
    options.trims = TrimMode::Drop;
    options.color_changes = ColorChangeMode::Drop;
    options.stops = StopMode::Drop;
    const auto decoded = decode_exp(*encode_exp(rich_short(), options));
    REQUIRE(decoded.has_value());
    for (const auto& c : decoded->sequence.commands) {
        CHECK(c.type != CommandType::Trim);
        CHECK(c.type != CommandType::ColorChange);
        CHECK(c.type != CommandType::Stop);
    }
}

TEST_CASE("exp : decodage tolerant -- fichiers tronques ou corrompus ne plantent jamais") {
    const auto bytes = *encode_exp(rich_short());
    for (std::size_t n = 0; n <= bytes.size(); ++n) {
        (void)decode_exp(std::vector<std::uint8_t>(bytes.begin(),
                                                   bytes.begin() + static_cast<std::ptrdiff_t>(n)));
    }
    for (std::uint32_t seed = 1; seed <= 100; ++seed) {
        (void)decode_exp(lcg_bytes(400, seed));
    }
    const auto empty = decode_exp(std::vector<std::uint8_t>{});
    REQUIRE_FALSE(empty.has_value());
    CHECK(empty.error().category == ErrorCategory::InvalidFile);
}
