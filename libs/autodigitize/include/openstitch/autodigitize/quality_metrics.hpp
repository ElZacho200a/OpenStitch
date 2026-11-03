// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "openstitch/autodigitize/autodigitize.hpp"

namespace openstitch::autodigitize {

// Mesure géométrique d'un morceau (le plus grand sous-chemin d'une région) : la même que celle qui
// décide du satin (`choose_satin`), exposée pour que le choix et sa métrique ne divergent jamais.
// `std::nullopt` : analyse impossible (géométrie dégénérée).
struct BandShape {
    double length_um{0.0};     // périmètre total / 2 (une bande a deux bords de longueur ~L)
    double mean_width_um{0.0}; // aire / L
    double max_width_um{0.0};  // diamètre inscrit maximal
    double min_satin_width_um{0.0};
    double min_elongation{0.0};
    // Allongée et étroite : candidate au satin par squelette (largeur moyenne >= min_satin_width,
    // largeur max <= `satin_max_width`, longueur >= min_elongation x largeur max).
    bool band_like{false};
};

[[nodiscard]] std::optional<BandShape> measure_band_shape(const geometry::PathSet& region,
                                                          const AutoOptions& options);

// Métriques de qualité du CHOIX de type de point (HP-AUTO-001 / HP-AUTO-009), calculées sur le
// résultat d'une auto-numérisation. Purement géométriques et déterministes (aucun point généré).
struct AutoQualityReport {
    std::size_t object_count{0};
    std::size_t satin_count{0};         // auto-satin (bandes + compacts)
    std::size_t compact_satin_count{0}; // dont formes compactes (cordes parallèles)
    std::size_t fill_count{0};          // tatami
    std::size_t contour_count{0};       // point droit (petites régions)
    std::size_t other_count{0};
    // Bandes fines éligibles : région >= `min_fill_area_mm2`, au plus un trou, `band_like`.
    std::size_t thin_band_count{0};
    std::size_t thin_band_satin_count{0}; // dont cousues en satin
    // thin_band_satin_count / thin_band_count ; 1 s'il n'y a aucune bande fine.
    double thin_band_satin_rate{1.0};
    // Satin plus large que son maximum (`satin_max_width` pour une bande, `satin_compact_max_width`
    // pour une forme compacte). Doit rester 0.
    std::size_t satin_over_max_count{0};
    double satin_max_width_mm{0.0}; // plus grande largeur inscrite parmi les satins
    // Aires nettes des vecteurs sources cousus, par type (mm²). Permet à l'appelant de comparer à
    // l'aire à broder pour détecter les zones oubliées.
    double satin_area_mm2{0.0};
    double fill_area_mm2{0.0};
    double contour_area_mm2{0.0};
    double embroidered_area_mm2{0.0};
};

// `vectors` : les objets vectoriels sources (typiquement `AutoResult::vectors`, ou les objets
// d'entrée de `auto_digitize_vectors`). Un objet de broderie dont la source est introuvable est
// compté par type mais sans aire.
[[nodiscard]] AutoQualityReport
measure_auto_quality(const std::vector<document::VectorObject>& vectors,
                     const std::vector<document::EmbroideryObject>& embroideries,
                     const AutoOptions& options);

} // namespace openstitch::autodigitize
