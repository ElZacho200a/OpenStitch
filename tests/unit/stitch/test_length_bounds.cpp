// SPDX-License-Identifier: Apache-2.0
// HP-ENG-008 : bornes de longueur de point communes a tous les generateurs, sur la sequence
// effective (jamais generate_sequence).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "openstitch/stitch_generation/finish.hpp"
#include "openstitch/stitch_generation/generate.hpp"
#include "openstitch/stitch_generation/length_bounds.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;
using CmdType = stitch::CommandType;
using Pass = stitch::StitchPass;

namespace {

stitch::StitchCommand st(std::int32_t x, std::int32_t y, Pass pass = Pass::TopStitch) {
    return {Vec2um{Micrometers{x}, Micrometers{y}}, CmdType::Stitch, ObjectId{1}, pass};
}

stitch::StitchCommand jump(std::int32_t x, std::int32_t y) {
    return {Vec2um{Micrometers{x}, Micrometers{y}}, CmdType::Jump, ObjectId{1}, Pass::Travel};
}

document::SequenceFinishing bounds(bool merge, bool split) {
    document::SequenceFinishing f;
    f.filter_short_stitches = merge;
    f.min_stitch_length = Micrometers{500};
    f.split_long_stitches = split;
    f.max_stitch_length = Micrometers{7'000};
    return f;
}

} // namespace

TEST_CASE("bornes : un point trop long est decoupe en morceaux egaux sur la meme droite") {
    const document::Project project;
    const std::vector<stitch::StitchCommand> cmds = {jump(0, 0), st(0, 0), st(20'000, 0)};
    const auto r = enforce_length_bounds(cmds, project, bounds(false, true));
    CHECK(r.report.split == 1);
    CHECK(r.report.added == 2);
    CHECK(r.report.long_left == 0);
    REQUIRE(r.commands.size() == 5);
    CHECK(r.commands[2].pos.x.value == 6'667);
    CHECK(r.commands[3].pos.x.value == 13'333);
    CHECK(r.commands.back().pos.x.value == 20'000);
    for (const auto& c : r.commands) {
        CHECK(c.pos.y.value == 0);
    }
}

TEST_CASE("bornes : un point court interieur est fusionne, les extremites sont conservees") {
    const document::Project project;
    const std::vector<stitch::StitchCommand> cmds = {jump(0, 0), st(0, 0), st(200, 0),
                                                     st(3'000, 0)};
    const auto r = enforce_length_bounds(cmds, project, bounds(true, false));
    CHECK(r.report.merged == 1);
    CHECK(r.report.short_left == 0);
    REQUIRE(r.commands.size() == 3);
    CHECK(r.commands.back().pos.x.value == 3'000);
}

TEST_CASE("bornes : un dernier point trop proche retire son predecesseur") {
    const document::Project project;
    const std::vector<stitch::StitchCommand> cmds = {jump(0, 0), st(0, 0), st(3'000, 0),
                                                     st(3'200, 0)};
    const auto r = enforce_length_bounds(cmds, project, bounds(true, false));
    CHECK(r.report.merged == 1);
    CHECK(r.report.short_left == 0);
    REQUIRE(r.commands.size() == 3);
    CHECK(r.commands.back().pos.x.value == 3'200); // la fin du trace ne bouge pas
}

TEST_CASE("bornes : verrous et retouches manuelles ne sont jamais fusionnes") {
    const document::Project project;
    const std::vector<stitch::StitchCommand> cmds = {jump(0, 0), st(0, 0), st(100, 0, Pass::Lock),
                                                     st(200, 0, Pass::Manual), st(5'000, 0)};
    const auto r = enforce_length_bounds(cmds, project, bounds(true, false));
    CHECK(r.report.merged == 0);
    CHECK(r.commands.size() == cmds.size());
}

TEST_CASE("bornes : mesure des segments hors plage, sauts ignores") {
    const std::vector<stitch::StitchCommand> cmds = {st(0, 0),      st(100, 0),
                                                     st(10'000, 0), jump(50'000, 0),
                                                     st(50'000, 0), st(50'100, 0, Pass::Lock)};
    const auto m = measure_length_bounds(cmds, 500, 7'000);
    CHECK(m.short_left == 1);
    CHECK(m.long_left == 1);
}

TEST_CASE("bornes : activees, aucune sequence effective ne depasse le maximum (tous types)") {
    document::Project project;
    for (int i = 0; i < 3; ++i) {
        document::VectorObject vec;
        vec.id = project.object_ids.next();
        geometry::Path p;
        p.closed = true;
        const std::int32_t x0 = i * 40'000;
        const auto node = [](std::int32_t x, std::int32_t y) {
            return geometry::PathNode{Vec2um{Micrometers{x}, Micrometers{y}},
                                      geometry::NodeType::Corner, std::nullopt, std::nullopt};
        };
        p.nodes = {node(x0, 0), node(x0 + 30'000, 0), node(x0 + 30'000, 20'000), node(x0, 20'000)};
        vec.paths.push_back(geometry::PathSet{p, {}});
        project.vector_objects.push_back(vec);
        document::EmbroideryObject emb;
        emb.id = project.object_ids.next();
        emb.source_vector = vec.id;
        if (i == 0) {
            emb.params = document::RunningStitchParams{};
        } else if (i == 1) {
            emb.params = document::TatamiParams{};
        } else {
            emb.params = document::DirectionalFillParams{};
        }
        project.embroidery_objects.push_back(emb);
    }
    project.finishing.enabled = true;
    project.finishing.split_long_stitches = true;
    project.finishing.filter_short_stitches = true;
    const auto seq = effective_sequence(project);
    REQUIRE(seq.has_value());
    const auto m =
        measure_length_bounds(seq->commands, 0, project.finishing.max_stitch_length.value);
    CHECK(m.long_left == 0);
    CHECK_FALSE(seq->commands.empty());
}
