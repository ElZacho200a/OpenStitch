// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>

#include "openstitch/commands/command.hpp"

namespace openstitch::commands {

// Lie deux remplissages sans modifier leurs paramètres ni leur rang de couture.
// Les anciens liens, même invalides, sont restaurés exactement par undo.
class SetColorBlendLinkCommand final : public ICommand {
public:
    SetColorBlendLinkCommand(ObjectId background, ObjectId foreground)
        : background_(background), foreground_(foreground) {}

    void apply(document::Project& project) override {
        applied_ = false;
        auto* background = project.findEmbroidery(background_);
        auto* foreground = project.findEmbroidery(foreground_);
        if (!background_.valid() || !foreground_.valid() || background_ == foreground_ ||
            background == nullptr || foreground == nullptr ||
            !(background->is_tatami() || background->is_directional()) ||
            !(foreground->is_tatami() || foreground->is_directional()) ||
            !background->source_vector.valid() ||
            background->source_vector != foreground->source_vector ||
            project.findObject(background->source_vector) == nullptr) {
            return;
        }
        previousBackground_ = background->color_blend;
        previousForeground_ = foreground->color_blend;
        background->color_blend = document::ColorBlendLink{foreground_, false};
        foreground->color_blend = document::ColorBlendLink{background_, true};
        applied_ = true;
    }

    void revert(document::Project& project) override {
        if (!applied_) {
            return;
        }
        if (auto* background = project.findEmbroidery(background_)) {
            background->color_blend = previousBackground_;
        }
        if (auto* foreground = project.findEmbroidery(foreground_)) {
            foreground->color_blend = previousForeground_;
        }
        applied_ = false;
    }

    [[nodiscard]] std::string name() const override { return "Lier les couches du fondu"; }

private:
    ObjectId background_;
    ObjectId foreground_;
    std::optional<document::ColorBlendLink> previousBackground_;
    std::optional<document::ColorBlendLink> previousForeground_;
    bool applied_{false};
};

} // namespace openstitch::commands
