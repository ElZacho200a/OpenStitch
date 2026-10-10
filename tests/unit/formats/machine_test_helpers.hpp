// SPDX-License-Identifier: Apache-2.0
// Séquences synthétiques et utilitaires partagés par les tests PES / JEF / EXP (HP-FMT-002..005).
// Aucun fichier de référence propriétaire : tout est généré ici.
#pragma once

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "openstitch/formats/machine_design.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace machine_test {

using openstitch::Micrometers;
using openstitch::ObjectId;
using openstitch::Vec2um;
using openstitch::stitch::CommandType;
using openstitch::stitch::StitchSequence;

inline Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

inline StitchSequence make(std::vector<std::pair<Vec2um, CommandType>> items) {
    StitchSequence seq;
    for (const auto& [pos, type] : items) {
        seq.commands.push_back({pos, type, ObjectId{}});
    }
    return seq;
}

// Motif à deux couleurs, une coupe, un saut court, un arrêt -- sans déplacement dépassant les
// plus petits pas de format (aller-retour EXACT attendu pour tous les formats).
inline StitchSequence rich_short() {
    return make({
        {um(0, 0), CommandType::Jump},
        {um(0, 0), CommandType::Stitch},
        {um(5'000, 0), CommandType::Stitch},
        {um(5'000, 5'000), CommandType::Stitch},
        {um(0, 5'000), CommandType::Stitch},
        {um(0, 0), CommandType::Stitch},
        {um(0, 0), CommandType::Trim},
        {um(4'000, 3'000), CommandType::Jump},
        {um(4'000, 3'000), CommandType::Stitch},
        {um(4'500, 3'200), CommandType::Stitch},
        {um(4'500, 3'200), CommandType::ColorChange},
        {um(6'000, 3'200), CommandType::Stitch},
        {um(6'000, 2'000), CommandType::Stitch},
        {um(6'000, 2'000), CommandType::Stop},
        {um(7'000, 2'000), CommandType::Stitch},
        {um(7'000, 2'000), CommandType::End},
    });
}

// Même motif avec un saut de 55 mm (découpé en plusieurs enregistrements par chaque format).
inline StitchSequence rich_long_jump() {
    return make({
        {um(0, 0), CommandType::Stitch},
        {um(3'000, 0), CommandType::Stitch},
        {um(3'000, 0), CommandType::Trim},
        {um(58'000, 21'000), CommandType::Jump},
        {um(58'000, 21'000), CommandType::Stitch},
        {um(59'000, 21'500), CommandType::Stitch},
        {um(59'000, 21'500), CommandType::ColorChange},
        {um(59'000, 20'000), CommandType::Stitch},
        {um(59'000, 20'000), CommandType::End},
    });
}

inline void check_same_shape(const StitchSequence& a, const StitchSequence& b) {
    REQUIRE(a.commands.size() == b.commands.size());
    for (std::size_t i = 0; i < a.commands.size(); ++i) {
        INFO("commande " << i);
        CHECK(a.commands[i].type == b.commands[i].type);
        CHECK(a.commands[i].pos == b.commands[i].pos);
    }
}

// Positions des commandes relatives à la première (le décodeur place le point de départ à 0).
inline StitchSequence rebased(const StitchSequence& s) {
    StitchSequence out = s;
    if (out.commands.empty()) {
        return out;
    }
    const auto origin = out.commands.front().pos;
    for (auto& c : out.commands) {
        c.pos = Vec2um{c.pos.x - origin.x, c.pos.y - origin.y};
    }
    return out;
}

inline std::vector<std::uint8_t> lcg_bytes(std::size_t count, std::uint32_t seed) {
    std::vector<std::uint8_t> out(count);
    std::uint32_t state = seed;
    for (auto& b : out) {
        state = state * 1664525u + 1013904223u;
        b = static_cast<std::uint8_t>(state >> 24);
    }
    return out;
}

} // namespace machine_test
