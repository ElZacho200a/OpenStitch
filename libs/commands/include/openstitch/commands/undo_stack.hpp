// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "openstitch/commands/command.hpp"

namespace openstitch::commands {

class UndoStack {
public:
    // Applique la commande et l'empile. Toute nouvelle commande invalide
    // la branche « rétablir ».
    void execute(std::unique_ptr<ICommand> command, document::Project& project);

    bool undo(document::Project& project);
    bool redo(document::Project& project);

    [[nodiscard]] bool canUndo() const { return !undo_.empty(); }
    [[nodiscard]] bool canRedo() const { return !redo_.empty(); }
    [[nodiscard]] std::string undoName() const;
    [[nodiscard]] std::string redoName() const;

    void clear();

private:
    std::vector<std::unique_ptr<ICommand>> undo_;
    std::vector<std::unique_ptr<ICommand>> redo_;
};

}  // namespace openstitch::commands
