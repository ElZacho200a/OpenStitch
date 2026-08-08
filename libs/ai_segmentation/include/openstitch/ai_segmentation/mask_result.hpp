// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "openstitch/core/error.hpp"

namespace openstitch::ai_segmentation {

// Un masque produit par le worker (un fichier PNG binaire par masque, jamais
// de Base64 dans le JSON — cf. schéma `masks.json`).
struct MaskEntry {
    int id{0};
    std::string file;  // chemin relatif au dossier de tâche, ex. "masks/mask_0000.png"
    std::size_t area_pixels{0};
    std::array<int, 4> bbox_xywh{};  // x, y, largeur, hauteur en pixels
    double predicted_iou{0.0};
    double stability_score{0.0};
    int component_count_before{0};
    int component_count_after_cleanup{0};
    int removed_island_count{0};
    int filled_hole_count{0};
};

// Contenu complet d'un `masks.json` écrit par le worker pour une tâche de
// segmentation. Aucune donnée d'image ici : seulement des chemins, tailles
// et scores — les pixels vivent dans les fichiers PNG du dossier de tâche.
struct MaskCollection {
    int schema_version{0};
    std::string source_file;    // ex. "input.png", relatif au dossier de tâche
    std::string model_worker_id;
    int image_width{0};
    int image_height{0};
    std::vector<MaskEntry> masks;
};

[[nodiscard]] Result<MaskCollection> parse_masks_json(std::string_view json_text);
[[nodiscard]] std::string serialize_masks_json(const MaskCollection& collection);

}  // namespace openstitch::ai_segmentation
