// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "openstitch/commands/command.hpp"

namespace openstitch::commands {

// Commande composite : N sous-commandes = UN seul pas d'annulation.
//
// - `apply` exécute les sous-commandes dans l'ordre d'insertion ;
//   `revert` les annule dans l'ordre INVERSE (le document revient exactement à
//   son état antérieur).
// - Échec partiel (garantie forte) : si l'`apply` de la sous-commande k lève
//   une exception, les sous-commandes 0..k-1 déjà appliquées sont annulées
//   dans l'ordre inverse, puis l'exception d'origine est relancée : le
//   document est inchangé et la composite n'est pas empilée par `UndoStack`.
//   Un `revert` qui lève est propagé tel quel (pas de rattrapage possible).
// - Une composite vide est un no-op valide.
// - Déterministe : aucune dépendance à autre chose que l'ordre d'insertion.
class CompositeCommand final : public ICommand {
public:
    explicit CompositeCommand(std::string name,
                              std::vector<std::unique_ptr<ICommand>> commands = {});

    // Ajoute une sous-commande en fin de liste (avant le premier `apply`).
    void add(std::unique_ptr<ICommand> command);

    [[nodiscard]] std::size_t size() const { return commands_.size(); }
    [[nodiscard]] bool empty() const { return commands_.empty(); }

    void apply(document::Project& project) override;
    void revert(document::Project& project) override;
    [[nodiscard]] std::string name() const override { return name_; }

private:
    std::string name_;
    std::vector<std::unique_ptr<ICommand>> commands_;
};

} // namespace openstitch::commands
