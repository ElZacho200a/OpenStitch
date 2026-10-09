// SPDX-License-Identifier: Apache-2.0
#include "openstitch/commands/undo_stack.hpp"

namespace openstitch::commands {

void UndoStack::execute(std::unique_ptr<ICommand> command, document::Project& project) {
    command->apply(project);
    const auto now = std::chrono::steady_clock::now();
    // Coalescence : même clé, dans la fenêtre, sans coupure (undo/redo/sauvegarde).
    if (mergeable_ && mergeWindow_.count() > 0 && !undo_.empty() && redo_.empty() &&
        now - lastExecute_ <= mergeWindow_) {
        const std::string key = command->mergeKey();
        if (!key.empty() && key == undo_.back()->mergeKey() && undo_.back()->mergeWith(*command)) {
            lastExecute_ = now;
            return;
        }
    }
    undo_.push_back(std::move(command));
    redo_.clear();
    lastExecute_ = now;
    mergeable_ = !undo_.back()->mergeKey().empty();
}

bool UndoStack::undo(document::Project& project) {
    if (undo_.empty()) {
        return false;
    }
    undo_.back()->revert(project);
    redo_.push_back(std::move(undo_.back()));
    undo_.pop_back();
    mergeable_ = false;
    return true;
}

bool UndoStack::redo(document::Project& project) {
    if (redo_.empty()) {
        return false;
    }
    redo_.back()->apply(project);
    undo_.push_back(std::move(redo_.back()));
    redo_.pop_back();
    mergeable_ = false;
    return true;
}

std::string UndoStack::undoName() const {
    return undo_.empty() ? std::string{} : undo_.back()->name();
}

std::string UndoStack::redoName() const {
    return redo_.empty() ? std::string{} : redo_.back()->name();
}

std::vector<std::string> UndoStack::undoNames() const {
    std::vector<std::string> names;
    names.reserve(undo_.size());
    for (const auto& c : undo_) {
        names.push_back(c->name());
    }
    return names;
}

std::vector<std::string> UndoStack::redoNames() const {
    std::vector<std::string> names;
    names.reserve(redo_.size());
    for (auto it = redo_.rbegin(); it != redo_.rend(); ++it) {
        names.push_back((*it)->name());
    }
    return names;
}

void UndoStack::clear() {
    undo_.clear();
    redo_.clear();
    mergeable_ = false;
}

} // namespace openstitch::commands
