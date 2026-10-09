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

} // namespace

TEST_CASE("en-tete : 512 octets, champs calcules depuis le corps") {
    const auto bytes = encode_dst(simple_square());
    REQUIRE(bytes.has_value());
    REQUIRE(bytes->size() > 512);
    CHECK((bytes->size() - 512) % 3 == 0);

    const std::string header(bytes->begin(), bytes->begin() + 512);
    CHECK(header.substr(0, 3) == "LA:");
    CHECK(header.find("ST:") != std::string::npos);
    CHECK(header.find("CO:  0") != std::string::npos);
    CHECK(header.find("+X:   50") != std::string::npos); // 5 mm = 50 unites
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
        {um(50'000, 0), CommandType::Jump, ObjectId{}}, // 50 mm > 12,1 mm
        {um(50'000, 0), CommandType::Stitch, ObjectId{}},
        {um(50'000, 0), CommandType::End, ObjectId{}},
    };
    const auto bytes = encode_dst(seq);
    REQUIRE(bytes.has_value());
    const auto decoded = decode_dst(*bytes);
    REQUIRE(decoded.has_value());
    const auto stats = stitch::compute_stats(*decoded);
    CHECK(stats.jumps >= 5); // 500 unites / 121 -> au moins 5 sauts
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

TEST_CASE("coupe a sauts non nuls : aucun saut nul, relue comme une coupe, position exacte") {
    // Certaines machines et logiciels de transfert ignorent les sauts de delta nul : la coupe
    // est alors perdue (fils non coupes entre objets). Option machine : triangle de 0,1 mm.
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{1}},
        {um(3'000, 0), CommandType::Stitch, ObjectId{1}},
        {um(3'000, 0), CommandType::Trim, ObjectId{1}},
        {um(8'000, 2'000), CommandType::Jump, ObjectId{2}},
        {um(8'000, 2'000), CommandType::Stitch, ObjectId{2}},
        {um(9'000, 2'000), CommandType::Stitch, ObjectId{2}},
        {um(9'000, 2'000), CommandType::Trim, ObjectId{2}}, // coupe finale
        {um(9'000, 2'000), CommandType::End, ObjectId{}},
    };
    DstWriteOptions options;
    options.trim_jumps_with_movement = true;
    const auto bytes = encode_dst(seq, options);
    REQUIRE(bytes.has_value());

    // Aucun enregistrement « saut de delta nul » (octets 00 00 83) dans le corps.
    bool zeroJump = false;
    for (std::size_t i = 512; i + 2 < bytes->size(); i += 3) {
        zeroJump =
            zeroJump || ((*bytes)[i] == 0x00 && (*bytes)[i + 1] == 0x00 && (*bytes)[i + 2] == 0x83);
    }
    CHECK_FALSE(zeroJump);

    const auto decoded = decode_dst(*bytes);
    REQUIRE(decoded.has_value());
    const auto stats = stitch::compute_stats(*decoded);
    CHECK(stats.trims == 2);
    CHECK(stats.stitches == 4);
    // Le premier point cousu après la coupe est bien à (8 mm, 2 mm).
    bool arrived = false;
    for (const auto& c : decoded->commands) {
        arrived = arrived || (c.type == CommandType::Stitch && c.pos == um(8'000, 2'000));
    }
    CHECK(arrived);
    // Octets historiques conservés par défaut.
    CHECK(encode_dst(seq).value() != *bytes);
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

TEST_CASE("octets en trop apres le marqueur de fin ignores (Hatch: 0x1A)") {
    // Certains logiciels ajoutent un octet de fin DOS (0x1A), voire du padding,
    // apres `00 00 F3`. Le decodage doit reussir et donner le meme resultat.
    const auto clean = *encode_dst(simple_square());
    const auto ref = decode_dst(clean);
    REQUIRE(ref.has_value());

    auto withTail = clean;
    withTail.push_back(0x1A);                      // marqueur EOF DOS
    withTail.insert(withTail.end(), {0x00, 0x7F}); // + reliquat quelconque
    const auto got = decode_dst(withTail);
    REQUIRE(got.has_value());
    CHECK(got->commands == ref->commands);
}

TEST_CASE("sequence vide refusee a l'export") {
    CHECK_FALSE(encode_dst(stitch::StitchSequence{}).has_value());
}

// Lot E (audit marine) : coupes automatiques -- verrou, coupe, long
// déplacement (plusieurs enregistrements), verrou.
TEST_CASE("coupe suivie d'un long deplacement : relue comme une coupe, position exacte") {
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{}},
        {um(2'000, 0), CommandType::Stitch, ObjectId{}},
        {um(2'000, 0), CommandType::Trim, ObjectId{}},
        {um(40'000, 7'000), CommandType::Jump, ObjectId{}}, // > 12,1 mm : découpé
        {um(40'000, 7'000), CommandType::Stitch, ObjectId{}},
        {um(42'000, 7'000), CommandType::Stitch, ObjectId{}},
        {um(42'000, 7'000), CommandType::End, ObjectId{}},
    };
    const auto decoded = decode_dst(*encode_dst(seq));
    REQUIRE(decoded.has_value());
    const auto stats = stitch::compute_stats(*decoded);
    CHECK(stats.trims == 1);
    CHECK(stats.stitches == 4);
    CHECK(decoded->commands[decoded->commands.size() - 2].pos == um(42'000, 7'000));
}

// Un saut plus court que la résolution DST (0,1 mm) s'encode en saut de
// délta nul ; trois à la suite seraient relus comme une COUPE fantôme
// (convention trim_jumps). Ils ne portent aucune information : l'encodeur
// les omet.
TEST_CASE("sauts sous la resolution DST : jamais relus comme une coupe") {
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{}},
        {um(1'000, 0), CommandType::Stitch, ObjectId{}},
        {um(1'020, 0), CommandType::Jump, ObjectId{}},
        {um(1'030, 0), CommandType::Jump, ObjectId{}},
        {um(1'040, 0), CommandType::Jump, ObjectId{}},
        {um(2'000, 0), CommandType::Stitch, ObjectId{}},
        {um(2'000, 0), CommandType::End, ObjectId{}},
    };
    const auto decoded = decode_dst(*encode_dst(seq));
    REQUIRE(decoded.has_value());
    CHECK(stitch::compute_stats(*decoded).trims == 0);
}
