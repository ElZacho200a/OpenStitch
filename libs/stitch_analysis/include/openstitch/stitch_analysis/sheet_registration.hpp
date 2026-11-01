// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "openstitch/stitch_analysis/sheet_deformation.hpp"

namespace openstitch::stitch_analysis {

// Repérage entre couleurs : le solveur de la feuille tissu (sheet_deformation.hpp) est rejoué dans
// l'ordre de couture, un état par bloc de couleur (séquence coupée aux ColorChange / Stop).
//
// Modèle (quasi statique, linéaire par bloc) : à l'instant où le bloc k est cousu, le tissu est
// dans l'état s_k = (1 - f) U_{k-1} + f U_k, où U_j est le déplacement du tissu une fois les
// blocs 1..j cousus et f = `own_block_fraction` (le bloc tire sur le tissu pendant qu'il se coud).
// L'aiguille pique au point nominal P ; le fil reste solidaire du tissu, qui finit dans l'état
// U_final. Le point atterrit donc en P + e_k(P) avec e_k = U_final - s_k : l'écart d'un bloc à sa
// position nominale. Deux couleurs voisines sont décalées l'une par rapport à l'autre de e_a - e_b
// = s_b - s_a. Le dernier bloc ne bouge presque pas ; les premiers sont entraînés par tout ce qui
// suit.
//
// Même statut que le solveur : estimation NON calibrée (`calibrated` faux), ordres de grandeur
// empiriques, plis et superposition des couches non simulés. Déterministe, lecture seule.

struct RegistrationParams {
    // Part du tirage du bloc déjà exercée « pendant » sa couture (0 = état avant, 1 = état après).
    double own_block_fraction{0.5};
    // Décalage entre deux couleurs voisines au-delà duquel il se voit à l'oeil (mm). Empirique.
    double visible_gap_mm{0.3};
    // Nombre maximal d'états résolus ; au-delà, des blocs consécutifs sont regroupés.
    int max_blocks{64};
};

struct ColorBlock {
    std::size_t first_command{0};
    std::size_t end_command{0}; // exclu
    std::size_t stitch_count{0};
    ObjectId first_object{};
    // Écart moyen / maximal des points du bloc par rapport à leur position nominale (mm).
    double mean_offset_x_mm{0.0};
    double mean_offset_y_mm{0.0};
    double max_offset_mm{0.0};
};

struct SheetRegistration {
    SheetDeformation final_state; // grille et déplacement final (tous les blocs cousus)
    std::vector<ColorBlock> blocks;
    // Écart (atterrissage - nominal) de chaque commande, µm, aligné sur la séquence d'entrée.
    std::vector<Vec2um> command_offset_um;
    // Par élément (columns × rows) : nombre de blocs présents (plafonné à 255) et plus grand
    // décalage entre deux d'entre eux, en mm (0 si moins de deux blocs).
    std::vector<std::uint8_t> block_count;
    std::vector<float> gap_mm;

    double max_gap_mm{0.0};
    Vec2um max_gap_at{};
    double gap_area_mm2{0.0}; // éléments dont le décalage >= visible_gap_mm
    double max_offset_mm{0.0};
    double visible_gap_mm{0.0};
    bool calibrated{false};

    [[nodiscard]] bool empty() const { return final_state.empty() || blocks.empty(); }
};

// Sans point cousu ou domaine dégénéré, le résultat est vide.
[[nodiscard]] SheetRegistration measure_registration(const stitch::StitchSequence& sequence,
                                                     const SheetParams& sheet,
                                                     const RegistrationParams& registration,
                                                     const SheetDomain& domain, Micrometers cell,
                                                     int max_cells_per_side = 128);

// Contre-compensation : décale chaque commande de -strength × son écart prévu pour que les points
// atterrissent sur leur position nominale. Point fixe par itérations : C <- N - strength × e(C).
struct RegistrationCompensation {
    stitch::StitchSequence sequence;
    int iterations{0};
    // Plus grand écart à la position nominale, mm : avant compensation, puis prévu après.
    double max_offset_before_mm{0.0};
    double max_offset_after_mm{0.0};
    // Même chose pour le décalage entre couleurs voisines.
    double max_gap_before_mm{0.0};
    double max_gap_after_mm{0.0};
    bool calibrated{false};
};

[[nodiscard]] RegistrationCompensation
compensate_registration(const stitch::StitchSequence& sequence, const SheetParams& sheet,
                        const RegistrationParams& registration, const SheetDomain& domain,
                        Micrometers cell, double strength = 1.0, int iterations = 2,
                        int max_cells_per_side = 128);

// Carte RGBA (un élément = un pixel, ligne 0 = haut) du décalage entre couleurs : transparent là où
// il n'y a qu'un bloc, bleu -> vert -> jaune -> rouge jusqu'à `scale_mm`.
[[nodiscard]] SheetRaster rasterize_registration_gap(const SheetRegistration& registration,
                                                     double scale_mm);

} // namespace openstitch::stitch_analysis
