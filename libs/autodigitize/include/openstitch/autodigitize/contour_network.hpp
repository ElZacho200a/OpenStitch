// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "openstitch/autodigitize/contour_options.hpp"
#include "openstitch/core/error.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/segmentation/segmentation.hpp"

namespace openstitch::autodigitize {

// GEOMETRIE : ce qui a ete detecte (aucune decision de couture, aucun id).

enum class ContourNodeKind : std::uint8_t { Endpoint, Junction, Continuation };

struct ContourNode {
    Vec2um position{};
    ContourNodeKind kind{ContourNodeKind::Endpoint};
    int degree{0};
};

// Un trait entre deux noeuds (ou une boucle fermee sans noeud).
struct ContourSegment {
    std::vector<Vec2um> centerline;    // polyligne ouverte, simplifiee (si `closed`: anneau)
    std::vector<double> half_width_um; // demi-largeur locale, parallele a `centerline`
    bool closed{false};                // anneau pur, sans noeud
    std::int32_t start_node{-1};       // index dans ContourComponent::nodes (-1 : aucun)
    std::int32_t end_node{-1};
    double length_um{0.0};
    // Largeurs robustes (centre du trait, percentiles 10/90 : les bouts de
    // jonction et d'extremite faussent le rayon local).
    double min_width_um{0.0};
    double max_width_um{0.0};
    double mean_width_um{0.0};
    // Irregularite de largeur : etendue (p90 - p10) des ecarts a la tendance
    // LINEAIRE de la largeur le long du trait, rapportee a la largeur moyenne.
    // Un effilement regulier a une irregularite faible ; une largeur qui ondule
    // ou saute a une irregularite forte.
    double width_roughness{0.0};
    double max_turn_deg{0.0}; // virage le plus brusque (fenetre ~1 mm)
};

// Une composante connexe d'une couleur (un "trait" ou un reseau de traits).
struct ContourComponent {
    std::array<std::uint8_t, 3> rgb{};
    geometry::PathSet region; // polygone vectorise dont le squelette est issu
    std::vector<ContourNode> nodes;
    std::vector<ContourSegment> segments;
    double raster_pixel_um{0.0}; // pixel reel du squelette
    // Trait de 1-2 px extrait du masque de labels : pas de polygone (`region`
    // vide), donc jamais de satin.
    bool pixel_route{false};
    std::size_t removed_short_branches{0};
    std::size_t removed_small_loops{0};

    [[nodiscard]] std::size_t junction_count() const;
    [[nodiscard]] std::size_t endpoint_count() const;
};

struct ContourNetwork {
    double detail{0.5};
    ContourThresholds thresholds;
    std::vector<ContourComponent> components; // ordre : couleur (rgb) puis position
    std::size_t removed_isolated{0};          // composantes isolees sous le seuil
    std::size_t failed_components{0};         // analyse impossible (diagnostic)
    std::vector<std::string> diagnostics;
};

// Segmentation -> reseau de lignes mediennes par couleur. Chaque couleur est
// un groupe distinct ; les traits de couleurs differentes ne sont jamais
// fusionnes. Deterministe.
[[nodiscard]] Result<ContourNetwork> analyze_contours(const segmentation::Segmentation& seg,
                                                      const ContourOptions& options);

} // namespace openstitch::autodigitize
