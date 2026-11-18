// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>

#include "openstitch/commands/command.hpp"

namespace openstitch::commands {

// Pose, modifie ou retire le FONDU À LA FRONTIÈRE de deux zones
// (docs/studio-fondu-frontiere-spec.md §7) : écrit les deux liens ensemble, sans toucher au type,
// aux paramètres ni à l'ordre des zones. `nullopt` retire le lien de cette zone. Undo restaure
// exactement les liens précédents, même orphelins. La validation (frontière, types, largeurs) est
// faite en amont par `project_ops::make_boundary_blend_command`.
class SetBoundaryBlendLinkCommand final : public ICommand {
public:
    SetBoundaryBlendLinkCommand(ObjectId first,
                                std::optional<document::BoundaryBlendLink> firstLink,
                                ObjectId second,
                                std::optional<document::BoundaryBlendLink> secondLink,
                                std::string label = "Fondu à la frontière")
        : first_(first), second_(second), firstNext_(std::move(firstLink)),
          secondNext_(std::move(secondLink)), label_(std::move(label)) {}

    void apply(document::Project& project) override {
        if (auto* first = project.findEmbroidery(first_)) {
            firstBefore_ = first->boundary_blend;
            first->boundary_blend = firstNext_;
        }
        if (auto* second = project.findEmbroidery(second_)) {
            secondBefore_ = second->boundary_blend;
            second->boundary_blend = secondNext_;
        }
    }

    void revert(document::Project& project) override {
        if (auto* first = project.findEmbroidery(first_)) {
            first->boundary_blend = firstBefore_;
        }
        if (auto* second = project.findEmbroidery(second_)) {
            second->boundary_blend = secondBefore_;
        }
    }

    [[nodiscard]] std::string name() const override { return label_; }

private:
    ObjectId first_;
    ObjectId second_;
    std::optional<document::BoundaryBlendLink> firstNext_;
    std::optional<document::BoundaryBlendLink> secondNext_;
    std::optional<document::BoundaryBlendLink> firstBefore_;
    std::optional<document::BoundaryBlendLink> secondBefore_;
    std::string label_;
};

} // namespace openstitch::commands
