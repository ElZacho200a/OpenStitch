// SPDX-License-Identifier: Apache-2.0
#include "context_commands.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace openstitch::desktop_v2;

TEST_CASE("context menu table serves node, text and rung commands", "[context_commands]") {
    for (const char* id : {"node.smooth", "node.corner", "node.hole.remove", "text.remove",
                           "satin.rung.insert", "edit.delete", "order.front", "arrange.group"}) {
        CHECK(commandBelongsTo(CommandSurface::ContextMenu, id));
    }
}

TEST_CASE("context menu table excludes undo, redo and global commands", "[context_commands]") {
    for (const char* id : {"edit.undo", "edit.redo", "file.save", "workspace.digitize",
                           "thread.palette", "chat.open"}) {
        CHECK_FALSE(commandBelongsTo(CommandSurface::ContextMenu, id));
    }
}

TEST_CASE("floating bar follows the active contexts without duplicates", "[context_commands]") {
    CHECK(floatingBarCommands({}).empty());
    const CommandContext obj[] = {CommandContext::Object};
    const auto base = floatingBarCommands(obj);
    CHECK(!base.empty());
    const CommandContext both[] = {CommandContext::Object, CommandContext::Satin,
                                   CommandContext::Object};
    const auto more = floatingBarCommands(both);
    CHECK(more.size() == base.size() + 3);
    CHECK(more.front() == base.front());
    CHECK(std::find(more.begin(), more.end(), "satin.rung.insert") != more.end());
}
