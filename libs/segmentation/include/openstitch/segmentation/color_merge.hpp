// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/core/ids.hpp"
#include "openstitch/segmentation/segmentation.hpp"

namespace openstitch::segmentation {

// Fusion de COULEURS d'une segmentation (et non de régions une à une) : plusieurs régions
// connexes portent la même teinte ; fusionner deux teintes recolore toutes les régions de l'une
// avec l'autre. Tout est calculé comme un PLAN (aucune écriture) puis appliqué : le même plan sert
// à l'aperçu (sur une copie) et à la commande annulable du bureau. Déterministe.

// Une teinte distincte de la segmentation et les régions vivantes qui la portent.
struct ColorClass {
    std::array<std::uint8_t, 3> rgb{};
    std::size_t pixel_count{0};
    std::vector<RegionId> regions; // par identifiant croissant
};

// Teintes distinctes, de la plus étendue à la plus petite (égalité -> couleur croissante).
[[nodiscard]] std::vector<ColorClass> color_classes(const Segmentation& seg);

struct ColorMergePlan {
    // (région, nouvelle couleur) : seulement les régions dont la couleur change.
    std::vector<std::pair<RegionId, std::array<std::uint8_t, 3>>> recolors;
    // (gardée, absorbée) : régions voisines qui ont la même couleur APRÈS les recolorations, à
    // fusionner (ordre d'application). Vide si `merge_adjacent` est faux.
    std::vector<std::pair<RegionId, RegionId>> merges;
    std::size_t colors_before{0};
    std::size_t colors_after{0};

    [[nodiscard]] bool empty() const { return recolors.empty() && merges.empty(); }
};

// Réduit la palette à au plus `max_colors` teintes (0 = pas de limite) ET fusionne les teintes
// distantes d'au plus `max_distance` (CIEDE2000 ; 0 = pas de seuil), en pondérant par l'aire :
// le plus gros aplat garde sa teinte exacte (`thread_palette::reduce_colors`). Les régions dont la
// couleur vaut `excluded` (fond ignoré) ne comptent pas et ne sont pas touchées.
[[nodiscard]] ColorMergePlan
plan_reduce_colors(const Segmentation& seg, std::size_t max_colors, double max_distance,
                   bool merge_adjacent,
                   const std::optional<std::array<std::uint8_t, 3>>& excluded = {});

// Fusion manuelle : toutes les régions des teintes `sources` prennent la couleur `target`.
[[nodiscard]] ColorMergePlan plan_merge_colors(const Segmentation& seg,
                                               std::span<const std::array<std::uint8_t, 3>> sources,
                                               std::array<std::uint8_t, 3> target,
                                               bool merge_adjacent);

// Applique le plan : recolorations puis fusions. Erreur si une région du plan n'existe plus
// (plan calculé sur une autre segmentation) ; la segmentation peut alors être partiellement
// modifiée -- l'appelant travaille sur une copie ou valide le plan avant.
[[nodiscard]] Result<void> apply_color_merge(Segmentation& seg, const ColorMergePlan& plan);

} // namespace openstitch::segmentation
