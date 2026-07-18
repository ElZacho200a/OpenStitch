// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/core/ids.hpp"
#include "openstitch/image/image.hpp"

namespace openstitch::segmentation {

// Une région connexe issue de la quantification. Deux régions distinctes
// restent distinctes même si elles partagent la même couleur (§4.3).
struct Region {
    RegionId id;
    std::array<std::uint8_t, 3> rgb{};  // couleur représentative
    std::size_t pixel_count{0};
};

// Carte de régions d'une image.
// - labels[y*width+x] : 0 = fond (pixels transparents), sinon slot+1 ;
// - region_slots[slot] : la région, ou nullopt si elle a été fusionnée/supprimée.
// Les region_slots ne sont JAMAIS réutilisés : RegionId (= slot+1) est stable
// pendant toute la vie de la segmentation.
struct Segmentation {
    int width{0};
    int height{0};
    std::vector<std::uint32_t> labels;
    std::vector<std::optional<Region>> region_slots;

    [[nodiscard]] const Region* find(RegionId id) const;
    [[nodiscard]] Region* find(RegionId id);
    [[nodiscard]] std::size_t region_count() const;
};

struct SegmentationOptions {
    int max_colors{8};      // 2..64
    int min_region_px{16};  // régions plus petites absorbées par leur voisine
};

// Quantifie l'image en CIELAB (k-means déterministe) puis extrait les
// composantes connexes (4-connexité). Les pixels d'alpha < 128 forment le fond.
[[nodiscard]] Result<Segmentation> segment(const image::Image& img,
                                           const SegmentationOptions& options);

[[nodiscard]] std::optional<RegionId> region_at(const Segmentation& seg, int x, int y);

// Fusionne `absorb` dans `keep` (la couleur de `keep` est conservée).
// Renvoie les indices de pixels réétiquetés — nécessaires à l'annulation.
[[nodiscard]] Result<std::vector<std::uint32_t>> merge_regions(Segmentation& seg, RegionId keep,
                                                               RegionId absorb);

// Supprime une région en l'absorbant dans sa voisine majoritaire (choix
// déterministe), ou dans le fond si elle n'a aucune voisine.
// Renvoie {absorbeur (invalide = fond), indices réétiquetés}.
[[nodiscard]] Result<std::pair<RegionId, std::vector<std::uint32_t>>> remove_region(
    Segmentation& seg, RegionId id);

// Change la couleur représentative. Renvoie l'ancienne couleur.
[[nodiscard]] Result<std::array<std::uint8_t, 3>> recolor_region(
    Segmentation& seg, RegionId id, std::array<std::uint8_t, 3> rgb);

// Carte des régions en RGBA (fond transparent) ; la région `highlight`
// est éclaircie pour matérialiser la sélection.
[[nodiscard]] image::Image render_map(const Segmentation& seg,
                                      std::optional<RegionId> highlight = {});

}  // namespace openstitch::segmentation
