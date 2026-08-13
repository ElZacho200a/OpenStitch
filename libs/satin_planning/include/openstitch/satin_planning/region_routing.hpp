// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "openstitch/auto_satin/satin_column.hpp"
#include "openstitch/satin_planning/region_split.hpp"
#include "openstitch/stitch_generation/routing.hpp"

namespace openstitch::satin_planning {

// Parametres du routage (phase 9 du plan SGSD, §20 : continuite directe /
// travel run / jump -- le trim est une consequence du choix `Jump`, jamais
// une contrainte imposee au decoupage ou a la generation).
struct RegionRoutingParams {
    auto_satin::SatinColumnsParameters genParams{};
    stitch_generation::RoutingConfig routingConfig{};
    Vec2um origin{};  // point de depart du routage (ex. origine du cadre)
};

// Une SatinRegion resolue (phase 3/6/7), reellement construite en colonne
// satin (auto_satin::build_satin_columns) et reduite a ses deux extremites
// pour le moteur de routage existant.
struct RoutedRegion {
    std::size_t path_index{0};
    bool build_succeeded{false};
    stitch_generation::RouteColumn column;  // valide seulement si build_succeeded
};

struct RegionRoutingReport {
    std::vector<RoutedRegion> regions;  // une par SatinRegion de RegionSplitReport::regions, meme ordre
    stitch_generation::RoutePlan plan;  // routage des seules regions dont build_succeeded == true
};

// Construit reellement une colonne satin par SatinRegion resolue (meme
// conversion que l'oracle phase 5), la reduit a ses deux extremites
// (centres des barreaux d'about, cf. `stitch_generation::RouteColumn`) et
// route l'ensemble avec le moteur de routage EXISTANT (Lot 6,
// `stitch_generation::route_columns`) -- ce module ne reimplemente RIEN du
// routage lui-meme, seulement le pont entre les regions SGSD et ce moteur
// deja teste.
//
// **Limite connue** : `RouteColumn::start_junction`/`end_junction` ne sont
// PAS renseignes. Chaque region est reanalysee independamment par
// `build_satin_columns`, avec sa PROPRE numerotation locale de jonctions
// (via un nouvel appel a `analyze_region` en interne), non comparable entre
// regions -- contrairement au cas normal ou toutes les colonnes routees
// proviennent d'une seule region non decomposee et partagent donc un meme
// graphe de jonctions. `route_columns` retombe alors sur son seuil de
// quasi-contact le plus strict (`underpath_max_without_junction`) pour
// TOUTES les liaisons entre regions SGSD : sur (jamais de trajet cache
// injustifie), mais plus conservateur qu'il ne pourrait l'etre. Relier la
// numerotation de jonctions a travers les regions (pour permettre des
// liaisons `Underpath` a la portee normale `underpath_max` la ou l'adjacence
// est deja connue via `RegionSplitReport::merge_candidates`) reste un
// travail futur.
[[nodiscard]] RegionRoutingReport route_regions(const RegionSplitReport& split,
                                                 const RegionRoutingParams& params = {});

[[nodiscard]] std::string format_region_routing_report(const RegionRoutingReport& report);

}  // namespace openstitch::satin_planning
