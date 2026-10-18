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
[[nodiscard]] std::vector<FillStitch>
fill_directional(const geometry::PathSet& region, const document::DirectionalFillParams& params);

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
directional_field_at(const geometry::PathSet& region, const document::DirectionalFillParams& params,
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
// Assistant de guides : construit des courbes guides ouvertes depuis l'axe
// medial de la region (pipeline auto-satin : rasterisation, Zhang-Suen,
// SkeletonGraph). Le chemin le plus long donne le guide principal ; chaque
// branche significative restante (>= min_path_length) recoit son propre guide,
// ancre sur le squelette deja couvert, jusqu'a max_guides. Ne genere aucun
// satin ; le resultat va directement dans DirectionalFillParams::guides.
struct DirectionalGuideOptions {
    Micrometers min_path_length{1'500};
    Micrometers node_spacing{2'500};
    std::size_t max_guides{8};
};

[[nodiscard]] std::vector<geometry::Path>
directional_guides_from_region(const geometry::PathSet& region,
                               const DirectionalGuideOptions& options = {});

// Dégradé de densité couvrant toute la forme : l'axe est parallèle à
// `direction` (radians, repère Y vers le haut), centré sur la boîte englobante
// et d'une longueur égale à l'étendue de la forme dans cette direction (nœuds
// des contours extérieurs). nullopt pour une forme vide ou d'étendue nulle.
[[nodiscard]] std::optional<document::DensityGradient>
density_gradient_across(const std::vector<geometry::PathSet>& shape, Angle direction,
                        Micrometers spacing_from, Micrometers spacing_to);

// Fondu entre deux zones voisines : axe de dégradé du centre de la boîte
// englobante de `zone_a` vers celui de `zone_b`, couvrant l'étendue des deux
// zones le long de cet axe. `spacing_a` (côté A) et `spacing_b` (côté B) sont
// les écarts entre lignes du remplissage de la couleur B : large côté A, serré
// côté B pour que B domine progressivement. nullopt si une zone est vide ou si
// les centres sont confondus.
[[nodiscard]] std::optional<document::DensityGradient>
density_gradient_between(const std::vector<geometry::PathSet>& zone_a,
                         const std::vector<geometry::PathSet>& zone_b, Micrometers spacing_a,
                         Micrometers spacing_b);

// Restreint la rampe d'un dégradé à la portion [start ; end] de son axe
// (fractions de from→to, 0 = from, 1 = to). Avant `start` l'écart reste
// `spacing_from`, après `end` il reste `spacing_to` : le générateur borne la
// position sur l'axe, donc les extrémités deviennent des aplats. Les autres
// champs sont conservés. nullopt si une fraction est non finie ou hors de
// [0 ; 1], ou si start >= end.
[[nodiscard]] std::optional<document::DensityGradient>
narrow_gradient_zone(const document::DensityGradient& gradient, double start, double end);

// Fondu de couleurs : les lignes ne peuvent espacer leur écart que
// perpendiculairement à elles-mêmes. Si `params.density_gradient` a un axe non
// nul, remplace les guides par UN guide droit perpendiculaire à cet axe
// (traversant la boîte englobante de `shape` par son centre) ; sinon ne change
// rien. Les lignes de rupture sont conservées.
void align_guides_across_density_gradient(document::DirectionalFillParams& params,
                                          const std::vector<geometry::PathSet>& shape);

[[nodiscard]] document::DirectionalFillParams
directional_from_tatami(const document::TatamiParams& tatami,
                        const std::vector<geometry::PathSet>& shape, std::uint32_t seed);

} // namespace openstitch::stitch_generation
