// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "openstitch/formats/dst.hpp"

using namespace openstitch;
using namespace openstitch::formats;
using stitch::CommandType;

namespace {

Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

stitch::StitchSequence simple_square() {
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), CommandType::Jump, ObjectId{1}},
        {um(0, 0), CommandType::Stitch, ObjectId{1}},
        {um(5'000, 0), CommandType::Stitch, ObjectId{1}},
        {um(5'000, 5'000), CommandType::Stitch, ObjectId{1}},
        {um(0, 5'000), CommandType::Stitch, ObjectId{1}},
        {um(0, 0), CommandType::Stitch, ObjectId{1}},
        {um(0, 0), CommandType::End, ObjectId{}},
    };
    return seq;
}

// Types et positions (les ObjectId ne survivent pas au DST, c'est documenté).
void check_same_shape(const stitch::StitchSequence& a, const stitch::StitchSequence& b) {
    REQUIRE(a.commands.size() == b.commands.size());
    for (std::size_t i = 0; i < a.commands.size(); ++i) {
        CHECK(a.commands[i].type == b.commands[i].type);
        CHECK(a.commands[i].pos == b.commands[i].pos);
    }
}

}  // namespace

TEST_CASE("en-tete : 512 octets, champs calcules depuis le corps") {
    const auto bytes = encode_dst(simple_square());
    REQUIRE(bytes.has_value());
    REQUIRE(bytes->size() > 512);
    CHECK((bytes->size() - 512) % 3 == 0);

    const std::string header(bytes->begin(), bytes->begin() + 512);
    CHECK(header.substr(0, 3) == "LA:");
    CHECK(header.find("ST:") != std::string::npos);
    CHECK(header.find("CO:  0") != std::string::npos);
    CHECK(header.find("+X:   50") != std::string::npos);  // 5 mm = 50 unites
    CHECK(header.find("+Y:   50") != std::string::npos);
    CHECK(header.find("-X:    0") != std::string::npos);
}

TEST_CASE("aller-retour exact d'un carre") {
    const auto bytes = encode_dst(simple_square());
    REQUIRE(bytes.has_value());
    const auto decoded = decode_dst(*bytes);
    REQUIRE(decoded.has_value());
    // Les positions du carre sont multiples de 0,1 mm : aller-retour exact.
    check_same_shape(simple_square(), *decoded);
}

TEST_CASE("aller-retour : tous les deltas de -121 a +121") {
    stitch::StitchSequence seq;
    std::int32_t x = 0;
    seq.commands.push_back({um(0, 0), CommandType::Stitch, ObjectId{}});
    for (int d = -121; d <= 121; ++d) {
        x += d * 100;
        seq.commands.push_back({um(x, -x), CommandType::Stitch, ObjectId{}});
    }
    seq.commands.push_back({um(x, -x), CommandType::End, ObjectId{}});

    const auto bytes = encode_dst(seq);
    REQUIRE(bytes.has_value());
    const auto decoded = decode_dst(*bytes);
    REQUIRE(decoded.has_value());
    check_same_shape(seq, *decoded);
}

TEST_CASE("quantification sans derive cumulative") {
    // 1000 points espaces de 0,149 mm : chaque pas arrondi varie mais la
    // position absolue reste a moins de 50 um de la verite.
    stitch::StitchSequence seq;
    for (int i = 0; i < 1000; ++i) {
        seq.commands.push_back({um(i * 149, 0), CommandType::Stitch, ObjectId{}});
    }
    seq.commands.push_back({seq.commands.back().pos, CommandType::End, ObjectId{}});

    const auto decoded = decode_dst(*encode_dst(seq));
    REQUIRE(decoded.has_value());
    const auto& last = decoded->commands[decoded->commands.size() - 2];
    CHECK(std::abs(last.pos.x.value - 999 * 149) <= 50);
}

TEST_CASE("grand deplacement subdivise en sauts") {
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{}},
        {um(50'000, 0), CommandType::Jump, ObjectId{}},  // 50 mm > 12,1 mm
        {um(50'000, 0), CommandType::Stitch, ObjectId{}},
        {um(50'000, 0), CommandType::End, ObjectId{}},
    };
    const auto bytes = encode_dst(seq);
    REQUIRE(bytes.has_value());
    const auto decoded = decode_dst(*bytes);
    REQUIRE(decoded.has_value());
    const auto stats = stitch::compute_stats(*decoded);
    CHECK(stats.jumps >= 5);  // 500 unites / 121 -> au moins 5 sauts
    // Position finale exacte malgre la subdivision.
    CHECK(decoded->commands[decoded->commands.size() - 2].pos == um(50'000, 0));
}

TEST_CASE("trim et changement de couleur : aller-retour") {
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{}},
        {um(3'000, 0), CommandType::Stitch, ObjectId{}},
        {um(3'000, 0), CommandType::Trim, ObjectId{}},
        {um(3'000, 0), CommandType::ColorChange, ObjectId{}},
        {um(6'000, 0), CommandType::Stitch, ObjectId{}},
        {um(6'000, 0), CommandType::End, ObjectId{}},
    };
    const auto decoded = decode_dst(*encode_dst(seq));
    REQUIRE(decoded.has_value());
    const auto stats = stitch::compute_stats(*decoded);
    CHECK(stats.trims == 1);
    CHECK(stats.color_changes == 1);
    CHECK(stats.stitches == 3);
}

TEST_CASE("determinisme : memes octets a chaque encodage") {
    const auto a = encode_dst(simple_square());
    const auto b = encode_dst(simple_square());
    REQUIRE((a.has_value() && b.has_value()));
    CHECK(*a == *b);
}

TEST_CASE("fichiers invalides refuses proprement, sans crash") {
    CHECK_FALSE(decode_dst({}).has_value());

    std::vector<std::uint8_t> tooShort(100, 0x20);
    CHECK_FALSE(decode_dst(tooShort).has_value());

    // En-tete valide mais corps tronque (pas multiple de 3).
    auto bytes = *encode_dst(simple_square());
    bytes.pop_back();
    const auto r = decode_dst(bytes);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().category == ErrorCategory::InvalidFile);

    // Corps sans marqueur de fin.
    auto noEnd = *encode_dst(simple_square());
    noEnd.resize(noEnd.size() - 3);
    CHECK_FALSE(decode_dst(noEnd).has_value());
}

TEST_CASE("sequence vide refusee a l'export") {
    CHECK_FALSE(encode_dst(stitch::StitchSequence{}).has_value());
}
