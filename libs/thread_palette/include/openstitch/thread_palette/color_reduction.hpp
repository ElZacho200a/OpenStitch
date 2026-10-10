// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace openstitch::thread_palette {

// Une couleur à réduire, avec son poids (aire, nombre de points... : l'unité
// n'importe pas, seul le rapport entre les poids compte).
struct WeightedColor {
    std::array<std::uint8_t, 3> rgb{};
    double weight{1.0};
};

// Résultat d'une réduction de palette : `palette` (au plus `max_colors`
// couleurs, ordonnées par première apparition dans l'entrée) et `mapping`
// (même taille que l'entrée : indice de la couleur de `palette` retenue pour
// chaque couleur d'entrée).
struct ColorReduction {
    std::vector<std::array<std::uint8_t, 3>> palette;
    std::vector<std::size_t> mapping;
};

// Réduit `colors` à au plus `max_colors` couleurs (HP-THR-011, « limiter à N
// fils ») par fusion agglomérative : tant qu'il reste trop de couleurs, les
// deux couleurs perceptuellement les plus proches (CIEDE2000) sont fusionnées
// et le résultat prend la couleur de la plus lourde des deux (le plus gros
// aplat garde sa teinte exacte). Les couleurs identiques fusionnent d'abord
// (distance nulle). Déterministe : égalités départagées par l'ordre d'entrée.
// `max_colors == 0` = pas de limite (identité, doublons exacts fusionnés).
[[nodiscard]] ColorReduction reduce_colors(std::span<const WeightedColor> colors,
                                           std::size_t max_colors);

} // namespace openstitch::thread_palette
