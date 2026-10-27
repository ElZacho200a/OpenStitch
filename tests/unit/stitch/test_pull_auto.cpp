// SPDX-License-Identifier: Apache-2.0
//
// Compensation du tirage AUTOMATIQUE (HP-ENG-001.b, `PullMode::Auto`).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <utility>
#include <vector>

#include "openstitch/stitch_generation/generate.hpp"
#include "openstitch/stitch_generation/pull_auto.hpp"
#include "openstitch/stitch_generation/satin.hpp"
#include "openstitch/stitch_generation/tatami.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

namespace {

Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

geometry::PathNode node(std::int32_t x, std::int32_t y) {
    return geometry::PathNode{um(x, y), geometry::NodeType::Corner, std::nullopt, std::nullopt};
}

geometry::PathSet rect(std::int32_t w, std::int32_t h) {
    geometry::Path p;
    p.closed = true;
    p.nodes = {node(0, 0), node(w, 0), node(w, h), node(0, h)};
    return geometry::PathSet{p, {}};
}

document::TatamiParams tatami() {
    document::TatamiParams p;
    p.inset = Micrometers{0};
    return p;
}

// Étendue en x des points d'un remplissage (rangées horizontales : angle 0).
std::int32_t x_extent(const std::vector<FillStitch>& fill) {
    std::int32_t lo = 1'000'000;
    std::int32_t hi = -1'000'000;
    for (const auto& fs : fill) {
        lo = std::min(lo, fs.pos.x.value);
        hi = std::max(hi, fs.pos.x.value);
    }
    return hi - lo;
}

document::Project tatami_project(const document::TatamiParams& params) {
    document::Project project;
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.paths.push_back(rect(20'000, 10'000));
    project.vector_objects.push_back(vec);
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.source_vector = vec.id;
    emb.params = params;
    project.embroidery_objects.push_back(emb);
    return project;
}

stitch::StitchSequence gen(const document::Project& project) {
    auto seq = generate_sequence(project);
    REQUIRE(seq.has_value());
    return std::move(*seq);
}

bool same_points(const stitch::StitchSequence& a, const stitch::StitchSequence& b) {
    if (a.commands.size() != b.commands.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.commands.size(); ++i) {
        if (a.commands[i].pos != b.commands[i].pos || a.commands[i].type != b.commands[i].type) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("tirage auto : bornes du depassement des remplissages") {
    CHECK(choose_fill_pull(0.0).value == 0);
    CHECK(choose_fill_pull(-1.0).value == 0);
    CHECK(choose_fill_pull(0.3).value == 159);   // 0,15 + 0,03 x 0,3
    CHECK(choose_fill_pull(100.0).value == 400); // plafonné à 0,4 mm
    CHECK(choose_fill_pull(5.0).value > choose_fill_pull(2.0).value);
}

TEST_CASE("tirage auto : bornes de l'elargissement du satin") {
    CHECK(choose_satin_pull(0.0).value == 0);
    CHECK(choose_satin_pull(0.5).value == 100); // plancher 0,1 mm
    CHECK(choose_satin_pull(4.0).value == 200);
    CHECK(choose_satin_pull(40.0).value == 350); // plafond 0,35 mm
}

TEST_CASE("tirage auto : mode Manual renvoie les parametres inchanges") {
    auto p = tatami();
    p.pull_compensation = Micrometers{123};
    const auto out = resolve_pull_compensation(p, rect(20'000, 10'000));
    CHECK(out == p);
}

TEST_CASE("tirage auto : mode Auto ignore le reglage manuel et suit la largeur") {
    auto p = tatami();
    p.pull_compensation = Micrometers{2'500}; // ignoré
    p.pull_mode = document::PullMode::Auto;
    const auto thin = resolve_pull_compensation(p, rect(20'000, 2'000));
    const auto thick = resolve_pull_compensation(p, rect(20'000, 20'000));
    CHECK(thin.pull_compensation.value < 2'500);
    CHECK(thin.pull_compensation.value >= 150);
    CHECK(thick.pull_compensation.value > thin.pull_compensation.value);
    CHECK(thick.pull_compensation.value <= 400);
}

TEST_CASE("tirage auto : les rangees depassent de la compensation choisie") {
    auto p = tatami();
    p.pull_mode = document::PullMode::Auto;
    const geometry::PathSet region = rect(20'000, 10'000);
    const auto resolved = resolve_pull_compensation(p, region);
    REQUIRE(resolved.pull_compensation.value > 0);
    const auto base = fill_tatami(region, tatami());
    const auto pulled = fill_tatami(region, resolved);
    CHECK(x_extent(pulled) == x_extent(base) + 2 * resolved.pull_compensation.value);
}

TEST_CASE("tirage auto : la generation applique le mode Auto, Manual reste historique") {
    const auto manualSeq = gen(tatami_project(tatami()));
    auto manualZero = tatami();
    manualZero.pull_compensation = Micrometers{0};
    CHECK(same_points(manualSeq, gen(tatami_project(manualZero))));

    auto autoParams = tatami();
    autoParams.pull_mode = document::PullMode::Auto;
    const auto autoSeq = gen(tatami_project(autoParams));
    CHECK_FALSE(same_points(manualSeq, autoSeq));

    // Auto équivaut au réglage manuel de la valeur choisie pour la même forme.
    auto manualEquivalent = tatami();
    manualEquivalent.pull_compensation =
        resolve_pull_compensation(autoParams, rect(20'000, 10'000)).pull_compensation;
    CHECK(same_points(autoSeq, gen(tatami_project(manualEquivalent))));

    // Déterminisme.
    CHECK(same_points(autoSeq, gen(tatami_project(autoParams))));
}

TEST_CASE("tirage auto : le satin Auto elargit la colonne") {
    const auto make = [](document::PullMode mode) {
        document::Project project;
        document::SatinParams sp;
        sp.rail_a.nodes = {node(0, 0), node(20'000, 0)};
        sp.rail_b.nodes = {node(0, 4'000), node(20'000, 4'000)};
        sp.center_underlay = false;
        sp.pull_mode = mode;
        document::EmbroideryObject emb;
        emb.id = project.object_ids.next();
        emb.params = sp;
        project.embroidery_objects.push_back(emb);
        return gen(project);
    };
    const auto spread = [](const stitch::StitchSequence& s) {
        std::int32_t lo = 1'000'000;
        std::int32_t hi = -1'000'000;
        for (const auto& c : s.commands) {
            lo = std::min(lo, c.pos.y.value);
            hi = std::max(hi, c.pos.y.value);
        }
        return hi - lo;
    };
    const auto manual = make(document::PullMode::Manual);
    const auto automatic = make(document::PullMode::Auto);
    REQUIRE_FALSE(manual.commands.empty());
    CHECK(spread(automatic) == spread(manual) + 2 * choose_satin_pull(4.0).value);
}

TEST_CASE("tirage auto : satin a largeur variable compense par traversee") {
    const std::vector<SatinStation> stations{
        {um(0, -1'000), um(0, 1'000)},
        {um(2'000, -4'000), um(2'000, 4'000)},
    };
    SatinConfig base;
    base.density = Micrometers{2'000};
    SatinConfig local = base;
    local.auto_pull_local = true;
    const auto nominal = finish_satin_stations(stations, base);
    const auto compensated = finish_satin_stations(stations, local);
    REQUIRE(compensated.satin.size() == nominal.satin.size());
    REQUIRE(compensated.satin.size() >= 4);
    CHECK(std::abs(compensated.satin.front().y.value - nominal.satin.front().y.value) ==
          choose_satin_pull(2.0).value);
    CHECK(std::abs(compensated.satin.back().y.value - nominal.satin.back().y.value) ==
          choose_satin_pull(8.0).value);
    CHECK(compensated.satin == finish_satin_stations(stations, local).satin);
}
