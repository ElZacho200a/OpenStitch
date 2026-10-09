// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
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

    // Coalescence : une commande exécutée moins de `window` après la précédente
    // et de même `mergeKey()` non vide est absorbée par celle-ci (un seul pas
    // d'annulation pour une rafale). 0 = désactivée. Défaut : 600 ms.
    void setMergeWindow(std::chrono::milliseconds window) { mergeWindow_ = window; }
    // Coupe la chaîne de fusion : la prochaine commande sera empilée à part
    // (à appeler à l'enregistrement, au changement de sélection, etc.). Undo,
    // redo et clear la coupent d'eux-mêmes.
    void breakMergeChain() { mergeable_ = false; }
    // Noms des pas d'annulation (du plus ancien au plus récent) et de
    // rétablissement (du prochain à rétablir au plus lointain) : panneau Historique.
    [[nodiscard]] std::vector<std::string> undoNames() const;
    [[nodiscard]] std::vector<std::string> redoNames() const;

private:
    std::chrono::milliseconds mergeWindow_{600};
    std::chrono::steady_clock::time_point lastExecute_{};
    bool mergeable_{false};
    std::vector<std::unique_ptr<ICommand>> undo_;
    std::vector<std::unique_ptr<ICommand>> redo_;
};

} // namespace openstitch::commands
