// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "openstitch/core/units.hpp"
#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/stitch_generation/tatami.hpp"

namespace openstitch::stitch_generation {

// Remplissage DIRECTIONNEL (passé empiétant / peinture à l'aiguille, cf.
// docs/source/directional-fill.md). Chaîne complète, pure et déterministe :
//
// 1. SECTEURS : la région est découpée par les lignes de rupture
//    (`break_lines`) ; chaque secteur est traité indépendamment (champ,
//    lignes, parcours), avec un chevauchement `sector_overlap` le long des
//    ruptures pour éviter les interstices.
// 2. CHAMP : en chaque point, l'orientation du fil (modulo 180°) est
//    interpolée par pondération inverse au carré de la distance à chaque
//    guide du secteur, sur l'angle DOUBLÉ (une tangente et son opposée
//    s'additionnent au lieu de s'annuler). La tangente du bord le plus proche
//    s'y mélange avec le poids `edge_weight`. Sans guide : axe principal du
//    secteur. Le champ est échantillonné sur une grille puis interpolé
//    bilinéairement (toujours sur l'angle doublé).
// 3. LIGNES DE COURANT : algorithme de Jobard & Lefer (1997), distance de
//    séparation = `row_spacing`, distance de test = 0,5 × `row_spacing`.
//    Une ligne s'arrête hors de la région, dans un trou, ou trop près d'une
//    autre ligne — c'est ce qui garde une densité homogène là où les
//    directions convergent. Un balayage final réensemence toute zone restée
//    vide.
// 4. POINTS : chaque ligne est découpée à la longueur cible (bornée à
//    [1 ; 7] mm, aucun point < 0,5 mm), pénétrations décalées d'une ligne à
//    l'autre (`stagger`) ; aspect « fait main » optionnel (Phase 3).
// 5. PARCOURS : enchaînement glouton des extrémités les plus proches (une
//    ligne sur deux parcourue en sens inverse, voisines regroupées) ; liaison
//    cousue si courte et intérieure, sinon trajet caché direct ou le long du
//    bord rentré (`hidden_underpath`), sinon saut.

// Une ligne de courant tracée dans un secteur (échantillonnage dense, pas
// ≈ 0,2 × espacement), exposée pour les tests et le diagnostic.
struct DirectionalStreamline {
    std::vector<Vec2um> points;
    std::size_t sector{0};
};

// Lignes de courant de tous les secteurs, dans l'ordre de traçage.
[[nodiscard]] std::vector<DirectionalStreamline>
trace_directional_streamlines(const geometry::PathSet& region,
                              const document::DirectionalFillParams& params);

// Pénétrations de chaque ligne de courant retenue (avant enchaînement) : une
// polyligne cousue par ligne, dans le sens de traçage. Exposé pour les tests
// (longueurs de points, respect du champ).
[[nodiscard]] std::vector<std::vector<Vec2um>>
directional_stitch_lines(const geometry::PathSet& region,
                         const document::DirectionalFillParams& params);

// Remplissage complet (couche supérieure), même vocabulaire que le tatami
// (`FillStitch` : point cousu, saut, ou déplacement cousu caché).
[[nodiscard]] std::vector<FillStitch> fill_directional(const geometry::PathSet& region,
                                                       const document::DirectionalFillParams& params);

// Sous-couches : réutilise `tatami_underlay` (contour rentré + rangées
// droites), les rangées étant perpendiculaires à la direction MOYENNE du
// champ. Vide si aucune sous-couche n'est activée.
[[nodiscard]] std::vector<std::vector<Vec2um>>
directional_underlay(const geometry::PathSet& region,
                     const document::DirectionalFillParams& params);

// Orientation du fil (dans [0, pi), repère Y vers le haut) aux points
// demandés — perturbation « fait main » comprise. nullopt pour un point hors
// de tous les secteurs. Exposé pour les tests et l'aperçu.
[[nodiscard]] std::vector<std::optional<Angle>>
directional_field_at(const geometry::PathSet& region,
                     const document::DirectionalFillParams& params,
                     const std::vector<Vec2um>& points);

// Aperçu du champ : un échantillon (position, orientation) tous les `step`
// µm à l'intérieur de la région — l'interface en tire de petits traits de
// direction avant génération. Déterministe (balayage ligne par ligne).
struct DirectionTick {
    Vec2um pos{};
    Angle angle{};
};
[[nodiscard]] std::vector<DirectionTick>
directional_field_preview(const geometry::PathSet& region,
                          const document::DirectionalFillParams& params, Micrometers step);

// Paramètres directionnels équivalents à un tatami existant (conversion
// depuis le panneau de propriétés) : mêmes densité, longueur, retrait,
// décalage et sous-couches ; UN guide droit à l'angle du tatami, traversant
// la forme par le centre de sa boîte englobante, pour que le résultat
// initial reproduise l'orientation du tatami. `seed` fixe la graine de
// l'aspect fait main (typiquement l'id de l'objet).
[[nodiscard]] document::DirectionalFillParams
directional_from_tatami(const document::TatamiParams& tatami,
                        const std::vector<geometry::PathSet>& shape, std::uint32_t seed);

} // namespace openstitch::stitch_generation
