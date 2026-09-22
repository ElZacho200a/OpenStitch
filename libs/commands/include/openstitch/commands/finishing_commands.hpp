// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include "openstitch/commands/command.hpp"
#include "openstitch/document/finishing.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::commands {

// Remplace les finitions de la séquence du projet (coupes automatiques,
// points d'arrêt, points courts -- Lots E/F) et mémorise les précédentes pour
// un retour exact.
class SetFinishingCommand final : public ICommand {
public:
    explicit SetFinishingCommand(document::SequenceFinishing finishing) : finishing_(finishing) {}

    void apply(document::Project& project) override {
        previous_ = project.finishing;
        project.finishing = finishing_;
    }
    void revert(document::Project& project) override { project.finishing = previous_; }
    [[nodiscard]] std::string name() const override { return "Options de génération"; }

private:
    document::SequenceFinishing finishing_;
    document::SequenceFinishing previous_{};
};

} // namespace openstitch::commands
