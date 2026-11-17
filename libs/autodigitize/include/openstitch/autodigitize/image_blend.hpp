// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/image/image.hpp"

namespace openstitch::autodigitize {

// FONDU À PARTS LUES DANS L'IMAGE (2026-11-17, docs/source/directional-fill.md, « Parts
// lues dans l'image »). Une forme pluricolore (ciel, eau, brume) est cousue par N fils
// sur une grille de rangées partagée ; en chaque point, la part de chaque fil est celle
// qui reproduit le mieux la couleur de l'image par mélange optique. Les fils voisins se
// mélangent en lumière : le mélange est calculé en RVB linéaire.

using Rgb8 = std::array<std::uint8_t, 3>;

struct ImageBlendOptions {
    Micrometers cell{1000}; // pas de la grille de parts (1 mm)
    // Pénalité de localité λ : favorise les fils proches de la couleur visée (mélange moins
    // contrasté vu de près). 0,05 : meilleur compromis mesuré à 1 mm de distance de vue.
    double locality{0.05};
    // Fils mélangés au plus en un point (2 à 4).
    std::size_t max_threads_per_point{4};
    // Part minimale d'un fil en un point : une part plus faible est retirée et les autres
    // renormalisées. Un fil très minoritaire ne coud que des bouts de rangées isolés,
    // reliés par des sauts et des coupes. 0 = aucune.
    double min_share{0.0};
};

// Parts des couleurs de `palette` (2 à 8, de l'arrière vers l'avant) en chaque case d'une
// grille couvrant la boîte de `shape` (repère du modèle, origine au centre de l'image, Y
// vers le haut, comme la vectorisation). Couleur d'une case : moyenne en RVB linéaire des
// pixels de la forme qui y tombent ; une case sans pixel de la forme reprend la case
// remplie la plus proche. Parts : moindres carrés avec somme 1 et parts positives sur au
// plus `max_threads_per_point` couleurs, plus la pénalité de localité ; arrondies sur 255
// avec une somme exacte de 255 par case. Déterministe ; parallèle par rangée de cases.
// Erreur utilisateur si l'image est vide, la palette hors [2 ; 8] ou la forme sans pixel.
[[nodiscard]] Result<document::WeightGrid>
image_blend_weights(const image::Image& image, Millimeters mm_per_px,
                    const std::vector<geometry::PathSet>& shape, const std::vector<Rgb8>& palette,
                    const ImageBlendOptions& options = {});

// Fils d'une zone de fondu parmi `palette` (palette globale d'un tableau, ordre de couture) :
// le meilleur fil seul, puis ajout glouton du fil qui réduit le plus l'erreur de mélange sur
// les couleurs de la zone, gardé seulement s'il la réduit d'au moins `min_gain` (0,2 : règle
// de l'étude), `max_threads` (1 à 4) au plus. Indices dans `palette`, croissants. Un seul
// indice : la zone est un aplat. Vide si la forme ne couvre aucun pixel. Déterministe.
[[nodiscard]] std::vector<std::size_t>
choose_zone_threads(const image::Image& image, Millimeters mm_per_px,
                    const std::vector<geometry::PathSet>& shape, const std::vector<Rgb8>& palette,
                    std::size_t max_threads, double min_gain = 0.2,
                    const ImageBlendOptions& options = {});

// Proposition de `count` couleurs (2 à 16) pour un fondu à parts image : des candidates
// (k-means déterministe sur les pixels de la forme, plus les couleurs extrêmes de ses axes
// principaux, car un mélange ne sort jamais de l'enveloppe de ses fils) puis choix glouton
// de celles qui minimisent l'erreur de MÉLANGE. Triées du plus sombre au plus clair.
[[nodiscard]] Result<std::vector<Rgb8>>
propose_blend_palette(const image::Image& image, Millimeters mm_per_px,
                      const std::vector<geometry::PathSet>& shape, std::size_t count,
                      const ImageBlendOptions& options = {});

// Parts d'une couleur `target` (RVB linéaire, [0 ; 1]) sur `colors` (RVB linéaire) : exposé
// pour les tests et l'estimation d'erreur. Somme 1, parts ≥ 0, au plus `max_threads` non
// nulles. Écrit l'erreur quadratique du mélange dans `residual` si non nul.
[[nodiscard]] std::vector<double> mix_weights(const std::array<double, 3>& target,
                                              const std::vector<std::array<double, 3>>& colors,
                                              double locality, std::size_t max_threads,
                                              double* residual = nullptr);

[[nodiscard]] double srgb_to_linear(std::uint8_t channel);

} // namespace openstitch::autodigitize
