// SPDX-License-Identifier: Apache-2.0
//
// Previsualisation du tirage (carte de deplacement + sequence simulee). Valide la geometrie et
// le determinisme, jamais une efficacite textile reelle (aucun coefficient mesure).
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "openstitch/stitch_generation/pull_map.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

namespace {

stitch::StitchCommand stitch_at(int x, int y, std::uint64_t source = 1) {
    stitch::StitchCommand c;
    c.pos = {{x}, {y}};
    c.type = stitch::CommandType::Stitch;
    c.source = ObjectId{source};
    c.pass = stitch::StitchPass::TopStitch;
    return c;
}

// Quelques rangees de tatami de 8 mm de long.
stitch::StitchSequence fill() {
    stitch::StitchSequence s;
    for (int row = 0; row < 20; ++row) {
        const int y = row * 400;
        const bool leftToRight = row % 2 == 0;
        s.commands.push_back(stitch_at(leftToRight ? 0 : 8000, y));
        s.commands.push_back(stitch_at(leftToRight ? 8000 : 0, y));
    }
    return s;
}

} // namespace

TEST_CASE("pull_map: sans point cousu la carte est vide", "[pull_map]") {
    CHECK(compute_pull_map({}, FabricResponse{}, Micrometers{500}).empty());
    stitch::StitchSequence jumps;
    stitch::StitchCommand j;
    j.type = stitch::CommandType::Jump;
    jumps.commands = {j, j};
    CHECK(compute_pull_map(jumps, FabricResponse{}, Micrometers{500}).empty());
    CHECK(simulate_fabric_pull(jumps, FabricResponse{}).commands == jumps.commands);
}

TEST_CASE("pull_map: la grille couvre le motif et le pic est au bord du remplissage",
          "[pull_map]") {
    const stitch::StitchSequence seq = fill();
    const PullMap map = compute_pull_map(seq, FabricResponse{}, Micrometers{500});
    REQUIRE_FALSE(map.empty());
    CHECK(map.samples.size() == map.columns * map.rows);
    CHECK(map.origin.x.value < 0);
    CHECK(map.origin.y.value < 0);
    const PullPeak peak = find_pull_peak(map);
    REQUIRE(peak.found);
    CHECK(peak.magnitude_mm > 0.0);
    CHECK(peak.magnitude_mm <= FabricResponse{}.max_displacement_mm + 0.01);
    // Le tissu se contracte : au bord droit du remplissage, il est tire vers la gauche.
    const PullField field(collect_pull_sources(seq), FabricResponse{});
    CHECK(field.sample({{8000}, {4000}}).displacement.x.value < 0);
}

TEST_CASE("pull_map: la grille est bornee a 512 cases par cote", "[pull_map]") {
    stitch::StitchSequence big;
    big.commands = {stitch_at(0, 0), stitch_at(400000, 300000)};
    const PullMap map = compute_pull_map(big, FabricResponse{}, Micrometers{100});
    REQUIRE_FALSE(map.empty());
    CHECK(map.columns <= 513);
    CHECK(map.rows <= 513);
    CHECK(map.cell.value > 100);
}

TEST_CASE("pull_map: determinisme octet pour octet", "[pull_map]") {
    const stitch::StitchSequence seq = fill();
    const PullMap a = compute_pull_map(seq, FabricResponse{}, Micrometers{400});
    const PullMap b = compute_pull_map(seq, FabricResponse{}, Micrometers{400});
    REQUIRE(a.samples.size() == b.samples.size());
    for (std::size_t i = 0; i < a.samples.size(); ++i) {
        CHECK(a.samples[i].displacement == b.samples[i].displacement);
        CHECK(a.samples[i].confidence == b.samples[i].confidence);
    }
    stitch::StitchSequence two = seq;
    two.commands.push_back(
        {{}, stitch::CommandType::ColorChange, {}, stitch::StitchPass::TopStitch});
    two.commands.push_back(stitch_at(8000, 0, 2));
    two.commands.push_back(stitch_at(8000, 7600, 2));
    CHECK(simulate_fabric_pull(two, FabricResponse{}).commands ==
          simulate_fabric_pull(two, FabricResponse{}).commands);
}

TEST_CASE("pull_map: la simulation decale les blocs suivants, pas le premier", "[pull_map]") {
    stitch::StitchSequence seq = fill(); // bloc 1 : tatami de l'objet 1
    stitch::StitchCommand change;
    change.type = stitch::CommandType::ColorChange;
    seq.commands.push_back(change);
    const std::size_t secondStart = seq.commands.size();
    // bloc 2 : un contour (objet 2) le long du bord droit du remplissage
    seq.commands.push_back(stitch_at(8000, 0, 2));
    seq.commands.push_back(stitch_at(8000, 7600, 2));

    const stitch::StitchSequence sim = simulate_fabric_pull(seq, FabricResponse{});
    REQUIRE(sim.commands.size() == seq.commands.size());
    for (std::size_t i = 0; i < seq.commands.size(); ++i) {
        CHECK(sim.commands[i].type == seq.commands[i].type);
        CHECK(sim.commands[i].source == seq.commands[i].source);
        if (i < secondStart) {
            CHECK(sim.commands[i].pos == seq.commands[i].pos); // premier bloc : en place
        }
    }
    // Le contour, cousu apres, suit le tissu contracte par le remplissage (x diminue).
    CHECK(sim.commands[secondStart].pos.x.value < 8000);
}

TEST_CASE("pull_map: un seul bloc de couleur est rendu inchange", "[pull_map]") {
    const stitch::StitchSequence seq = fill();
    CHECK(simulate_fabric_pull(seq, FabricResponse{}).commands == seq.commands);
}

TEST_CASE("pull_map: l'image de la carte est transparente hors influence", "[pull_map]") {
    const PullMap map = compute_pull_map(fill(), FabricResponse{}, Micrometers{500});
    const PullRaster r = rasterize_pull_map(map);
    REQUIRE(r.width == static_cast<int>(map.columns));
    REQUIRE(r.height == static_cast<int>(map.rows));
    REQUIRE(r.rgba.size() == static_cast<std::size_t>(r.width) * r.height * 4);
    CHECK(r.rgba[3] == 0); // coin haut-gauche : hors portee
    bool opaque = false;
    for (std::size_t i = 3; i < r.rgba.size(); i += 4) {
        opaque = opaque || r.rgba[i] > 0;
    }
    CHECK(opaque);
    CHECK(rasterize_pull_map(PullMap{}).width == 0);
}

TEST_CASE("pull_map: les reglages nommes ordonnent le tirage", "[pull_map]") {
    CHECK(fabric_preset("stable").gain < fabric_preset("default").gain);
    CHECK(fabric_preset("linen").gain > fabric_preset("default").gain);
    CHECK(fabric_preset("inconnu").gain == fabric_preset("default").gain);
    const stitch::StitchSequence seq = fill();
    const auto peak = [&seq](std::string_view name) {
        return find_pull_peak(compute_pull_map(seq, fabric_preset(name), Micrometers{500}))
            .magnitude_mm;
    };
    CHECK(peak("linen") >= peak("default"));
    CHECK(peak("default") >= peak("stable"));
    CHECK(fabric_preset_names().size() == 3);
}
