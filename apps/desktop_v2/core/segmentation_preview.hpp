// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- aperçu de segmentation AVANT numérisation : l'utilisateur règle le nombre de
// couleurs, la taille minimale des régions et le lissage en voyant le résultat côte à côte avec
// l'image d'origine. Même fonction que la numérisation (`segmentation::segment`), exécutée sur un
// fil de travail ; ne touche jamais le document. Sans Qt.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/core/task_control.hpp"
#include "openstitch/segmentation/segmentation.hpp"

namespace openstitch::desktop_v2 {

struct SegmentationPreview {
    int width{0};
    int height{0};
    std::vector<std::uint8_t> rgba; // carte des régions, fond transparent
    std::size_t regions{0};
    std::size_t smallestRegionPx{0};
    std::vector<std::array<std::uint8_t, 3>> palette; // couleur de chaque région, grandes d'abord
    double seconds{0.0};
    segmentation::SegmentationOptions options; // réglages qui ont produit cet aperçu
};

// Charge `image` et la segmente avec `options`. Erreur du moteur sinon (fichier illisible…).
[[nodiscard]] Result<SegmentationPreview>
previewSegmentation(const std::filesystem::path& image,
                    const segmentation::SegmentationOptions& options, const TaskContext& task);

// Variante sur une image déjà chargée (tests, réglages successifs sans relire le disque).
[[nodiscard]] Result<SegmentationPreview>
previewSegmentation(const image::Image& image, const segmentation::SegmentationOptions& options,
                    const TaskContext& task);

} // namespace openstitch::desktop_v2
