// SPDX-License-Identifier: Apache-2.0
#include "openstitch/commands/undo_stack.hpp"

namespace openstitch::commands {

void UndoStack::execute(std::unique_ptr<ICommand> command, document::Project& project) {
    command->apply(project);
    undo_.push_back(std::move(command));
    redo_.clear();
}

bool UndoStack::undo(document::Project& project) {
    if (undo_.empty()) {
        return false;
    }
    undo_.back()->revert(project);
    redo_.push_back(std::move(undo_.back()));
    undo_.pop_back();
    return true;
}

bool UndoStack::redo(document::Project& project) {
    if (redo_.empty()) {
        return false;
    }
    redo_.back()->apply(project);
    undo_.push_back(std::move(redo_.back()));
    redo_.pop_back();
    return true;
}

std::string UndoStack::undoName() const {
    return undo_.empty() ? std::string{} : undo_.back()->name();
}

std::string UndoStack::redoName() const {
    return redo_.empty() ? std::string{} : redo_.back()->name();
}

void UndoStack::clear() {
    undo_.clear();
    redo_.clear();
}

}  // namespace openstitch::commands
