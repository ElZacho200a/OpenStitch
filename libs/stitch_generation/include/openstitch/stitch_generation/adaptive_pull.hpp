// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <vector>

namespace openstitch::stitch_generation {

// AdaptivePullCompensation, NIVEAU 1 (compensation GÉOMÉTRIQUE, `PullMode::Adaptive`).
//
// Remplace, en mode adaptatif seulement, la compensation uniforme (largeur moyenne) par un
// calcul LOCAL le long de la colonne de satin. Fonctions pures et déterministes ; aucune mesure
// physique n'est incorporée : les coefficients par défaut sont des RÈGLES EMPIRIQUES non
// validées sur machine (le niveau 2, textile, et le niveau 3, prédiction, sont d'autres couches).
// `scale` est le point d'entrée d'un profil textile / d'une calibration (1 = aucun effet).
// Le mode `Auto` historique (`pull_auto.hpp`) reste strictement inchangé.

// Modèle de la compensation locale d'un satin, par côté de colonne :
//   C(s) = clamp(a + b · largeur(s), min, max) · scale · f_extrémité · f_jonction · f_pente
// puis lissage spatial (fenêtre triangulaire, jamais à travers un saut), puis bornes de
// sécurité géométriques (fraction de la largeur, rayon intérieur des virages).
struct AdaptiveSatinModel {
    double a_mm{0.05}; // compensation à largeur nulle
    double b{0.04};    // mm de compensation par mm de largeur
    double min_mm{0.08};
    double max_mm{0.40};
    double scale{1.0}; // facteur textile / calibration (1 = neutre)

    double end_taper_mm{1.5}; // distance de bout sur laquelle l'effet d'extrémité s'étale
    double end_factor{0.6};   // facteur appliqué au tout bout de la colonne
    double junction_mm{1.0};  // distance à un saut/une jonction sur laquelle l'effet s'étale
    double junction_factor{0.7};
    double slope_gain{2.0};          // atténuation par pente de largeur (mm/mm)
    double smooth_mm{1.5};           // rayon du lissage le long de l'axe
    double max_width_fraction{0.20}; // une compensation ne dépasse jamais cette part de la largeur
    double inner_radius_fraction{0.4}; // part du rayon intérieur d'un virage qu'elle peut consommer
    double hard_max_mm{0.60};          // plafond absolu par côté, quelles que soient les entrées
};

// Station d'entrée (µm, mêmes unités que le moteur satin) : couple de points rail A / rail B.
struct SatinPullInput {
    double ax{0.0}, ay{0.0}, bx{0.0}, by{0.0};
    bool jump_before{false}; // station atteinte par un saut : coupe la propagation du lissage
};

// Drapeaux de diagnostic d'une station (combinables).
enum SatinPullFlag : std::uint32_t {
    kPullEndReduced = 1u << 0,      // facteur d'extrémité actif
    kPullJunctionReduced = 1u << 1, // facteur de jonction actif
    kPullSlopeReduced = 1u << 2,    // variation de largeur atténuante
    kPullCurvatureCapped = 1u << 3, // côté intérieur d'un virage limité par le rayon
    kPullWidthCapped = 1u << 4,     // limité par la fraction de la largeur
};

// Résultat par station : offsets EFFECTIFS par côté en µm (rail A = « left », rail B = « right »)
// et grandeurs de diagnostic en mm (affichage uniquement).
struct SatinPullSample {
    double left_um{0.0};
    double right_um{0.0};
    double arc_mm{0.0};   // abscisse curviligne de l'axe
    double width_mm{0.0}; // largeur locale (avant compensation)
    double base_mm{0.0};  // a + b·largeur borné, avant modulations
    double curvature_per_mm{0.0};
    std::uint32_t flags{0};
};

// Calcule la compensation de chaque station. `max_per_side_um` : borne dure d'appel (la borne
// `pull_max` du satin). Sortie de même taille que `stations` ; vide pour une entrée vide.
[[nodiscard]] std::vector<SatinPullSample>
adaptive_satin_pull(const std::vector<SatinPullInput>& stations, const AdaptiveSatinModel& model,
                    double max_per_side_um);

// --- Remplissages (tatami, directionnel) : dépassement de bout de rangée -----------------------
//
//   P = clamp(a + b · min(longueur, plafond), min, max) · scale · f_densité · f_pointe · f_courbure
//
// puis bornes : jamais plus de `max_row_fraction` de la longueur de la rangée par bout, et (côté
// appelant) jamais plus de la moitié d'un intervalle libre entre deux segments d'une même rangée
// (trou, étranglement), pour qu'aucune pénétration n'entre dans un trou ni ne croise sa voisine.
struct AdaptiveFillModel {
    double a_mm{0.12};
    double b{0.012};            // mm par mm de longueur de rangée
    double length_cap_mm{20.0}; // au-delà, l'effet sature
    double min_mm{0.08};
    double max_mm{0.40};
    double scale{1.0}; // facteur textile / calibration (1 = neutre)
    double reference_spacing_mm{0.4};
    double tip_overhang_rows{2.0}; // bout dépassant ses voisines de plus de N écarts = pointe
    double tip_factor{0.5};
    double curvature_gain{0.5}; // atténuation par radian de virage cumulé de la ligne
    double max_row_fraction{0.15};
    double hard_max_mm{0.60};
};

// Contexte d'un bout de rangée (µm / radians).
struct FillEndContext {
    double row_length_um{0.0};
    double spacing_um{400.0};
    double overhang_um{0.0}; // saillie du bout au-delà des rangées voisines (0 si inconnue)
    double turning_rad{0.0}; // virage cumulé de la ligne (directionnel ; 0 pour le tatami)
};

enum FillPullFlag : std::uint32_t {
    kFillTip = 1u << 0,       // pointe fine : compensation réduite
    kFillRowCapped = 1u << 1, // limitée par la fraction de la longueur de rangée
    kFillGapCapped = 1u << 2, // limitée par l'intervalle libre (appliqué par l'appelant)
};

// Compensation d'un bout, en µm (>= 0). `flags` (optionnel) reçoit les drapeaux de diagnostic.
[[nodiscard]] double adaptive_fill_end_pull(const FillEndContext& ctx,
                                            const AdaptiveFillModel& model,
                                            std::uint32_t* flags = nullptr);

} // namespace openstitch::stitch_generation
