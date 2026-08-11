// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/document/vector_object.hpp"
#include "openstitch/segmentation/segmentation.hpp"

namespace openstitch::autodigitize {

struct AutoOptions {
    Millimeters mm_per_px{25.4 / 96.0};
    Micrometers simplify_tolerance{200};
    // Largeur moyenne max pour proposer un satin (au-delà : tatami).
    Micrometers satin_max_width{6'000};
    // Aire min (mm²) pour un remplissage ; en dessous, simple contour.
    double min_fill_area_mm2{4.0};
    // Ignore le FOND présumé : la couleur du plus gros morceau segmenté, et
    // TOUTE autre région de cette même couleur exacte (pas seulement ce
    // morceau) -- un fond peut se fragmenter en plusieurs régions disjointes
    // de la même couleur (ex. les zones hors d'un motif rond inscrit dans une
    // image carrée). Particulièrement utile sur une image SANS canal alpha :
    // sans transparence, le fond devient une région opaque comme les autres.
    bool skip_largest_region{false};
    // Moteur topologique : squelette, decomposition en sections ouvertes,
    // barreaux et routage par source commune. Active par defaut.
    bool use_auto_satin{true};
    // Satin automatique NAÏF (rails_from_contour) : désactivé par défaut. Ses
    // deux rails « bouts les plus éloignés » débordent sur les formes concaves
    // ou branchues (les rungs enjambent les creux). Tant que le vrai moteur
    // auto-satin (squelette) ne génère pas la géométrie, on remplit ces zones
    // en tatami — découpé proprement sur la région, donc sans débordement.
    bool use_naive_satin{false};
};

// Objets produits par l'autonumérisation : toujours ÉDITABLES (§13). Le type
// de chaque objet est choisi par la forme de sa région (satin pour les bandes
// fines, tatami pour les zones pleines, contour pour les petites régions).
struct AutoResult {
    std::vector<document::VectorObject> vectors;
    std::vector<document::EmbroideryObject> embroideries;
};

// Construit les objets à partir d'une segmentation. Les identifiants sont
// alloués via `ids` (le générateur du document), donc jamais réutilisés.
[[nodiscard]] Result<AutoResult> auto_digitize(const segmentation::Segmentation& seg,
                                               IdGenerator<ObjectId>& ids,
                                               const AutoOptions& options);

// Convertit une colonne squelette (`auto_satin::SatinColumnGeometry`, mode
// Legacy, OU `auto_satin::ParametricSatinObject`, mode Parametric — mêmes
// noms de champs rail_a/rail_b/rungs/section_*/*_junction, cf.
// libs/auto_satin/include/openstitch/auto_satin/satin_column.hpp) en
// `document::SatinParams`. Point de conversion UNIQUE partagé par
// l'autonumérisation (ci-dessus) et les créations satin manuelles
// (apps/desktop) — `stitch_generation::fill_satin_columns` aplatit
// `rail_a`/`rail_b` (Bézier ou polyligne dense indifféremment) à la demande,
// donc un seul point d'entrée suffit pour les deux modes.
template <typename ColumnLike>
[[nodiscard]] document::SatinParams satin_params_from_column(const ColumnLike& col, Micrometers density,
                                                              Micrometers pull_compensation,
                                                              bool center_underlay,
                                                              Micrometers max_width) {
    document::SatinParams sp;
    sp.rail_a = col.rail_a;
    sp.rail_b = col.rail_b;
    sp.density = density;
    sp.pull_compensation = pull_compensation;
    sp.center_underlay = center_underlay;
    sp.max_width = max_width;
    sp.rungs.reserve(col.rungs.size());
    for (const auto& rung : col.rungs) {
        sp.rungs.push_back(document::SatinRung{rung.a, rung.b});
    }
    if (col.section_count > 1 || col.start_junction || col.end_junction) {
        sp.topology = document::SatinSectionTopology{col.section_index, col.section_count,
                                                     col.start_junction, col.end_junction};
    }
    return sp;
}

}  // namespace openstitch::autodigitize
