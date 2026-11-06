// SPDX-License-Identifier: Apache-2.0
#include "context_commands.hpp"

#include <catch2/catch_test_macros.hpp>

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
