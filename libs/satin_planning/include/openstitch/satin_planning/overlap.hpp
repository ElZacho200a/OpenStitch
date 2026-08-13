// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "openstitch/satin_planning/region_split.hpp"

namespace openstitch::satin_planning {

// Parametres du recouvrement (phase 8 du plan SGSD, §19).
struct OverlapParams {
    // Distance de recouvrement, "petite" par construction (§19). 300 µm par
    // defaut : perceptible pour fermer un interstice de decoupe (bien plus
    // large que `cut_width`, 20 µm) sans creer de surdensite visible une
    // fois cousu.
    Micrometers overlap_distance{300};
};

// Deux regions adjacentes, chacune ELARGIE vers l'autre pour fermer
// l'interstice physique laisse par la coupe qui les a separees (§19 spec
// SGSD : "deux satins separes exactement sur une cut peuvent laisser
// apparaitre un trou physique"). `first_extended`/`second_extended` sont des
// geometries DEDIEES A LA BRODERIE, distinctes des regions structurelles
// (`SatinRegion::region`, jamais modifiees -- restent la reference pour
// toute mesure de couverture, phases 4/5/6/7).
struct RegionOverlap {
    std::size_t first_path_index{0};
    std::size_t second_path_index{0};
    geometry::PathSet first_extended;
    geometry::PathSet second_extended;
    double first_extended_area_mm2{0.0};
    double second_extended_area_mm2{0.0};
};

struct OverlapReport {
    std::vector<RegionOverlap> overlaps;  // une par paire adjacente directe (cf. limite ci-dessous)
};

// Genere, pour chaque paire de regions directement adjacentes
// (`RegionSplitReport::merge_candidates` -- reutilise TEL QUEL : c'est deja
// exactement l'ensemble des paires dont on connait la geometrie exacte
// d'avant coupe, cf. phase 7) une extension de chacune vers l'autre.
//
// Methode : dilate chaque region de `overlap_distance` (`geometry::inset_path_set`
// avec un delta negatif) puis la RECADRE dans la geometrie exacte d'avant
// coupe (`MergeCandidate::merged_region`, sans interstice) -- garantit que
// l'extension "reste dans la shape originale" (§19) sans avoir a reconstruire
// une ligne de coupe deplacee. En cas d'echec de la dilatation ou du
// recadrage (region degeneree), l'extension retombe sur la region non
// modifiee plutot que d'echouer silencieusement.
//
// **Limite connue** : ne couvre que les paires DIRECTEMENT adjacentes dont
// les deux cotes sont restes des feuilles jusqu'a la fin du decoupage (meme
// restriction que `merge_candidates`, phase 7). Une couture plus profonde
// dans l'arbre de decoupage (un cote redecoupe ensuite) n'est pas encore
// couverte -- necessiterait de determiner l'adjacence finale au-dela d'une
// seule coupe, hors perimetre de cette phase.
[[nodiscard]] OverlapReport generate_overlaps(const RegionSplitReport& split, const OverlapParams& params = {});

[[nodiscard]] std::string format_overlap_report(const OverlapReport& report);

}  // namespace openstitch::satin_planning
