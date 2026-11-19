// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "openstitch/commands/command.hpp"
#include "openstitch/document/boundary_blend.hpp"

namespace openstitch::commands {

// Pose, modifie ou retire le FONDU À LA FRONTIÈRE d'UN couple de zones (`first`, `second`)
// (docs/studio-fondu-frontiere-spec.md §7) : écrit les deux liens ensemble (celui de `first` vers
// `second` et celui de `second` vers `first`), sans toucher aux AUTRES fondus des deux zones, ni
// au type, aux paramètres ou à l'ordre des zones. `nullopt` retire le lien de cette zone vers
// l'autre ; le `partner` d'un lien fourni est imposé (l'autre zone du couple). Undo restaure
// exactement les listes de liens précédentes, même orphelins. La validation (frontière, types,
// largeurs) est faite en amont par `project_ops::make_boundary_blend_command`.
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
            firstBefore_ = first->boundary_blends;
            document::set_boundary_blend_link(first->boundary_blends, second_, firstNext_);
        }
        if (auto* second = project.findEmbroidery(second_)) {
            secondBefore_ = second->boundary_blends;
            document::set_boundary_blend_link(second->boundary_blends, first_, secondNext_);
        }
    }

    void revert(document::Project& project) override {
        if (auto* first = project.findEmbroidery(first_)) {
            first->boundary_blends = firstBefore_;
        }
        if (auto* second = project.findEmbroidery(second_)) {
            second->boundary_blends = secondBefore_;
        }
    }

    [[nodiscard]] std::string name() const override { return label_; }

private:
    ObjectId first_;
    ObjectId second_;
    std::optional<document::BoundaryBlendLink> firstNext_;
    std::optional<document::BoundaryBlendLink> secondNext_;
    std::vector<document::BoundaryBlendLink> firstBefore_;
    std::vector<document::BoundaryBlendLink> secondBefore_;
    std::string label_;
};

} // namespace openstitch::commands
