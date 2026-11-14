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

// Délie les deux couches d'un dégradé (retour à un remplissage uni) sans toucher à
// leurs paramètres. Undo restaure exactement les liens précédents.
class ClearColorBlendLinkCommand final : public ICommand {
public:
    ClearColorBlendLinkCommand(ObjectId first, ObjectId second) : first_(first), second_(second) {}

    void apply(document::Project& project) override {
        previousFirst_ = previousSecond_ = std::nullopt;
        if (auto* first = project.findEmbroidery(first_)) {
            previousFirst_ = first->color_blend;
            first->color_blend.reset();
        }
        if (auto* second = project.findEmbroidery(second_)) {
            previousSecond_ = second->color_blend;
            second->color_blend.reset();
        }
    }

    void revert(document::Project& project) override {
        if (auto* first = project.findEmbroidery(first_)) {
            first->color_blend = previousFirst_;
        }
        if (auto* second = project.findEmbroidery(second_)) {
            second->color_blend = previousSecond_;
        }
    }

    [[nodiscard]] std::string name() const override { return "Délier les couches du dégradé"; }

private:
    ObjectId first_;
    ObjectId second_;
    std::optional<document::ColorBlendLink> previousFirst_;
    std::optional<document::ColorBlendLink> previousSecond_;
};

// Métadonnée d'appartenance à un groupe multicolore, modifiée sans toucher aux
// points : les paramètres de couture sont mis à jour séparément et de façon
// atomique par une CompositeCommand.
class SetMultiBlendLinkCommand final : public ICommand {
public:
    SetMultiBlendLinkCommand(ObjectId id, std::optional<document::MultiBlendLink> next)
        : id_(id), next_(std::move(next)) {}

    void apply(document::Project& project) override {
        if (auto* object = project.findEmbroidery(id_)) {
            before_ = object->multi_blend;
            object->multi_blend = next_;
        }
    }

    void revert(document::Project& project) override {
        if (auto* object = project.findEmbroidery(id_)) {
            object->multi_blend = before_;
        }
    }

    [[nodiscard]] std::string name() const override { return "Modifier le groupe de fondu"; }

private:
    ObjectId id_;
    std::optional<document::MultiBlendLink> before_;
    std::optional<document::MultiBlendLink> next_;
};

} // namespace openstitch::commands
