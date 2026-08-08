// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/core/ids.hpp"
#include "openstitch/segmentation/segmentation.hpp"

namespace openstitch::ai_segmentation {

struct TopologyCleanupOptions {
    double mm_per_px{25.4 / 96.0};
    // Composante (8-connexité) de label non-fond sous ce seuil : fusionnée
    // dans la région voisine partageant la plus longue frontière, ou
    // supprimée (retour au fond) si isolée.
    double min_island_area_mm2{0.5};
    // Composante de fond entièrement enclose sous ce seuil : comblée dans la
    // région voisine.
    double min_hole_area_mm2{0.5};
    // Région dont la largeur estimée (2×aire/périmètre) tombe sous ce seuil
    // est signalée « fine » — informatif seulement, jamais corrigé ici.
    double thin_band_max_width_mm{0.3};
    // Régions jamais fusionnées ni supprimées, même sous les seuils
    // ci-dessus (détail protégé explicitement par l'utilisateur).
    std::vector<RegionId> protected_regions;
};

struct SegmentationValidationReport {
    std::size_t label_count{0};
    std::size_t component_count{0};
    std::size_t tiny_island_count{0};
    std::size_t tiny_hole_count{0};
    std::size_t thin_component_count{0};
    std::size_t ambiguous_component_count{0};
    bool ready_for_vectorization{false};
};

// Passe de nettoyage topologique appliquée EN PLACE sur `seg`, juste avant
// la vectorisation : composantes connexes (8-connexité), îlots fusionnés
// vers le voisin à la plus longue frontière partagée (ou supprimés s'ils
// sont isolés), trous comblés, régions protégées jamais touchées, bandes
// fines signalées sans être corrigées. Les seuils d'aire sont physiques
// (mm²), donc valides quelle que soit la résolution source.
[[nodiscard]] Result<SegmentationValidationReport> cleanup_topology(
    segmentation::Segmentation& seg, const TopologyCleanupOptions& options);

}  // namespace openstitch::ai_segmentation
