// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <utility>

#include "openstitch/commands/command.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::commands {

// HP-HOOP-002 : choisit le profil machine du projet (identifiant de
// `document::machine_profiles()`, vide = aucune machine). Ne touche ni au cadre ni au
// design : le profil ne sert qu'aux vérifications avant export.
class SetMachineCommand final : public ICommand {
public:
    explicit SetMachineCommand(std::string machine_id) : machine_id_(std::move(machine_id)) {}

    void apply(document::Project& project) override {
        previous_ = project.machine_id;
        project.machine_id = machine_id_;
    }
    void revert(document::Project& project) override { project.machine_id = previous_; }
    [[nodiscard]] std::string name() const override { return "Choisir la machine"; }

private:
    std::string machine_id_;
    std::string previous_;
};

} // namespace openstitch::commands
