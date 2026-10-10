// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "openstitch/commands/thread_commands.hpp"
#include "openstitch/commands/undo_stack.hpp"

using namespace openstitch;
using namespace openstitch::commands;

namespace {

document::Project project_with_three() {
    document::Project p;
    for (std::uint8_t i = 1; i <= 3; ++i) {
        document::EmbroideryObject e;
        e.id = p.object_ids.next();
        e.name = "obj" + std::to_string(i);
        e.rgb = {i, i, i};
        p.embroidery_objects.push_back(e);
    }
    return p;
}

} // namespace

TEST_CASE("SetObjectThreadCommand assigns a thread to several objects in one undo step") {
    document::Project p = project_with_three();
    UndoStack stack;
    const thread_palette::ThreadKey key{"generic", "G06"};
    const std::vector<ObjectId> ids{p.embroidery_objects[0].id, p.embroidery_objects[2].id};
    stack.execute(std::make_unique<SetObjectThreadCommand>(
                      ids, key, std::array<std::uint8_t, 3>{200, 16, 46}),
                  p);
    CHECK(p.embroidery_objects[0].thread == key);
    CHECK(p.embroidery_objects[0].rgb == std::array<std::uint8_t, 3>{200, 16, 46});
    CHECK_FALSE(p.embroidery_objects[1].thread.has_value());
    CHECK(p.embroidery_objects[1].rgb == std::array<std::uint8_t, 3>{2, 2, 2});
    CHECK(p.embroidery_objects[2].thread == key);

    CHECK(stack.undo(p));
    CHECK_FALSE(p.embroidery_objects[0].thread.has_value());
    CHECK(p.embroidery_objects[0].rgb == std::array<std::uint8_t, 3>{1, 1, 1});
    CHECK(p.embroidery_objects[2].rgb == std::array<std::uint8_t, 3>{3, 3, 3});
    CHECK_FALSE(stack.canUndo()); // un seul pas

    CHECK(stack.redo(p));
    CHECK(p.embroidery_objects[2].thread == key);
}

TEST_CASE("SetObjectThreadCommand with no thread frees the object (free colour)") {
    document::Project p = project_with_three();
    p.embroidery_objects[1].thread = thread_palette::ThreadKey{"generic", "G01"};
    UndoStack stack;
    stack.execute(std::make_unique<SetObjectThreadCommand>(
                      std::vector<ObjectId>{p.embroidery_objects[1].id}, std::nullopt,
                      std::array<std::uint8_t, 3>{9, 9, 9}),
                  p);
    CHECK_FALSE(p.embroidery_objects[1].thread.has_value());
    CHECK(p.embroidery_objects[1].rgb == std::array<std::uint8_t, 3>{9, 9, 9});
    CHECK(stack.undo(p));
    CHECK(p.embroidery_objects[1].thread == thread_palette::ThreadKey{"generic", "G01"});
    CHECK(p.embroidery_objects[1].rgb == std::array<std::uint8_t, 3>{2, 2, 2});
}

TEST_CASE("SetObjectThreadCommand per-object assignments and unknown ids are ignored") {
    document::Project p = project_with_three();
    UndoStack stack;
    std::vector<ObjectThreadAssignment> a;
    a.push_back({p.embroidery_objects[0].id, std::nullopt, {10, 0, 0}});
    a.push_back({p.embroidery_objects[1].id, std::nullopt, {0, 10, 0}});
    a.push_back({ObjectId{9999}, std::nullopt, {0, 0, 10}});
    stack.execute(std::make_unique<SetObjectThreadCommand>(std::move(a), "Limiter les fils"), p);
    CHECK(p.embroidery_objects[0].rgb == std::array<std::uint8_t, 3>{10, 0, 0});
    CHECK(p.embroidery_objects[1].rgb == std::array<std::uint8_t, 3>{0, 10, 0});
    CHECK(p.embroidery_objects[2].rgb == std::array<std::uint8_t, 3>{3, 3, 3});
    CHECK(stack.undo(p));
    CHECK(p.embroidery_objects[0].rgb == std::array<std::uint8_t, 3>{1, 1, 1});
    CHECK(p.embroidery_objects[1].rgb == std::array<std::uint8_t, 3>{2, 2, 2});
}
