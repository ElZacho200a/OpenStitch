// SPDX-License-Identifier: Apache-2.0
//
// HP-THR-006 / HP-THR-008 : commandes de coloris et de tissu (annulation exacte).
#include <catch2/catch_test_macros.hpp>

#include "openstitch/commands/colorway_commands.hpp"
#include "openstitch/commands/thread_commands.hpp"
#include "openstitch/commands/undo_stack.hpp"

using namespace openstitch;
using namespace openstitch::commands;

namespace {

document::Project two_objects() {
    document::Project p;
    for (std::uint8_t i = 1; i <= 2; ++i) {
        document::EmbroideryObject e;
        e.id = p.object_ids.next();
        e.rgb = {i, i, i};
        p.embroidery_objects.push_back(e);
    }
    return p;
}

void recolor(UndoStack& stack, document::Project& p, std::size_t index, std::uint8_t v) {
    stack.execute(std::make_unique<SetObjectThreadCommand>(
                      std::vector<ObjectId>{p.embroidery_objects[index].id}, std::nullopt,
                      std::array<std::uint8_t, 3>{v, v, v}),
                  p);
}

} // namespace

TEST_CASE("AddColorwayCommand captures the base design then the copy and activates it") {
    auto p = two_objects();
    UndoStack stack;
    stack.execute(std::make_unique<AddColorwayCommand>("Hiver"), p);
    REQUIRE(p.colorways.size() == 2);
    CHECK(p.colorways[0].name == "Original");
    CHECK(p.colorways[1].name == "Hiver");
    CHECK(p.active_colorway == 1);
    CHECK(p.colorways[0].entries.size() == 2);

    CHECK(stack.undo(p));
    CHECK(p.colorways.empty());
    CHECK(p.active_colorway == -1);
}

TEST_CASE("SetActiveColorwayCommand switches colors and keeps edits of the outgoing colorway") {
    auto p = two_objects();
    UndoStack stack;
    stack.execute(std::make_unique<AddColorwayCommand>("Hiver"), p);
    recolor(stack, p, 0, 50); // retouche faite dans « Hiver »
    CHECK(p.embroidery_objects[0].rgb[0] == 50);

    stack.execute(std::make_unique<SetActiveColorwayCommand>(0), p);
    CHECK(p.active_colorway == 0);
    CHECK(p.embroidery_objects[0].rgb[0] == 1); // couleurs d'origine
    CHECK(p.colorways[1].entries[0].rgb[0] == 50);

    stack.execute(std::make_unique<SetActiveColorwayCommand>(1), p);
    CHECK(p.embroidery_objects[0].rgb[0] == 50);
    CHECK(p.embroidery_objects[1].rgb[0] == 2);

    CHECK(stack.undo(p)); // retour à « Original »
    CHECK(p.active_colorway == 0);
    CHECK(p.embroidery_objects[0].rgb[0] == 1);
}

TEST_CASE("SetActiveColorwayCommand ignores an invalid or already active index") {
    auto p = two_objects();
    UndoStack stack;
    stack.execute(std::make_unique<AddColorwayCommand>("B"), p);
    auto same = std::make_unique<SetActiveColorwayCommand>(1);
    auto* raw = same.get();
    stack.execute(std::move(same), p);
    CHECK_FALSE(raw->changed());
    CHECK(p.active_colorway == 1);
    stack.execute(std::make_unique<SetActiveColorwayCommand>(9), p);
    CHECK(p.active_colorway == 1);
}

TEST_CASE("RemoveColorwayCommand refuses the active one and reindexes the active one") {
    auto p = two_objects();
    UndoStack stack;
    stack.execute(std::make_unique<AddColorwayCommand>("B"), p);
    stack.execute(std::make_unique<AddColorwayCommand>("C"), p);
    REQUIRE(p.colorways.size() == 3);
    CHECK(p.active_colorway == 2);

    stack.execute(std::make_unique<RemoveColorwayCommand>(2), p); // actif : refusé
    CHECK(p.colorways.size() == 3);

    stack.execute(std::make_unique<RemoveColorwayCommand>(0), p);
    REQUIRE(p.colorways.size() == 2);
    CHECK(p.active_colorway == 1);
    CHECK(p.colorways[1].name == "C");

    CHECK(stack.undo(p));
    CHECK(p.colorways.size() == 3);
    CHECK(p.active_colorway == 2);
}

TEST_CASE("RenameColorwayCommand and SetFabricCommand are undoable") {
    auto p = two_objects();
    UndoStack stack;
    stack.execute(std::make_unique<AddColorwayCommand>("B"), p);
    stack.execute(std::make_unique<RenameColorwayCommand>(1, "Ete"), p);
    CHECK(p.colorways[1].name == "Ete");
    CHECK(stack.undo(p));
    CHECK(p.colorways[1].name == "B");

    document::FabricSettings navy;
    navy.color = {10, 20, 80};
    navy.name = "Marine";
    stack.execute(std::make_unique<SetFabricCommand>(navy), p);
    CHECK(p.fabric == navy);
    CHECK(stack.undo(p));
    CHECK(p.fabric == document::FabricSettings{});
}
