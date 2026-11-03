// SPDX-License-Identifier: Apache-2.0
//
// HP-THR-008 (fil proche du tissu), HP-THR-011 (plan de fusion), HP-THR-012 (pipette sur les
// points).
#include <catch2/catch_test_macros.hpp>

#include "openstitch/stitch_analysis/fabric_rules.hpp"
#include "openstitch/stitch_analysis/stitch_pick.hpp"
#include "openstitch/stitch_analysis/thread_reduction.hpp"

using namespace openstitch;
using namespace openstitch::stitch_analysis;

namespace {

document::Project with_colors(std::initializer_list<std::array<std::uint8_t, 3>> colors) {
    document::Project p;
    for (const auto& rgb : colors) {
        document::EmbroideryObject e;
        e.id = p.object_ids.next();
        e.name = "obj";
        e.rgb = rgb;
        p.embroidery_objects.push_back(e);
    }
    return p;
}

ThreadUsage usage_of(ObjectId id, std::array<std::uint8_t, 3> rgb, std::size_t stitches) {
    ThreadUsage u;
    u.identity.rgb = rgb;
    u.objects = {id};
    u.stitch_count = stitches;
    return u;
}

} // namespace

TEST_CASE("fabric rule : default fabric is not checked") {
    auto p = with_colors({{255, 255, 255}});
    CHECK(check_thread_vs_fabric(p).empty());
}

TEST_CASE("fabric rule : thread close to the fabric is flagged, contrasted thread is not") {
    auto p = with_colors({{30, 40, 120}, {250, 250, 250}, {32, 42, 118}});
    p.fabric.color = {30, 40, 120};
    const auto findings = check_thread_vs_fabric(p);
    REQUIRE(findings.size() == 2);
    CHECK(findings[0].category == "fil-couleur-tissu");
    CHECK(findings[0].object == p.embroidery_objects[0].id);
    CHECK(findings[1].object == p.embroidery_objects[2].id);

    p.embroidery_objects[0].visible = false;
    CHECK(check_thread_vs_fabric(p).size() == 1);
}

TEST_CASE("reduce plan : merges the two nearest threads into the most sewn one") {
    auto p = with_colors({{200, 0, 0}, {205, 2, 2}, {0, 0, 200}});
    std::vector<ThreadUsage> usage{usage_of(p.embroidery_objects[0].id, {200, 0, 0}, 900),
                                   usage_of(p.embroidery_objects[1].id, {205, 2, 2}, 100),
                                   usage_of(p.embroidery_objects[2].id, {0, 0, 200}, 500)};
    const auto plan = plan_reduce_threads(usage, 2);
    CHECK(plan.before == 3);
    CHECK(plan.after == 2);
    REQUIRE(plan.reassignments.size() == 1);
    CHECK(plan.reassignments[0].object == p.embroidery_objects[1].id);
    CHECK(plan.reassignments[0].rgb == std::array<std::uint8_t, 3>{200, 0, 0});
    CHECK(plan.target == std::vector<std::size_t>{0, 0, 2});

    CHECK(plan_reduce_threads(usage, 3).empty());
    CHECK(plan_reduce_threads(usage, 0).empty());
}

TEST_CASE("merge plan : manual merge of two chosen threads") {
    auto p = with_colors({{200, 0, 0}, {0, 200, 0}});
    std::vector<ThreadUsage> usage{usage_of(p.embroidery_objects[0].id, {200, 0, 0}, 10),
                                   usage_of(p.embroidery_objects[1].id, {0, 200, 0}, 10)};
    const auto plan = plan_merge_threads(usage, 0, 1);
    REQUIRE(plan.reassignments.size() == 1);
    CHECK(plan.reassignments[0].object == p.embroidery_objects[0].id);
    CHECK(plan.reassignments[0].rgb == std::array<std::uint8_t, 3>{0, 200, 0});
    CHECK(plan.after == 1);
    CHECK(plan_merge_threads(usage, 1, 1).empty());
    CHECK(plan_merge_threads(usage, 5, 1).empty());
}

TEST_CASE("stitch pick : topmost sewn object near the point wins, jumps do not count") {
    stitch::StitchSequence seq;
    const ObjectId a{1};
    const ObjectId b{2};
    const auto add = [&](stitch::CommandType type, int x, int y, ObjectId src) {
        seq.commands.push_back({Vec2um{{x}, {y}}, type, src, stitch::StitchPass::TopStitch});
    };
    add(stitch::CommandType::Stitch, 0, 0, a);
    add(stitch::CommandType::Stitch, 10'000, 0, a);
    add(stitch::CommandType::Jump, 0, 5'000, b);
    add(stitch::CommandType::Stitch, 0, 5'000, b);
    add(stitch::CommandType::Stitch, 10'000, 5'000, b);
    add(stitch::CommandType::Stitch, 10'000, 0, a); // dernier segment cousu, vertical, a x = 10 mm

    CHECK(stitch_object_at(seq, {{5'000}, {100}}, {300}) == a);
    CHECK(stitch_object_at(seq, {{5'000}, {5'100}}, {300}) == b);
    // Le saut de (10,0) a (0,5) n'est pas un fil : rien a mi-chemin.
    CHECK_FALSE(stitch_object_at(seq, {{5'000}, {2'500}}, {300}).has_value());
    // Tolerance etroite : le segment passe a 100 um du point, donc rien.
    CHECK_FALSE(stitch_object_at(seq, {{5'000}, {100}}, {50}).has_value());
}
