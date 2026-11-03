// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "openstitch/commands/command.hpp"
#include "openstitch/document/colorway.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::commands {

// Instantané des fils de tous les objets de broderie (HP-THR-006).
[[nodiscard]] inline document::Colorway capture_colorway(const document::Project& project,
                                                         std::string name) {
    document::Colorway colorway;
    colorway.name = std::move(name);
    colorway.entries.reserve(project.embroidery_objects.size());
    for (const auto& object : project.embroidery_objects) {
        colorway.entries.push_back({object.id, object.thread, object.rgb});
    }
    return colorway;
}

namespace detail {

// Colorie les objets d'après un coloris ; les objets absents du coloris gardent leur couleur.
inline void apply_colorway(document::Project& project, const document::Colorway& colorway) {
    for (const auto& entry : colorway.entries) {
        if (auto* object = project.findEmbroidery(entry.object)) {
            object->thread = entry.thread;
            object->rgb = entry.rgb;
        }
    }
}

// État annulable commun : la liste des coloris, l'actif et les couleurs des objets.
struct ColorwayState {
    std::vector<document::Colorway> colorways;
    int active{-1};
    document::Colorway objects; // couleurs courantes de tous les objets
};

[[nodiscard]] inline ColorwayState save_state(const document::Project& project) {
    return {project.colorways, project.active_colorway, capture_colorway(project, {})};
}

inline void restore_state(document::Project& project, const ColorwayState& state) {
    project.colorways = state.colorways;
    project.active_colorway = state.active;
    apply_colorway(project, state.objects);
}

// Rafraîchit l'instantané du coloris actif d'après les objets (les retouches faites depuis
// l'activation y sont inscrites).
inline void refresh_active(document::Project& project) {
    if (project.active_colorway >= 0 &&
        static_cast<std::size_t>(project.active_colorway) < project.colorways.size()) {
        auto& slot = project.colorways[static_cast<std::size_t>(project.active_colorway)];
        slot = capture_colorway(project, slot.name);
    }
}

} // namespace detail

// Ajoute un coloris = copie des couleurs actuelles, et l'active. Si le document n'a encore aucun
// coloris, le motif tel qu'il est devient d'abord le coloris « base_name » (sinon ses couleurs
// d'origine seraient perdues au premier changement). Un seul pas d'annulation.
class AddColorwayCommand final : public ICommand {
public:
    explicit AddColorwayCommand(std::string name, std::string base_name = "Original")
        : name_(std::move(name)), base_name_(std::move(base_name)) {}

    void apply(document::Project& project) override {
        before_ = detail::save_state(project);
        if (project.colorways.empty()) {
            project.colorways.push_back(capture_colorway(project, base_name_));
        } else {
            detail::refresh_active(project);
        }
        project.colorways.push_back(capture_colorway(project, name_));
        project.active_colorway = static_cast<int>(project.colorways.size()) - 1;
    }
    void revert(document::Project& project) override { detail::restore_state(project, before_); }
    [[nodiscard]] std::string name() const override { return "Ajouter un coloris"; }

private:
    std::string name_;
    std::string base_name_;
    detail::ColorwayState before_;
};

// Active le coloris `index` : les retouches du coloris sortant sont d'abord inscrites dans son
// instantané, puis les objets prennent les couleurs du coloris entrant. Un indice invalide ou le
// coloris déjà actif ne change rien (`changed()` renvoie alors false).
class SetActiveColorwayCommand final : public ICommand {
public:
    explicit SetActiveColorwayCommand(int index) : index_(index) {}

    void apply(document::Project& project) override {
        before_ = detail::save_state(project);
        changed_ = false;
        if (index_ < 0 || static_cast<std::size_t>(index_) >= project.colorways.size() ||
            index_ == project.active_colorway) {
            return;
        }
        detail::refresh_active(project);
        detail::apply_colorway(project, project.colorways[static_cast<std::size_t>(index_)]);
        project.active_colorway = index_;
        changed_ = true;
    }
    void revert(document::Project& project) override { detail::restore_state(project, before_); }
    [[nodiscard]] std::string name() const override { return "Changer de coloris"; }
    [[nodiscard]] bool changed() const { return changed_; }

private:
    int index_;
    bool changed_{false};
    detail::ColorwayState before_;
};

// Supprime un coloris INACTIF (supprimer l'actif ne ferait que perdre le dessin : refusé, no-op).
// L'indice du coloris actif est recalé. Un seul pas d'annulation.
class RemoveColorwayCommand final : public ICommand {
public:
    explicit RemoveColorwayCommand(int index) : index_(index) {}

    void apply(document::Project& project) override {
        before_ = detail::save_state(project);
        if (index_ < 0 || static_cast<std::size_t>(index_) >= project.colorways.size() ||
            index_ == project.active_colorway) {
            return;
        }
        project.colorways.erase(project.colorways.begin() + index_);
        if (project.active_colorway > index_) {
            --project.active_colorway;
        }
    }
    void revert(document::Project& project) override { detail::restore_state(project, before_); }
    [[nodiscard]] std::string name() const override { return "Supprimer un coloris"; }

private:
    int index_;
    detail::ColorwayState before_;
};

class RenameColorwayCommand final : public ICommand {
public:
    RenameColorwayCommand(int index, std::string name) : index_(index), name_(std::move(name)) {}

    void apply(document::Project& project) override {
        applied_ = false;
        if (index_ < 0 || static_cast<std::size_t>(index_) >= project.colorways.size()) {
            return;
        }
        auto& slot = project.colorways[static_cast<std::size_t>(index_)];
        previous_ = slot.name;
        slot.name = name_;
        applied_ = true;
    }
    void revert(document::Project& project) override {
        if (applied_) {
            project.colorways[static_cast<std::size_t>(index_)].name = previous_;
        }
    }
    [[nodiscard]] std::string name() const override { return "Renommer un coloris"; }

private:
    int index_;
    std::string name_;
    std::string previous_;
    bool applied_{false};
};

// HP-THR-008 : change le tissu de fond du document (couleur + libellé).
class SetFabricCommand final : public ICommand {
public:
    explicit SetFabricCommand(document::FabricSettings fabric) : fabric_(std::move(fabric)) {}

    void apply(document::Project& project) override {
        previous_ = project.fabric;
        project.fabric = fabric_;
    }
    void revert(document::Project& project) override { project.fabric = previous_; }
    [[nodiscard]] std::string name() const override { return "Changer le tissu"; }

private:
    document::FabricSettings fabric_;
    document::FabricSettings previous_;
};

} // namespace openstitch::commands
