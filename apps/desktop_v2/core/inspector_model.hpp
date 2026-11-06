// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- modèle de l'inspecteur contextuel (principe E) : « valeurs communes / mixtes »
// d'une sélection d'objets de broderie, et construction des lots d'opérations qui les modifient.
// Sans Qt : l'inspecteur est le MÊME chemin que Claude (`project_ops` : set_color, set_stitch_type,
// set_visible, set_locked), jamais une mutation directe.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "openstitch/document/project.hpp"
#include "selection_model.hpp"

namespace openstitch::desktop_v2 {

template <typename T> struct Common {
    bool mixed{false}; // les objets sélectionnés ne partagent pas la même valeur
    T value{};         // valeur commune (significative seulement si !mixed)
};

struct InspectorState {
    std::size_t count{0}; // objets de broderie sélectionnés qui existent dans le document
    std::vector<std::uint64_t> ids;
    Common<std::string> name;
    Common<std::string> typeId; // voir `stitchTypeId`
    Common<std::array<std::uint8_t, 3>> rgb;
    Common<bool> visible;
    Common<bool> locked;
};

[[nodiscard]] InspectorState inspect(const document::Project& project,
                                     const std::vector<Target>& selection);

// Types de points réglables par l'opération `set_stitch_type` (le satin passe par `auto_satin`).
struct SettableType {
    std::string_view id;
    std::string_view label;
};
[[nodiscard]] const std::vector<SettableType>& settableStitchTypes();
// Identifiant de la liste déroulante pour le type courant d'un objet (`stitchTypeId`).
[[nodiscard]] std::string settableIdOf(std::string_view objectTypeId);

// Lots JSON (`openstitch.batch/1`) ; chaîne vide si l'entrée est invalide (aucun objet, type
// inconnu).
[[nodiscard]] std::string batchSetColor(const std::vector<std::uint64_t>& ids,
                                        const std::array<std::uint8_t, 3>& rgb);
[[nodiscard]] std::string batchSetStitchType(const std::vector<std::uint64_t>& ids,
                                             std::string_view type);
// Satin « comme un remplissage » : choisi dans la liste des types, il passe par l'opération
// `auto_satin` (id `auto_satin`, celui de `stitchTypeId`). Comme dans la V1, plusieurs méthodes
// sont essayées à la suite (squelette, balayage parallèle, axe médian, puis axe compact) : la
// première qui construit des colonnes est retenue. `attempt` indexe `satinAttemptCount()`.
[[nodiscard]] std::size_t satinAttemptCount();
[[nodiscard]] std::string batchAutoSatin(const std::vector<std::uint64_t>& ids,
                                         std::size_t attempt);
[[nodiscard]] std::string batchSetVisible(const std::vector<std::uint64_t>& ids, bool visible);
[[nodiscard]] std::string batchSetLocked(const std::vector<std::uint64_t>& ids, bool locked);

} // namespace openstitch::desktop_v2
