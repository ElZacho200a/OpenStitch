// SPDX-License-Identifier: Apache-2.0
// HP-ENG-010 : un contour ferme demarre au sommet le plus proche de la fin de l'objet precedent.
#include <catch2/catch_test_macros.hpp>

#include <set>
#include <utility>

#include "openstitch/stitch_generation/join.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;
using CmdType = stitch::CommandType;
using Pass = stitch::StitchPass;

namespace {

stitch::StitchCommand cmd(CmdType t, std::int32_t x, std::int32_t y, Pass pass = Pass::TopStitch) {
    return {Vec2um{Micrometers{x}, Micrometers{y}}, t, ObjectId{1}, pass};
}

// Carre ferme de 10 mm demarrant en (0, 0), sens trigonometrique.
std::vector<stitch::StitchCommand> square_ring() {
    return {cmd(CmdType::Jump, 0, 0, Pass::Travel), cmd(CmdType::Stitch, 0, 0),
            cmd(CmdType::Stitch, 10'000, 0),        cmd(CmdType::Stitch, 10'000, 10'000),
            cmd(CmdType::Stitch, 0, 10'000),        cmd(CmdType::Stitch, 0, 0)};
}

std::set<std::pair<int, int>> vertices(const std::vector<stitch::StitchCommand>& c) {
    std::set<std::pair<int, int>> out;
    for (const auto& s : c) {
        if (s.type == CmdType::Stitch) {
            out.insert({s.pos.x.value, s.pos.y.value});
        }
    }
    return out;
}

} // namespace

TEST_CASE("entree auto : un contour ferme demarre au sommet le plus proche") {
    const auto chunk = square_ring();
    const Vec2um from{Micrometers{11'000}, Micrometers{11'000}};
    const auto out = orient_chunk(chunk, from, std::nullopt);
    REQUIRE(out.size() == chunk.size());
    CHECK(out.front().type == CmdType::Jump);
    CHECK(out.front().pos.x.value == 10'000);
    CHECK(out.front().pos.y.value == 10'000);
    // Le contour se referme sur son nouveau depart et couvre les memes sommets.
    CHECK(out.back().pos == out[1].pos);
    CHECK(vertices(out) == vertices(chunk));
    CHECK(total_jump_length_um(stitch::StitchSequence{out}) <= 14'143.0);
}

TEST_CASE("entree auto : depart deja le plus proche, contour inchange") {
    const auto chunk = square_ring();
    const Vec2um from{Micrometers{-500}, Micrometers{-500}};
    CHECK(orient_chunk(chunk, from, std::nullopt) == chunk);
}

TEST_CASE("entree auto : un contour ouvert n'est pas pivote") {
    std::vector<stitch::StitchCommand> open = {
        cmd(CmdType::Jump, 0, 0, Pass::Travel), cmd(CmdType::Stitch, 0, 0),
        cmd(CmdType::Stitch, 5'000, 0), cmd(CmdType::Stitch, 10'000, 0),
        cmd(CmdType::Stitch, 15'000, 0)};
    const Vec2um from{Micrometers{7'000}, Micrometers{0}};
    const auto out = orient_chunk(open, from, std::nullopt);
    // Seul un retournement complet est permis : memes sommets, jamais de rotation.
    CHECK(vertices(out) == vertices(open));
    CHECK((out.front().pos.x.value == 0 || out.front().pos.x.value == 15'000));
}

TEST_CASE("entree auto : la rotation est deterministe") {
    const auto chunk = square_ring();
    const Vec2um from{Micrometers{9'000}, Micrometers{9'000}};
    CHECK(orient_chunk(chunk, from, std::nullopt) == orient_chunk(chunk, from, std::nullopt));
}
