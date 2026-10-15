// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::stitch_analysis {

// Couverture PAR OBJET : compare la région d'origine de chaque objet de broderie à la zone
// que ses propres points balaient réellement. Complète `project_metrics`, dont
// `uncovered_ratio` est global et pixel-based contre la segmentation.
struct ObjectCoverageOptions {
    // Largeur de la trace d'un point : MÊME règle que `ProjectMetricsOptions::coverage_width`
    // (une cellule est couverte si son centre est à moins de la moitié de cette largeur d'un
    // segment cousu).
    Micrometers coverage_width{500};
    // Pas de la grille de mesure (discrétisation, pas un critère qualité) : la moitié de la
    // largeur de trace, pour que la trace couvre au moins deux cellules.
    Micrometers cell{250};
    // Plafond de cellules par objet : au-delà, le pas est multiplié par 2 (déterministe)
    // jusqu'à tenir. Borne mémoire/temps sur les très grands objets.
    std::size_t max_cells{4'000'000};
    // Tolérance d'aplatissement des courbes de Bézier : celle de la génération des remplissages.
    Micrometers flatten_tolerance{50};
};

struct ObjectCoverage {
    ObjectId object;        // objet de broderie
    ObjectId source_vector; // objet vectoriel suivi
    double region_area_mm2{0.0};
    // Part (0..1) de la région balayée par les points de CET objet.
    double covered_ratio{0.0};
    // Part (0..1) de la surface cousue (trace des points) qui sort de la région. Inclut la
    // demi-largeur de trace : un remplissage rentré de `inset` < coverage_width/2 déborde
    // faiblement par construction.
    double overflow_ratio{0.0};
    bool measured{false};
    std::string reason; // si !measured : pourquoi (« région vide », « aucun point »…)
};

// Un élément par objet de broderie VISIBLE, dans l'ordre du document.
//
// Région : contour vectoriel source (`PathSet`, trous compris, courbes aplaties) pour le
// tatami, le remplissage directionnel et l'auto-satin ; pour un satin à rails, le polygone
// formé par ses deux rails (c'est sa zone propre, le vecteur n'en est que le contour source).
// Un contour (running) n'a pas de couverture : `measured = false`.
//
// Points : commandes `Stitch` de la séquence dont `source == object.id` et dont la passe est
// visible (`TopStitch`, ou `Manual` après retouche), reliées par paires consécutives. Les
// sous-couches (`Underlay`), liaisons (`Travel`) et points d'arrêt (`Lock`) ne comptent pas :
// ils ne forment pas la surface visible.
//
// Objets partageant un même vecteur (auto-satin/tatami multiples) : chaque objet est mesuré
// contre la région complète du vecteur, donc un objet qui n'en couvre qu'une partie ne peut
// pas atteindre 1. Les satins à rails n'ont pas ce biais (région = leurs propres rails).
[[nodiscard]] std::vector<ObjectCoverage>
object_coverage(const document::Project& project, const stitch::StitchSequence& sequence,
                const ObjectCoverageOptions& options = {});

} // namespace openstitch::stitch_analysis
