// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::stitch_analysis {

// =============================================================================
// HP-VIEW-005 : carte de densité (cœur, Qt-free, déterministe).
//
// Entrée : la séquence EFFECTIVE (`stitch_generation::effective_sequence`). La
// grille couvre la boîte englobante des piqûres, alignée sur des multiples de
// `cell` (donc indépendante de l'origine du motif et stable d'un calcul à l'autre).
// Deux mesures par case :
//   - pénétrations : nombre de commandes `Stitch` dont la position tombe dans la
//     case (toutes passes) ; densité = pénétrations / surface de la case, en /mm² ;
//   - fil : longueur de fil des segments cousus (répartie sur les cases traversées)
//     x largeur du fil / surface, en « couches de remplissage dense » (même unité que
//     la règle `couches-superposees`).
// L'affichage (surimpression colorée) reste à la charge de l'interface.
// =============================================================================

struct DensityOptions {
    Micrometers cell{1'000};       // côté d'une case (1 mm)
    Micrometers thread_width{400}; // largeur du fil pour l'épaisseur cumulée
    // Au-dessus de cette densité (pénétrations/mm²) une case est « sur-dense ». Un
    // remplissage dense (rangées à 0,4 mm, points de 3 mm) fait ~0,8 /mm² ; deux
    // couches avec sous-couches ~2,5. 4,0 = heuristique d'ingénierie, non mesurée sur
    // un corpus. 0 = aucune zone signalée.
    double max_penetrations_per_mm2{4.0};
    // Plafond de cases : au-delà, la case est doublée jusqu'à tenir (mémoire bornée).
    std::size_t max_cells{1'000'000};
};

struct DensityMap {
    Vec2um origin{};         // coin bas-gauche de la case (0, 0), multiple de `cell`
    Micrometers cell{1'000}; // côté effectivement utilisé (>= celui demandé)
    std::size_t columns{0};
    std::size_t rows{0};
    std::vector<std::uint32_t> penetrations; // rows x columns, ligne par ligne (Y croissant)
    std::vector<double> thread_um;           // longueur de fil par case, en µm

    [[nodiscard]] bool empty() const { return penetrations.empty(); }
    [[nodiscard]] double cell_area_mm2() const;
    // Pénétrations par mm² de la case (col, row) ; 0 hors grille.
    [[nodiscard]] double density_per_mm2(std::size_t column, std::size_t row) const;
    // Épaisseur de fil en couches de remplissage dense ; 0 hors grille.
    [[nodiscard]] double thickness_layers(std::size_t column, std::size_t row,
                                          Micrometers thread_width) const;
    [[nodiscard]] double max_density_per_mm2() const;
};

// Zone contiguë (4-connexité) de cases au-dessus du seuil.
struct DensityHotspot {
    stitch::BoundsUm box{}; // boîte englobante des cases, en µm
    Vec2um center{};        // centre de la première case (balayage ligne par ligne)
    ObjectId object{};      // objet ayant le plus de piqûres dans la zone
    double max_penetrations_per_mm2{0.0};
    double area_mm2{0.0};
    std::size_t cells{0};
};

[[nodiscard]] DensityMap compute_density_map(const stitch::StitchSequence& sequence,
                                             const DensityOptions& options = {});

// Zones sur-denses, dans l'ordre de balayage de la grille (ligne par ligne, Y croissant).
// `options.max_penetrations_per_mm2 <= 0` : liste vide.
[[nodiscard]] std::vector<DensityHotspot>
find_density_hotspots(const stitch::StitchSequence& sequence, const DensityOptions& options = {});

} // namespace openstitch::stitch_analysis
