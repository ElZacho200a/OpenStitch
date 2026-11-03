// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <string>
#include <utility>

#include "openstitch/commands/command.hpp"

namespace openstitch::commands {

// Remplacement atomique du projet par un état déjà calculé (ADR 0001, option B) : un lot validé
// sur une COPIE (`project_ops::apply_batch`) est installé en un seul pas d'annulation.
// `apply` installe `after`, `revert` rétablit `before` ; les deux instantanés sont immuables et
// partagés, donc Undo/Redo sont exacts (identifiants et paramètres compris) et ne dépendent pas
// de la rejouabilité de commandes individuelles. Jamais fusionnée.
class ReplaceProjectCommand final : public ICommand {
public:
    ReplaceProjectCommand(std::string name, std::shared_ptr<const document::Project> before,
                          std::shared_ptr<const document::Project> after)
        : name_(std::move(name)), before_(std::move(before)), after_(std::move(after)) {}

    void apply(document::Project& project) override { project = *after_; }
    void revert(document::Project& project) override { project = *before_; }
    [[nodiscard]] std::string name() const override { return name_; }

private:
    std::string name_;
    std::shared_ptr<const document::Project> before_;
    std::shared_ptr<const document::Project> after_;
};

} // namespace openstitch::commands
