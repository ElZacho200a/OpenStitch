// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "openstitch/commands/finishing_commands.hpp"
#include "openstitch/commands/undo_stack.hpp"

using namespace openstitch;
using namespace openstitch::commands;

TEST_CASE("SetFinishingCommand : applique les finitions, undo/redo exacts") {
    document::Project project;
    const auto before = project.finishing;
    document::SequenceFinishing changed;
    changed.trim_threshold = Micrometers{5'000};
    changed.lock_type = document::LockStitch::MicroZigzag;
    changed.filter_short_stitches = false;

    UndoStack stack;
    stack.execute(std::make_unique<SetFinishingCommand>(changed), project);
    CHECK(project.finishing == changed);
    stack.undo(project);
    CHECK(project.finishing == before);
    stack.redo(project);
    CHECK(project.finishing == changed);
}
