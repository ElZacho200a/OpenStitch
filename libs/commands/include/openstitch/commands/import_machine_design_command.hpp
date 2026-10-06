// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>

#include "openstitch/commands/command.hpp"
#include "openstitch/document/imported_design.hpp"

namespace openstitch::commands {

// AD-04 : remplace l'exception desktop `MainWindow::sequenceImported_`
// (§17) -- importer un fichier machine devient une mutation du document
// comme une autre, passant par `ICommand` (Tier 0), donc annulable. Ne
// touche QUE `project.imported_design` : l'appelant (desktop `importDst`,
// S2a) décide séparément s'il vide le reste du document avant de pousser
// cette commande (import = remplacement du document en P0, même
// confirmation utilisateur qu'avant). Même patron d'échange que
// `SetSegmentationCommand` (`project_commands.hpp`).
class SetImportedDesignCommand final : public ICommand {
public:
    explicit SetImportedDesignCommand(std::optional<document::ImportedDesign> next)
        : next_(std::move(next)) {}

    void apply(document::Project& project) override {
        previous_ = std::move(project.imported_design);
        project.imported_design = std::move(next_);
        next_.reset();
    }
    void revert(document::Project& project) override {
        next_ = std::move(project.imported_design);
        project.imported_design = std::move(previous_);
        previous_.reset();
    }
    [[nodiscard]] std::string name() const override { return "Importer un design machine"; }

private:
    std::optional<document::ImportedDesign> next_;
    std::optional<document::ImportedDesign> previous_;
};

} // namespace openstitch::commands
