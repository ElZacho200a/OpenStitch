// SPDX-License-Identifier: Apache-2.0
//
// HP-ENG-013 : preréglages de points (enregistrer, appliquer, supprimer ; annulation exacte).
#include <catch2/catch_test_macros.hpp>

#include "openstitch/commands/stitch_preset_commands.hpp"
#include "openstitch/commands/undo_stack.hpp"

using namespace openstitch;
using namespace openstitch::commands;

namespace {

document::Project two_objects() {
    document::Project p;
    for (int i = 0; i < 2; ++i) {
        document::EmbroideryObject e;
        e.id = p.object_ids.next();
        e.params = document::RunningStitchParams{};
        p.embroidery_objects.push_back(e);
    }
    return p;
}

document::StitchParams tatami_550() {
    document::TatamiParams t;
    t.row_spacing = Micrometers{550};
    return t;
}

document::StitchParams running() {
    return document::RunningStitchParams{};
}

} // namespace

TEST_CASE("SaveStitchPresetCommand adds then replaces a preset by name, undo restores") {
    document::Project p;
    UndoStack stack;
    stack.execute(std::make_unique<SaveStitchPresetCommand>("a", tatami_550()), p);
    REQUIRE(p.stitch_presets.size() == 1);
    stack.execute(std::make_unique<SaveStitchPresetCommand>("a", running()), p);
    REQUIRE(p.stitch_presets.size() == 1);
    CHECK(p.stitch_presets[0].params == running());
    stack.undo(p);
    CHECK(p.stitch_presets[0].params == tatami_550());
    stack.undo(p);
    CHECK(p.stitch_presets.empty());
}

TEST_CASE("RemoveStitchPresetCommand removes and restores at the same index") {
    document::Project p;
    p.stitch_presets = {{"a", tatami_550()}, {"b", running()}};
    const auto before = p.stitch_presets;
    UndoStack stack;
    stack.execute(std::make_unique<RemoveStitchPresetCommand>(0), p);
    REQUIRE(p.stitch_presets.size() == 1);
    CHECK(p.stitch_presets[0].name == "b");
    stack.undo(p);
    CHECK(p.stitch_presets == before);
    stack.execute(std::make_unique<RemoveStitchPresetCommand>(9), p); // invalide : sans effet
    CHECK(p.stitch_presets == before);
}

TEST_CASE("ApplyStitchPresetCommand applies to several objects in one undo step") {
    auto p = two_objects();
    const std::vector<ObjectId> ids{p.embroidery_objects[0].id, p.embroidery_objects[1].id};
    UndoStack stack;
    stack.execute(
        std::make_unique<ApplyStitchPresetCommand>(ids, document::StitchPreset{"a", tatami_550()}),
        p);
    for (const auto& e : p.embroidery_objects) {
        CHECK(e.params == tatami_550());
        CHECK(e.intent == document::EmbroideryIntent::ForcedUserChoice);
    }
    CHECK(stack.undo(p));
    for (const auto& e : p.embroidery_objects) {
        CHECK(e.params == running());
        CHECK(e.intent == document::EmbroideryObject{}.intent);
    }
    CHECK_FALSE(stack.undo(p)); // un seul pas
}
