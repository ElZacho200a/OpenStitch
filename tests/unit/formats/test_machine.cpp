// SPDX-License-Identifier: Apache-2.0
// HP-FMT-001 : normalisation machine PURE, séparée des codecs. Ces tests
// vérifient `normalize_for_machine`/`sequence_from_machine_records`
// directement, sans passer par un format précis (DST ou autre).
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>

#include "openstitch/formats/machine.hpp"

using namespace openstitch;
using namespace openstitch::formats;

namespace {
Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

MachineConstraints dst_like_constraints() {
    MachineConstraints c;
    c.resolution_um = 100;
    c.max_record_delta = 121;
    c.trim_encoding = MachineConstraints::TrimEncoding::RepeatedZeroJumps;
    c.trim_zero_jump_count = 3;
    c.merge_stop_into_color_change = true;
    return c;
}
} // namespace

TEST_CASE("normalize_for_machine : sequence vide refusee") {
    const auto result = normalize_for_machine(stitch::StitchSequence{}, dst_like_constraints());
    CHECK_FALSE(result.has_value());
    CHECK(result.error().category == ErrorCategory::UserInput);
}

TEST_CASE("normalize_for_machine : saut de 50 mm decoupe en enregistrements <= max_record_delta") {
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), stitch::CommandType::Stitch, ObjectId{}},
        {um(50'000, 0), stitch::CommandType::Jump, ObjectId{}}, // 50 mm = 500 unites natives
        {um(50'000, 0), stitch::CommandType::Stitch, ObjectId{}},
        {um(50'000, 0), stitch::CommandType::End, ObjectId{}},
    };
    const auto result = normalize_for_machine(seq, dst_like_constraints());
    REQUIRE(result.has_value());
    for (const auto& rec : result->records) {
        CHECK(std::abs(rec.dx) <= 121);
        CHECK(std::abs(rec.dy) <= 121);
    }
    // 500 unites / 121 -> au moins 5 enregistrements pour ce seul saut.
    int jumpRecords = 0;
    for (const auto& rec : result->records) {
        if (rec.type == MachineRecordType::Jump) {
            ++jumpRecords;
        }
    }
    CHECK(jumpRecords >= 5);

    // Somme des deltas des enregistrements Jump == distance totale (pas de
    // dérive dans le découpage).
    int sumDx = 0;
    for (const auto& rec : result->records) {
        sumDx += rec.dx;
    }
    CHECK(sumDx == 500);
}

TEST_CASE("normalize_for_machine : derive cumulee nulle sur 10 000 points") {
    stitch::StitchSequence seq;
    for (int i = 0; i < 10'000; ++i) {
        // Pas de 0,149 mm : non multiple de la resolution (0,1 mm), chaque
        // arrondi varie mais la position ABSOLUE ne derive jamais.
        seq.commands.push_back({um(i * 149, 0), stitch::CommandType::Stitch, ObjectId{}});
    }
    seq.commands.push_back({seq.commands.back().pos, stitch::CommandType::End, ObjectId{}});

    const auto result = normalize_for_machine(seq, dst_like_constraints());
    REQUIRE(result.has_value());
    long long cumulative = 0;
    for (const auto& rec : result->records) {
        cumulative += rec.dx;
    }
    // Position finale attendue (unites natives, 0,1 mm) : round(9999*149/100).
    const double expected = std::lround(9'999 * 149 / 100.0);
    CHECK(std::abs(static_cast<double>(cumulative) - expected) <= 1.0);
}

TEST_CASE(
    "normalize_for_machine : Trim RepeatedZeroJumps produit trim_zero_jump_count sauts nuls") {
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), stitch::CommandType::Stitch, ObjectId{}},
        {um(1'000, 0), stitch::CommandType::Stitch, ObjectId{}},
        {um(1'000, 0), stitch::CommandType::Trim, ObjectId{}},
        {um(2'000, 0), stitch::CommandType::Stitch, ObjectId{}},
        {um(2'000, 0), stitch::CommandType::End, ObjectId{}},
    };
    auto constraints = dst_like_constraints();
    constraints.trim_zero_jump_count = 5;
    const auto result = normalize_for_machine(seq, constraints);
    REQUIRE(result.has_value());

    int zeroJumps = 0;
    for (const auto& rec : result->records) {
        if (rec.type == MachineRecordType::Jump && rec.dx == 0 && rec.dy == 0) {
            ++zeroJumps;
        }
    }
    CHECK(zeroJumps == 5);
}

TEST_CASE("normalize_for_machine : Trim Native produit un enregistrement Trim dedie") {
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), stitch::CommandType::Stitch, ObjectId{}},
        {um(1'000, 0), stitch::CommandType::Stitch, ObjectId{}},
        {um(1'000, 0), stitch::CommandType::Trim, ObjectId{}},
        {um(2'000, 0), stitch::CommandType::Stitch, ObjectId{}},
        {um(2'000, 0), stitch::CommandType::End, ObjectId{}},
    };
    auto constraints = dst_like_constraints();
    constraints.trim_encoding = MachineConstraints::TrimEncoding::Native;
    const auto result = normalize_for_machine(seq, constraints);
    REQUIRE(result.has_value());

    int trimRecords = 0;
    for (const auto& rec : result->records) {
        if (rec.type == MachineRecordType::Trim) {
            ++trimRecords;
        }
    }
    CHECK(trimRecords == 1);
    // Aucun saut nul artificiel avec l'encodage natif.
    for (const auto& rec : result->records) {
        CHECK_FALSE((rec.type == MachineRecordType::Jump && rec.dx == 0 && rec.dy == 0));
    }
}

TEST_CASE("normalize_for_machine : Stop fondu en ColorChange si merge_stop_into_color_change") {
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), stitch::CommandType::Stitch, ObjectId{}},
        {um(1'000, 0), stitch::CommandType::Stop, ObjectId{}},
        {um(2'000, 0), stitch::CommandType::Stitch, ObjectId{}},
        {um(2'000, 0), stitch::CommandType::End, ObjectId{}},
    };
    const auto merged = normalize_for_machine(seq, dst_like_constraints());
    REQUIRE(merged.has_value());
    bool hasColorChange = false;
    bool hasStop = false;
    for (const auto& rec : merged->records) {
        hasColorChange |= rec.type == MachineRecordType::ColorChange;
        hasStop |= rec.type == MachineRecordType::Stop;
    }
    CHECK(hasColorChange);
    CHECK_FALSE(hasStop);

    auto constraints = dst_like_constraints();
    constraints.merge_stop_into_color_change = false;
    const auto kept = normalize_for_machine(seq, constraints);
    REQUIRE(kept.has_value());
    bool keptStop = false;
    for (const auto& rec : kept->records) {
        keptStop |= rec.type == MachineRecordType::Stop;
    }
    CHECK(keptStop);
}

TEST_CASE("normalize_for_machine : coalescence des sauts organiques sous la resolution") {
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), stitch::CommandType::Stitch, ObjectId{}},
        {um(1'000, 0), stitch::CommandType::Stitch, ObjectId{}},
        {um(1'020, 0), stitch::CommandType::Jump, ObjectId{}},
        {um(1'030, 0), stitch::CommandType::Jump, ObjectId{}},
        {um(1'040, 0), stitch::CommandType::Jump, ObjectId{}},
        {um(2'000, 0), stitch::CommandType::Stitch, ObjectId{}},
        {um(2'000, 0), stitch::CommandType::End, ObjectId{}},
    };
    const auto result = normalize_for_machine(seq, dst_like_constraints());
    REQUIRE(result.has_value());
    int zeroJumps = 0;
    for (const auto& rec : result->records) {
        if (rec.type == MachineRecordType::Jump && rec.dx == 0 && rec.dy == 0) {
            ++zeroJumps;
        }
    }
    // Jamais 3 ou plus (qui serait relu comme une coupe fantome) : un seul
    // saut nul conserve au maximum.
    CHECK(zeroJumps <= 1);
}

TEST_CASE("normalize puis sequence_from_machine_records : aller-retour exact, tous types") {
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), stitch::CommandType::Jump, ObjectId{}},
        {um(0, 0), stitch::CommandType::Stitch, ObjectId{}},
        {um(5'000, 0), stitch::CommandType::Stitch, ObjectId{}},
        {um(5'000, 0), stitch::CommandType::Trim, ObjectId{}},
        {um(5'000, 0), stitch::CommandType::ColorChange, ObjectId{}},
        {um(10'000, 3'000), stitch::CommandType::Stitch, ObjectId{}},
        {um(10'000, 3'000), stitch::CommandType::End, ObjectId{}},
    };
    const auto constraints = dst_like_constraints();
    const auto normalized = normalize_for_machine(seq, constraints);
    REQUIRE(normalized.has_value());
    const auto reconstructed = sequence_from_machine_records(normalized->records, constraints);

    REQUIRE(reconstructed.commands.size() == seq.commands.size());
    for (std::size_t i = 0; i < seq.commands.size(); ++i) {
        CHECK(reconstructed.commands[i].type == seq.commands[i].type);
        CHECK(reconstructed.commands[i].pos == seq.commands[i].pos);
    }
}
