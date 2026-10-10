// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "openstitch/commands/command.hpp"
#include "openstitch/core/ids.hpp"
#include "openstitch/thread_palette/thread.hpp"

namespace openstitch::commands {

// Couleur/fil cible d'un objet de broderie.
struct ObjectThreadAssignment {
    ObjectId id;
    std::optional<thread_palette::ThreadKey> thread; // vide = couleur libre
    std::array<std::uint8_t, 3> rgb{};               // couleur de rendu (celle du fil si assigné)
};

// HP-THR-004 / HP-OBJ-018 : assigne un fil (ou une couleur libre) à un ou
// plusieurs objets de broderie en UN SEUL pas d'annulation. `rgb` reste la
// source de rendu : l'appelant passe la couleur du fil choisi. Un fil vide
// (`thread == nullopt`) fait de l'objet une couleur libre (version RGB de
// HP-OBJ-018). Les identifiants inconnus sont ignorés ; l'état précédent de
// chaque objet touché est mémorisé pour un retour exact.
class SetObjectThreadCommand final : public ICommand {
public:
    explicit SetObjectThreadCommand(std::vector<ObjectThreadAssignment> assignments,
                                    std::string label = {})
        : assignments_(std::move(assignments)), label_(std::move(label)) {}

    // Même cible pour tous les objets.
    SetObjectThreadCommand(const std::vector<ObjectId>& ids,
                           std::optional<thread_palette::ThreadKey> thread,
                           std::array<std::uint8_t, 3> rgb, std::string label = {})
        : label_(std::move(label)) {
        assignments_.reserve(ids.size());
        for (const ObjectId id : ids) {
            assignments_.push_back({id, thread, rgb});
        }
    }

    void apply(document::Project& project) override {
        previous_.clear();
        for (const auto& a : assignments_) {
            if (auto* obj = project.findEmbroidery(a.id)) {
                previous_.push_back({a.id, obj->thread, obj->rgb});
                obj->thread = a.thread;
                obj->rgb = a.rgb;
            }
        }
    }
    void revert(document::Project& project) override {
        for (auto it = previous_.rbegin(); it != previous_.rend(); ++it) {
            if (auto* obj = project.findEmbroidery(it->id)) {
                obj->thread = it->thread;
                obj->rgb = it->rgb;
            }
        }
    }
    [[nodiscard]] std::string name() const override {
        if (!label_.empty()) {
            return label_;
        }
        const bool anyThread = !assignments_.empty() && assignments_.front().thread.has_value();
        return anyThread ? "Assigner un fil" : "Changer la couleur";
    }

private:
    std::vector<ObjectThreadAssignment> assignments_;
    std::vector<ObjectThreadAssignment> previous_;
    std::string label_;
};

} // namespace openstitch::commands
