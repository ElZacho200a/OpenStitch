// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "openstitch/commands/command.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::commands {

// Préréglages de points (HP-ENG-013) : jeux de paramètres de couture nommés du projet.

// Ajoute un préréglage, ou remplace celui de même nom (le nom est unique).
class SaveStitchPresetCommand final : public ICommand {
public:
    SaveStitchPresetCommand(std::string name, document::StitchParams params)
        : name_(std::move(name)), params_(std::move(params)) {}

    void apply(document::Project& project) override {
        auto& list = project.stitch_presets;
        const auto it =
            std::find_if(list.begin(), list.end(),
                         [this](const document::StitchPreset& p) { return p.name == name_; });
        if (it != list.end()) {
            replacedIndex_ = static_cast<int>(it - list.begin());
            previous_ = it->params;
            it->params = params_;
        } else {
            replacedIndex_ = -1;
            list.push_back({name_, params_});
        }
    }
    void revert(document::Project& project) override {
        auto& list = project.stitch_presets;
        if (replacedIndex_ >= 0 && static_cast<std::size_t>(replacedIndex_) < list.size()) {
            list[static_cast<std::size_t>(replacedIndex_)].params = previous_;
        } else if (replacedIndex_ < 0 && !list.empty()) {
            list.pop_back();
        }
    }
    [[nodiscard]] std::string name() const override {
        return "Enregistrer le préréglage « " + name_ + " »";
    }

private:
    std::string name_;
    document::StitchParams params_;
    int replacedIndex_{-1};
    document::StitchParams previous_{document::RunningStitchParams{}};
};

// Supprime un préréglage (par indice) ; sans effet si l'indice est invalide.
class RemoveStitchPresetCommand final : public ICommand {
public:
    explicit RemoveStitchPresetCommand(int index) : index_(index) {}

    void apply(document::Project& project) override {
        removed_ = false;
        auto& list = project.stitch_presets;
        if (index_ < 0 || static_cast<std::size_t>(index_) >= list.size()) {
            return;
        }
        saved_ = list[static_cast<std::size_t>(index_)];
        list.erase(list.begin() + index_);
        removed_ = true;
    }
    void revert(document::Project& project) override {
        if (removed_) {
            auto& list = project.stitch_presets;
            list.insert(list.begin() + std::min<std::ptrdiff_t>(
                                           index_, static_cast<std::ptrdiff_t>(list.size())),
                        saved_);
        }
    }
    [[nodiscard]] std::string name() const override { return "Supprimer un préréglage de points"; }

private:
    int index_;
    bool removed_{false};
    document::StitchPreset saved_;
};

// Applique les paramètres d'un préréglage à plusieurs objets de broderie : UN seul pas
// d'annulation. Les objets qui n'existent plus sont ignorés.
class ApplyStitchPresetCommand final : public ICommand {
public:
    ApplyStitchPresetCommand(std::vector<ObjectId> ids, document::StitchPreset preset)
        : ids_(std::move(ids)), preset_(std::move(preset)) {}

    void apply(document::Project& project) override {
        saved_.clear();
        for (const ObjectId id : ids_) {
            if (auto* obj = project.findEmbroidery(id)) {
                saved_.push_back({id, obj->params, obj->intent});
                obj->params = preset_.params;
                obj->intent = document::EmbroideryIntent::ForcedUserChoice;
            }
        }
    }
    void revert(document::Project& project) override {
        for (const auto& s : saved_) {
            if (auto* obj = project.findEmbroidery(s.id)) {
                obj->params = s.params;
                obj->intent = s.intent;
            }
        }
    }
    [[nodiscard]] std::string name() const override {
        return "Appliquer le préréglage « " + preset_.name + " »";
    }

private:
    struct Saved {
        ObjectId id;
        document::StitchParams params;
        document::EmbroideryIntent intent;
    };
    std::vector<ObjectId> ids_;
    document::StitchPreset preset_;
    std::vector<Saved> saved_;
};

} // namespace openstitch::commands
