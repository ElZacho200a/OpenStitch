// SPDX-License-Identifier: Apache-2.0
// HP-FMT-002 / HP-FMT-003 : codec PES. Fichiers de référence 100 % synthétiques (générés ici).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

#include "machine_test_helpers.hpp"
#include "openstitch/formats/pes.hpp"

using namespace openstitch;
using namespace openstitch::formats;
using namespace machine_test;

namespace {

constexpr std::array<std::uint8_t, 3> kRed{237, 23, 31};  // entrée PEC 5
constexpr std::array<std::uint8_t, 3> kBlue{10, 85, 163}; // entrée PEC 2

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

TEST_CASE("pes : en-tete PES, bloc PEC, table des fils et vignettes coherents") {
    const auto bytes = encode_pes(rich_short(), two_colors());
    REQUIRE(bytes.has_value());
    REQUIRE(bytes->size() > 600);
    CHECK(std::string(bytes->begin(), bytes->begin() + 8) == "#PES0001");

    const std::size_t pec = u32(*bytes, 8);
    REQUIRE(pec + 512 + 16 < bytes->size());
    CHECK(std::string(bytes->begin() + static_cast<std::ptrdiff_t>(pec),
                      bytes->begin() + static_cast<std::ptrdiff_t>(pec) + 3) == "LA:");
    // Nom sur 16 caracteres puis CR.
    CHECK(std::string(bytes->begin() + static_cast<std::ptrdiff_t>(pec) + 3,
                      bytes->begin() + static_cast<std::ptrdiff_t>(pec) + 13) == "OPENSTITCH");
    CHECK((*bytes)[pec + 19] == 0x0D);
    CHECK((*bytes)[pec + 32] == 0xFF);
    CHECK((*bytes)[pec + 34] == 6);  // octets par ligne de vignette (48 px)
    CHECK((*bytes)[pec + 35] == 38); // hauteur de vignette
    // 3 blocs de couleur (couleur, changement, arret) : octet = 3 - 1, indices 5, 2, 2.
    CHECK((*bytes)[pec + 48] == 2);
    CHECK((*bytes)[pec + 49] == 5);
    CHECK((*bytes)[pec + 50] == 2);
    CHECK((*bytes)[pec + 51] == 2); // arret = meme fil
    CHECK((*bytes)[pec + 52] == 0x20);
    CHECK((*bytes)[pec + 511] == 0x20);

    // Bloc de points : signature et longueur ; vignettes (1 + 3) de 228 octets apres le flux.
    const std::size_t block = pec + 512;
    CHECK((*bytes)[block + 5] == 0x31);
    CHECK((*bytes)[block + 6] == 0xFF);
    CHECK((*bytes)[block + 7] == 0xF0);
    const std::size_t length =
        (*bytes)[block + 2] | ((*bytes)[block + 3] << 8) | ((*bytes)[block + 4] << 16);
    CHECK(block + length + 4 * 228 == bytes->size());
    CHECK((*bytes)[block + length - 2] == 0xFF); // marqueur de fin du flux
    CHECK((*bytes)[block + length - 1] == 0x00);
    // Largeur/hauteur du motif en 0,1 mm : 70 x 50.
    CHECK((*bytes)[block + 8] == 70);
    CHECK((*bytes)[block + 10] == 50);
}

TEST_CASE("pes : octets du flux PEC d'un motif minimal (axe Y inverse, formes courte/longue)") {
    // (0,0) -> (1 mm, 0) -> (1 mm, 5 mm vers le haut) puis un point a 12 mm (forme longue).
    const auto seq = make({
        {um(0, 0), CommandType::Stitch},
        {um(1'000, 0), CommandType::Stitch},
        {um(1'000, 5'000), CommandType::Stitch},
        {um(13'000, 5'000), CommandType::Stitch},
        {um(13'000, 5'000), CommandType::End},
    });
    const auto bytes = encode_pes(seq);
    REQUIRE(bytes.has_value());
    const std::size_t block = u32(*bytes, 8) + 512;
    // Boite 130 x 50 -> centre (65, 25) : saut de positionnement (-65, -25 haut) = (-65, +25 bas).
    const std::vector<std::uint8_t> expected = {
        0x9F, 0xBF, // x = -65 : 12 bits 0xFBF, drapeau saut
        0x90, 0x19, // y = +25
        0x00, 0x00, // premier point (0,0)
        0x0A, 0x00, // +1 mm en x
        0x00, 0x4E, // +5 mm vers le haut = -50 dans le fichier (forme courte 7 bits)
        0x80, 0x78, // +120 en x : forme longue (0x078)
        0x00,       // y = 0 (forme courte)
        0xFF, 0x00, // fin
    };
    const std::vector<std::uint8_t> actual(bytes->begin() + static_cast<std::ptrdiff_t>(block) + 16,
                                           bytes->begin() + static_cast<std::ptrdiff_t>(block) +
                                               16 + static_cast<std::ptrdiff_t>(expected.size()));
    CHECK(actual == expected);
}

TEST_CASE("pes : aller-retour exact (types, positions, couleurs) au pas de 0,1 mm") {
    const auto seq = rich_short();
    const auto bytes = encode_pes(seq, two_colors());
    REQUIRE(bytes.has_value());
    const auto decoded = decode_pes(*bytes);
    REQUIRE(decoded.has_value());
    check_same_shape(rebased(seq), decoded->sequence);
    REQUIRE(decoded->block_colors.size() == 3);
    CHECK(decoded->block_colors[0] == kRed);
    CHECK(decoded->block_colors[1] == kBlue);
    CHECK(decoded->block_colors[2] == kBlue);
    CHECK(decoded->name == "OPENSTITCH");
    // L'arret (meme indice de fil) est relu comme un arret, pas un changement de couleur.
    CHECK(decoded->sequence.commands[10].type == CommandType::ColorChange);
    CHECK(decoded->sequence.commands[13].type == CommandType::Stop);
}

TEST_CASE("pes : deterministe et idempotent apres un tour (grand saut decoupe)") {
    const auto seq = rich_long_jump();
    MachineExportOptions options;
    options.block_colors = {kRed, kBlue};
    const auto first = encode_pes(seq, options);
    const auto again = encode_pes(seq, options);
    REQUIRE(first.has_value());
    REQUIRE(again.has_value());
    CHECK(*first == *again);

    const auto decoded = decode_pes(*first);
    REQUIRE(decoded.has_value());
    // Le saut de 55 mm tient en une seule forme longue (max 204,7 mm) : aucun decoupage ici.
    check_same_shape(rebased(seq), decoded->sequence);
    options.block_colors = decoded->block_colors;
    const auto second = encode_pes(decoded->sequence, options);
    REQUIRE(second.has_value());
    CHECK(*second == *first);
}

TEST_CASE("pes : saut de plus de 204,7 mm decoupe en plusieurs enregistrements") {
    const auto seq = make({
        {um(0, 0), CommandType::Stitch},
        {um(1'000, 0), CommandType::Stitch},
        {um(1'000, 0), CommandType::Jump},
        {um(501'000, 100'000), CommandType::Jump}, // 500 mm
        {um(501'000, 100'000), CommandType::Stitch},
        {um(502'000, 100'000), CommandType::Stitch},
        {um(502'000, 100'000), CommandType::End},
    });
    const auto bytes = encode_pes(seq);
    REQUIRE(bytes.has_value());
    const auto decoded = decode_pes(*bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->sequence.commands.size() > seq.commands.size()); // sauts intermediaires
    // Position finale exacte.
    const auto& last = decoded->sequence.commands[decoded->sequence.commands.size() - 2];
    CHECK(last.pos == um(502'000, 100'000));
}

TEST_CASE("pes : tous les deltas courts et longs d'un point cousu font l'aller-retour") {
    StitchSequence seq;
    seq.commands.push_back({um(0, 0), CommandType::Stitch, ObjectId{}});
    std::int32_t x = 0;
    std::int32_t y = 0;
    for (int d = -200; d <= 200; d += 7) {
        x += d * 100;
        y -= (d / 2) * 100;
        seq.commands.push_back({um(x, y), CommandType::Stitch, ObjectId{}});
    }
    seq.commands.push_back({um(x, y), CommandType::End, ObjectId{}});
    const auto bytes = encode_pes(seq);
    REQUIRE(bytes.has_value());
    const auto decoded = decode_pes(*bytes);
    REQUIRE(decoded.has_value());
    check_same_shape(rebased(seq), decoded->sequence);
}

TEST_CASE("pes : options machine : coupes, arrets et changements de couleur") {
    const auto seq = rich_short();

    MachineExportOptions noTrims = two_colors();
    noTrims.trims = TrimMode::Drop;
    const auto a = decode_pes(*encode_pes(seq, noTrims));
    REQUIRE(a.has_value());
    for (const auto& c : a->sequence.commands) {
        CHECK(c.type != CommandType::Trim);
    }

    MachineExportOptions stopAsChange = two_colors();
    stopAsChange.stops = StopMode::AsColorChange;
    stopAsChange.block_colors = {kRed, kBlue, kRed};
    const auto b = decode_pes(*encode_pes(seq, stopAsChange));
    REQUIRE(b.has_value());
    std::size_t changes = 0;
    std::size_t stops = 0;
    for (const auto& c : b->sequence.commands) {
        changes += c.type == CommandType::ColorChange ? 1 : 0;
        stops += c.type == CommandType::Stop ? 1 : 0;
    }
    CHECK(changes == 2);
    CHECK(stops == 0);
    CHECK(b->block_colors.size() == 3);

    MachineExportOptions noStops = two_colors();
    noStops.stops = StopMode::Drop;
    const auto c = decode_pes(*encode_pes(seq, noStops));
    REQUIRE(c.has_value());
    CHECK(c->block_colors.size() == 2);

    MachineExportOptions mono = two_colors();
    mono.color_changes = ColorChangeMode::Drop;
    mono.stops = StopMode::Drop;
    const auto d = decode_pes(*encode_pes(seq, mono));
    REQUIRE(d.has_value());
    CHECK(d->block_colors.size() == 1);
}

TEST_CASE("pes : fils differents consecutifs ne partagent jamais un indice (sinon arret)") {
    MachineExportOptions options;
    // Deux couleurs quasi identiques, proches de la meme entree PEC.
    options.block_colors = {{237, 23, 31}, {238, 24, 30}, {237, 23, 31}};
    const auto seq = make({
        {um(0, 0), CommandType::Stitch},
        {um(1'000, 0), CommandType::Stitch},
        {um(1'000, 0), CommandType::ColorChange},
        {um(2'000, 0), CommandType::Stitch},
        {um(2'000, 0), CommandType::ColorChange},
        {um(3'000, 0), CommandType::Stitch},
        {um(3'000, 0), CommandType::End},
    });
    const auto decoded = decode_pes(*encode_pes(seq, options));
    REQUIRE(decoded.has_value());
    std::size_t stops = 0;
    for (const auto& c : decoded->sequence.commands) {
        stops += c.type == CommandType::Stop ? 1 : 0;
    }
    CHECK(stops == 0);
}

TEST_CASE("pes : limites -- trop de couleurs et motif hors plage donnent une erreur claire") {
    StitchSequence many;
    many.commands.push_back({um(0, 0), CommandType::Stitch, ObjectId{}});
    for (int i = 0; i < 300; ++i) {
        many.commands.push_back({um(i * 100, 0), CommandType::Stitch, ObjectId{}});
        many.commands.push_back({um(i * 100, 0), CommandType::ColorChange, ObjectId{}});
    }
    many.commands.push_back({um(30'000, 0), CommandType::End, ObjectId{}});
    const auto tooMany = encode_pes(many);
    REQUIRE_FALSE(tooMany.has_value());
    CHECK(tooMany.error().category == ErrorCategory::OperationImpossible);
    CHECK(tooMany.error().message.find("couleurs") != std::string::npos);

    const auto huge = make({
        {um(0, 0), CommandType::Stitch},
        {um(1'000'000, 0), CommandType::Stitch},
        {um(2'000'000, 0), CommandType::Stitch},
        {um(3'000'000, 0), CommandType::Stitch},
        {um(4'000'000, 0), CommandType::Stitch}, // 4 m
        {um(4'000'000, 0), CommandType::End},
    });
    const auto tooBig = encode_pes(huge);
    REQUIRE_FALSE(tooBig.has_value());
    CHECK(tooBig.error().message.find("trop grand") != std::string::npos);

    // Sauts hors plage (motif cousu petit, saut de 4 m).
    const auto farJump = make({
        {um(0, 0), CommandType::Stitch},
        {um(1'000, 0), CommandType::Stitch},
        {um(1'000, 0), CommandType::Jump},
        {um(4'000'000, 0), CommandType::Jump},
        {um(4'000'000, 0), CommandType::Stitch},
        {um(4'000'000, 0), CommandType::End},
    });
    CHECK_FALSE(encode_pes(farJump).has_value());
}

TEST_CASE("pes : decodage tolerant -- fichiers tronques ou corrompus ne plantent jamais") {
    const auto bytes = *encode_pes(rich_short(), two_colors());
    for (std::size_t n = 0; n < bytes.size(); n += 3) {
        const std::vector<std::uint8_t> prefix(bytes.begin(),
                                               bytes.begin() + static_cast<std::ptrdiff_t>(n));
        (void)decode_pes(prefix); // ne doit ni lever ni faire d'acces hors limites
    }
    for (std::uint32_t seed = 1; seed <= 50; ++seed) {
        auto junk = lcg_bytes(900, seed);
        junk[0] = '#';
        junk[1] = 'P';
        junk[2] = 'E';
        junk[3] = 'S';
        junk[8] = 12; // decalage PEC plausible puis octets aleatoires
        junk[9] = junk[10] = junk[11] = 0;
        (void)decode_pes(junk);
        (void)decode_pes(lcg_bytes(900, seed + 1000));
    }
    // Octets modifies un a un dans un vrai fichier.
    for (std::size_t i = 0; i < bytes.size(); i += 5) {
        auto copy = bytes;
        copy[i] = static_cast<std::uint8_t>(copy[i] ^ 0xA5);
        (void)decode_pes(copy);
    }
    const auto notPes = decode_pes(std::vector<std::uint8_t>{1, 2, 3});
    REQUIRE_FALSE(notPes.has_value());
    CHECK(notPes.error().category == ErrorCategory::InvalidFile);
}
