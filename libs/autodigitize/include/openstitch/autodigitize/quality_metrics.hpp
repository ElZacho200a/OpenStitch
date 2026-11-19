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

// --- Satin guidé sur formes larges (HP-ENG-009) ---
// Métrique d'un « champ de fils » : un ensemble de traversées (segments bord à bord) d'une région,
// qu'il vienne du satin guidé ou des rangées d'un tatami à angle fixe. Géométrique, déterministe.
struct ThreadFieldMetrics {
    std::size_t thread_count{0};
    // Désalignement moyen des fils avec la normale du bord à leurs deux extrémités :
    // |cos| de l'angle fil / tangente du bord. 0 = fil perpendiculaire au bord (bord net, comme un
    // satin), 1 = fil tangent au bord (bord crénelé).
    double edge_misalignment{0.0};
    // Part des extrémités où le fil rase le bord (angle fil / bord < 30°).
    double glancing_rate{0.0};
    // Régularité de la direction : écart d'angle maximal et moyen entre deux fils consécutifs d'une
    // même colonne (degrés, modulo 180). 0 pour des rangées parallèles.
    double max_turn_deg{0.0};
    double mean_turn_deg{0.0};
    // Paires de fils voisins qui se croisent (rails croisés). Doit rester 0.
    std::size_t crossings{0};
    // Longueur du fil le plus long (µm).
    double max_thread_um{0.0};
    // Densité : plus petit et plus grand écart (µm) entre les extrémités de deux fils consécutifs
    // d'une colonne, côté intérieur d'un anneau comme côté extérieur. Un satin tournant serre ses
    // fils côté concave et les écarte côté convexe ; hors de [160 ; 700] µm (espacement nominal
    // 400 µm), la densité n'est plus conforme (casse de fil / jour visible).
    double min_pitch_um{0.0};
    double max_pitch_um{0.0};
};

// Mesure un champ de traversées. `region` est aplatie en polygone (tolérance 50 µm) ; les fils sont
// des couples d'extrémités (µm), `column_starts` indique l'indice où commence chaque colonne
// (une direction n'est comparée qu'à l'intérieur d'une colonne). Au plus ~400 fils servent à la
// mesure d'alignement (échantillon régulier).
[[nodiscard]] ThreadFieldMetrics
measure_thread_field(const geometry::PathSet& region,
                     const std::vector<std::pair<Vec2um, Vec2um>>& threads,
                     const std::vector<std::size_t>& column_starts);

// Champ de fils du REPLI : rangées parallèles d'un tatami (pas `spacing`, angle `angle`).
[[nodiscard]] std::vector<std::pair<Vec2um, Vec2um>> fill_threads(const geometry::PathSet& region,
                                                                  Angle angle, Micrometers spacing);

// Angle naturel du tatami d'auto-numérisation (celui de `configure_tatami_fills`) : axe principal
// si la forme est anisotrope, sinon `isotropic_fill_angle`.
[[nodiscard]] Angle natural_fill_angle(const geometry::PathSet& region, const AutoOptions& options);

// Verdict de la comparaison satin guidé / tatami sur une région large.
struct WideSatinPlan {
    // Ligne centrale proposée (vide : axe automatique du moteur guidé, droit).
    std::vector<Vec2um> guide;
    double coverage{0.0}; // couverture du satin guidé
    std::size_t columns{0};
    ThreadFieldMetrics guided;
    ThreadFieldMetrics fill; // repli : tatami à son angle naturel
    // Vrai si le satin guidé bat le repli : couverture >= `satin_min_coverage`, aucun croisement,
    // au plus 2 colonnes (coupes), direction régulière (tour max <= 12°), fils pas plus longs que
    // le maximum d'un satin (`satin_compact_max_width`, ou `satin_max_width` s'il est plus grand),
    // densité conforme (écart entre fils dans [160 ; 700] µm), et bords nettement plus
    // francs (désalignement <= 70 % du tatami ET fils rasants pas plus fréquents).
    bool better{false};
};

// Construit la ligne centrale automatique (anneau : contour extérieur rentré de la demi-largeur
// moyenne ; sans trou : vide = axe principal), génère le satin guidé et le compare au tatami.
// `std::nullopt` : région dégénérée ou génération impossible.
[[nodiscard]] std::optional<WideSatinPlan> plan_wide_guided_satin(const geometry::PathSet& region,
                                                                  const AutoOptions& options);

// Métriques de qualité du CHOIX de type de point (HP-AUTO-001 / HP-AUTO-009), calculées sur le
// résultat d'une auto-numérisation. Purement géométriques et déterministes (aucun point généré).
struct AutoQualityReport {
    std::size_t object_count{0};
    std::size_t satin_count{0};         // auto-satin (bandes + compacts)
    std::size_t compact_satin_count{0}; // dont formes compactes (cordes parallèles)
    std::size_t guided_satin_count{0};  // dont satin guidé de forme large (HP-ENG-009)
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
