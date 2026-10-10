// SPDX-License-Identifier: Apache-2.0
// Commandes du lettrage (HP-TXT-001). Un texte et ses lettres dérivées (objets
// vectoriels `text_owner` + objets de broderie) sont remplacés ATOMIQUEMENT :
// créer, éditer le texte ou ses réglages, supprimer = un seul pas d'annulation.
// Les lettres sont calculées par `lettering::build_text_objects` (hors de cette
// lib, qui reste indépendante de FreeType) et passées toutes faites.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "openstitch/commands/command.hpp"
#include "openstitch/document/text_object.hpp"

namespace openstitch::commands {

// Crée le texte (id absent du projet) ou REMPLACE texte + lettres dérivées
// (id existant). Les nouvelles lettres reprennent la position d'ordre de couture
// des anciennes. Annulation : état exact d'avant (anciennes lettres, anciens
// index), y compris les retouches que l'utilisateur y avait faites.
class SetTextObjectCommand final : public ICommand {
public:
    SetTextObjectCommand(document::TextObject text, std::vector<document::VectorObject> vectors,
                         std::vector<document::EmbroideryObject> embroideries, std::string label);

    void apply(document::Project& project) override;
    void revert(document::Project& project) override;
    [[nodiscard]] std::string name() const override { return label_; }

private:
    document::TextObject text_;
    std::vector<document::VectorObject> vectors_;
    std::vector<document::EmbroideryObject> embroideries_;
    std::string label_;

    // État capturé par apply() pour revert().
    bool applied_{false};
    std::optional<std::size_t> textIndex_;
    std::optional<document::TextObject> oldText_;
    std::vector<std::pair<std::size_t, document::VectorObject>> oldVectors_;
    std::vector<std::pair<std::size_t, document::EmbroideryObject>> oldEmbroideries_;
};

// Supprime un texte ET toutes ses lettres (vecteurs + broderies).
class RemoveTextObjectCommand final : public ICommand {
public:
    explicit RemoveTextObjectCommand(ObjectId id) : id_(id) {}

    void apply(document::Project& project) override;
    void revert(document::Project& project) override;
    [[nodiscard]] std::string name() const override { return "Supprimer le texte"; }

private:
    ObjectId id_;
    bool applied_{false};
    std::size_t textIndex_{0};
    document::TextObject oldText_{};
    std::vector<std::pair<std::size_t, document::VectorObject>> oldVectors_;
    std::vector<std::pair<std::size_t, document::EmbroideryObject>> oldEmbroideries_;
};

} // namespace openstitch::commands
