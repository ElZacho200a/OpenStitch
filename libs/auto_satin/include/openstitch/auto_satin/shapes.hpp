// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>

#include "openstitch/geometry/path.hpp"

namespace openstitch::auto_satin {

// Corpus de formes procédurales pour le diagnostic et les tests (§29).
// Noms : rectangle, capsule, ribbon, s, y, t, cross, h, circle, ring,
// wide, tiny, notch, pinch, trident. Corpus de torture (mission de
// durcissement du contrat SatinPlanner, 2026-08-17, §9-14), formes
// délibérément difficiles : star5, asymmetric_star, comb, E, deep_recursive,
// multi_neck, dumbbell, deep_channel, two_holes, ring_branch,
// junction_with_hole, polygonal_cut_fixture. e_trunk_isolated : tronc en
// « ] » de `E` (montant + barre haute + barre basse, sans la barre du
// milieu), isolé le 2026-08-30 pour reproduire le défaut de coude à 90°
// indépendamment de toute jonction (HP-STI-018, Phase B). Régression squelette
// (2026-08-21) : thick_diagonal_blob (géométrie exacte d'une région
// utilisateur — escalier de squelette à 2 px de large ayant fait échouer
// la trace d'arête sans retour arrière, cf. skeleton_graph.cpp). Renvoie
// nullopt si le nom est inconnu.
[[nodiscard]] std::optional<geometry::PathSet> make_shape(const std::string& name);

} // namespace openstitch::auto_satin
