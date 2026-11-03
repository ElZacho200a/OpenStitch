// SPDX-License-Identifier: Apache-2.0
#pragma once

// Modèle de sélection (HP-OBJ-001) : état et règles de normalisation PURS, sans
// Qt ni dépendance à MainWindow, donc testables sans fenêtre. MainWindow en
// garde l'unique écrivain (`setSelection`) mais ne porte plus les règles.

#include <functional>
#include <optional>
#include <vector>

#include "openstitch/core/ids.hpp"

namespace openstitch::desktop {

// État de sélection complet. `objects` : objets vectoriels dans l'ordre de
// sélection, le DERNIER est le principal.
struct Selection {
    std::optional<RegionId> region; // région ACTIVE (la dernière cliquée)
    std::optional<ObjectId> embroidery;
    std::vector<ObjectId> objects;
    // Autres régions sélectionnées avec la région active. Vide sans région
    // active ; l'active n'y figure jamais.
    std::vector<RegionId> extraRegions;
};

using RegionExists = std::function<bool(RegionId)>;

// Normalise une demande de sélection :
//  - doublons d'objets retirés, la DERNIÈRE occurrence est gardée ;
//  - région ou broderie sélectionnée => au plus l'objet principal subsiste ;
//  - régions supplémentaires : sans doublon, ni l'active, ni région disparue
//    (`regionExists`), et vides sans région active.
[[nodiscard]] Selection normalize_selection(Selection selection, const RegionExists& regionExists);

// Cohérence d'un état déjà normalisé, sous la forme portée par MainWindow
// (`multi` vide pour 0 ou 1 objet, porté alors par `principal`) :
// multi >= 2, sans doublon, principal = dernier, aucune région ni broderie.
[[nodiscard]] bool selection_state_is_consistent(const std::optional<ObjectId>& principal,
                                                 const std::vector<ObjectId>& multi,
                                                 bool regionOrEmbroiderySelected);

} // namespace openstitch::desktop
