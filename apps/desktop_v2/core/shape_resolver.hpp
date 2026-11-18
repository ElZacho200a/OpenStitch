// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- la FORME vectorielle est l'unité d'édition ; la broderie en est une facette.
// Une cible de sélection (forme, broderie) se résout ici, en un seul endroit, vers la forme qu'elle
// désigne : les outils de géométrie (pinceau, nœuds, suppression de forme) n'ont plus à savoir si
// la forme est déjà convertie en points. Sans Qt.
//
// Les régions de segmentation ne sont PAS résolues ici : elles appartiennent à l'espace
// Segmentation et se suppriment par `RemoveRegionCommand`.

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include "openstitch/commands/command.hpp"
#include "openstitch/document/project.hpp"
#include "selection_model.hpp"

namespace openstitch::desktop_v2 {

// Forme désignée par une cible (`VectorObject` : elle-même ; `EmbroideryObject` : sa source).
// Vide pour une région ou une cible disparue du document.
[[nodiscard]] std::optional<ObjectId> shapeOfTarget(const document::Project& project,
                                                    Target target);

// Formes distinctes désignées par la sélection, dans l'ordre de première apparition. Les formes
// verrouillées (forme ou calque) sont écartées : elles restent intouchables au canevas.
[[nodiscard]] std::vector<ObjectId> resolveEditableShapes(const document::Project& project,
                                                          const std::vector<Target>& targets);

// Suppression de FORME : retire chaque forme éditable et toutes les broderies qui en dépendent,
// en une seule étape d'annulation. Les lettres d'un texte (`text_owner`) sont écartées (elles se
// retirent avec leur objet texte). `command` est nul quand il n'y a rien à supprimer.
struct ShapeRemoval {
    std::unique_ptr<commands::ICommand> command;
    std::size_t shapes{0};       // formes supprimées
    std::size_t embroideries{0}; // broderies emportées avec elles
};
[[nodiscard]] ShapeRemoval prepareShapeRemoval(const document::Project& project,
                                               const std::vector<Target>& targets);

} // namespace openstitch::desktop_v2
