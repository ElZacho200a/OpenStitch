// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "openstitch/core/units.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::stitch_analysis {

// Déformation du tissu : solveur mécanique 2D de la feuille tissu + stabilisateur.
//
// Différence avec `stitch_generation::PullField` : le champ de tirage est un noyau analytique
// (réponse locale, sans bord). Ici la feuille est un VRAI problème aux limites : élasticité plane
// orthotrope (chaîne / trame / cisaillement), bord fixé au cadre, charges = dipôles de tension de
// chaque point cousu, raidissement local par la densité de fil. On résout K u = f par éléments
// finis Q4 et gradient conjugué préconditionné (Jacobi), puis on dérive des déformations un indice
// de plissement.
//
// Déterministe : parcours des éléments et des segments par indice croissant, un seul fil, nombre
// d'itérations borné ; même entrée -> même sortie, octet pour octet. Lecture seule, jamais stocké
// dans le document.
//
// AUCUNE MESURE PHYSIQUE n'est incorporée. Rigidités, tension du fil et déformation critique de
// plissement sont des ordres de grandeur empiriques ; `calibrated` reste faux tant qu'ils ne
// viennent pas de la pièce-test (HP-ENG-001.a, HP-PHYS-001). Le résultat est une estimation
// relative utile pour comparer deux versions d'un motif, pas une mesure. Les plis eux-mêmes
// (flambage hors plan) ne sont pas simulés : l'indice mesure la compression dans le plan par
// rapport à un seuil critique.

struct SheetParams {
    // Rigidités de membrane (module d'Young × épaisseur), en N/mm. Chaîne = direction
    // `grain_angle`.
    double stiffness_warp_n_per_mm{40.0};
    double stiffness_weft_n_per_mm{40.0};
    double poisson_warp_weft{0.25};
    // Rigidité de cisaillement dans le plan, N/mm. <= 0 : valeur isotrope déduite.
    double shear_n_per_mm{0.0};
    double grain_angle_rad{0.0};
    // Le fil raidit la feuille : facteur local = 1 + stitch_stiffening × densité (mm de fil / mm²).
    double stitch_stiffening{0.08};
    // Tension d'un point sur la feuille (N), appliquée aux deux pénétrations vers l'intérieur.
    double thread_tension_n{0.20};
    // Étalement de la charge d'une pénétration (rayon, mm) : évite la singularité d'une force
    // ponctuelle et rend le résultat indépendant de la finesse du maillage.
    double load_radius_mm{0.6};
    // Compression dans le plan (déformation) au-delà de laquelle le tissu risque de plisser.
    double critical_compressive_strain{0.002};
    bool calibrated{false};
    // Solveur.
    int max_iterations{4000};
    double tolerance{1e-8};
};

// Réglages nommés, EMPIRIQUES : « woven », « jersey », « felt », avec stabilisateur « none »,
// « tearaway », « cutaway ». Un nom inconnu rend « woven » sans stabilisateur.
[[nodiscard]] SheetParams sheet_preset(std::string_view material, std::string_view stabilizer);

struct SheetDomain {
    Vec2um min{};
    Vec2um max{};
};

// Emprise des points cousus élargie de `margin`. Vide (min == max) sans segment.
[[nodiscard]] SheetDomain domain_around_stitches(const stitch::StitchSequence& sequence,
                                                 Micrometers margin);

struct SheetDeformation {
    Vec2um origin{};     // coin bas-gauche du domaine
    Micrometers cell{0}; // côté d'un élément
    std::size_t columns{0};
    std::size_t rows{0};
    // Déplacements aux noeuds (columns+1) × (rows+1), ligne par ligne, Y vers le haut, en mm.
    std::vector<double> ux_mm;
    std::vector<double> uy_mm;
    // Par élément (columns × rows) : plus forte compression principale (>= 0, sans unité) et indice
    // de plissement = compression / déformation critique (1 = seuil atteint).
    std::vector<float> compression;
    std::vector<float> wrinkle_index;
    // Densité de fil par élément, mm / mm².
    std::vector<float> thread_density;

    std::size_t segment_count{0};
    int iterations{0};
    bool converged{false};
    double residual{0.0};

    double max_displacement_mm{0.0};
    Vec2um max_displacement_at{};
    double max_wrinkle_index{0.0};
    Vec2um max_wrinkle_at{};
    double p99_wrinkle_index{0.0};
    double wrinkle_area_mm2{0.0}; // éléments dont l'indice >= 1
    bool calibrated{false};

    [[nodiscard]] bool empty() const { return columns == 0 || rows == 0; }
    [[nodiscard]] std::size_t node_columns() const { return columns + 1; }
    [[nodiscard]] Vec2um displacement_at(std::size_t node_col, std::size_t node_row) const;
};

// `cell` : côté d'élément souhaité ; relevé si la grille dépasserait `max_cells_per_side`.
// Sans segment cousu ou domaine dégénéré, le résultat est vide.
[[nodiscard]] SheetDeformation solve_sheet_deformation(const stitch::StitchSequence& sequence,
                                                       const SheetParams& params,
                                                       const SheetDomain& domain, Micrometers cell,
                                                       int max_cells_per_side = 128);

// Carte RGBA (un élément = un pixel, ligne 0 = haut) de l'indice de plissement : transparent sans
// fil ni compression, bleu -> vert -> jaune -> rouge jusqu'à `scale` (défaut 1 = seuil critique).
struct SheetRaster {
    int width{0};
    int height{0};
    std::vector<std::uint8_t> rgba;
};
[[nodiscard]] SheetRaster rasterize_wrinkle_index(const SheetDeformation& deformation,
                                                  double scale = 1.0);

} // namespace openstitch::stitch_analysis
