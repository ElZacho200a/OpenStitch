// SPDX-License-Identifier: Apache-2.0
// HP-EDIT-002 : suppression et insertion de points par retouche (StitchOverride::deleted /
// inserted_after) appliquees par apply_manual_overrides.
#include <catch2/catch_test_macros.hpp>

#include "openstitch/stitch_generation/generate.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

namespace {

using CmdType = stitch::CommandType;
using Pass = stitch::StitchPass;

stitch::StitchCommand mk(std::int32_t x, std::int32_t y, CmdType type, ObjectId source,
                         Pass pass = Pass::TopStitch) {
    return {Vec2um{Micrometers{x}, Micrometers{y}}, type, source, pass};
}

// Jump (Travel) puis 3 Stitch (TopStitch) aux index bruts 1, 2, 3.
stitch::StitchSequence make_sequence(ObjectId oid) {
    stitch::StitchSequence seq;
    seq.commands = {
        mk(0, 0, CmdType::Jump, oid, Pass::Travel),
        mk(0, 0, CmdType::Stitch, oid),
        mk(1'000, 0, CmdType::Stitch, oid),
        mk(1'000, 1'000, CmdType::Stitch, oid),
    };
    return seq;
}

document::Project edited_project(ObjectId oid, const stitch::StitchSequence& seq,
                                 std::vector<document::StitchOverride> overrides) {
    document::Project project;
    document::EmbroideryObject obj;
    obj.id = oid;
    const auto raw = raw_slice(seq, oid);
    obj.edited_point_count = static_cast<std::uint32_t>(raw.size());
    obj.edited_fingerprint = fingerprint(raw);
    obj.overrides = std::move(overrides);
    project.embroidery_objects.push_back(obj);
    return project;
}

Vec2um pt(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

} // namespace

TEST_CASE("point delete : supprime un point TopStitch") {
    const ObjectId oid{1};
    auto seq = make_sequence(oid);
    document::StitchOverride ov;
    ov.base_index = 2;
    ov.deleted = true;
    const auto project = edited_project(oid, seq, {ov});

    CHECK(apply_manual_overrides(seq, project).empty());
    REQUIRE(seq.commands.size() == 3);
    CHECK(seq.commands[1].pos == pt(0, 0));
    CHECK(seq.commands[2].pos == pt(1'000, 1'000));
}

TEST_CASE("point delete : un point de passe Travel n'est pas supprimable") {
    const ObjectId oid{1};
    auto seq = make_sequence(oid);
    document::StitchOverride ov;
    ov.base_index = 0;
    ov.deleted = true;
    const auto project = edited_project(oid, seq, {ov});

    (void)apply_manual_overrides(seq, project);
    CHECK(seq.commands.size() == 4);
}

TEST_CASE("point insert : points Stitch Manual apres le point cible, dans l'ordre") {
    const ObjectId oid{1};
    auto seq = make_sequence(oid);
    document::StitchOverride ov;
    ov.base_index = 1;
    ov.inserted_after = {pt(300, 0), pt(600, 0)};
    const auto project = edited_project(oid, seq, {ov});

    CHECK(apply_manual_overrides(seq, project).empty());
    REQUIRE(seq.commands.size() == 6);
    CHECK(seq.commands[2].pos == pt(300, 0));
    CHECK(seq.commands[3].pos == pt(600, 0));
    CHECK(seq.commands[2].type == CmdType::Stitch);
    CHECK(seq.commands[2].pass == Pass::Manual);
    CHECK(seq.commands[2].source == oid);
    CHECK(seq.commands[4].pos == pt(1'000, 0));
}

TEST_CASE("point insert delete : suppression, insertion et coupe combinees") {
    const ObjectId oid{1};
    auto seq = make_sequence(oid);
    document::StitchOverride a;
    a.base_index = 1;
    a.deleted = true;
    a.inserted_after = {pt(50, 50)};
    document::StitchOverride b;
    b.base_index = 2;
    b.trim_after = true;
    const auto project = edited_project(oid, seq, {a, b});

    (void)apply_manual_overrides(seq, project);
    // Jump, [point 1 supprime], insere, point 2, Trim, point 3
    REQUIRE(seq.commands.size() == 5);
    CHECK(seq.commands[1].pos == pt(50, 50));
    CHECK(seq.commands[2].pos == pt(1'000, 0));
    CHECK(seq.commands[3].type == CmdType::Trim);
    CHECK(seq.commands[4].pos == pt(1'000, 1'000));
}

TEST_CASE("point delete : objet Dirty -- suppression jamais reappliquee") {
    const ObjectId oid{1};
    auto seq = make_sequence(oid);
    document::StitchOverride ov;
    ov.base_index = 1;
    ov.deleted = true;
    auto project = edited_project(oid, seq, {ov});
    project.embroidery_objects[0].edited_fingerprint += 1;

    const auto dirty = apply_manual_overrides(seq, project);
    CHECK(dirty.size() == 1);
    CHECK(seq.commands.size() == 4);
}
