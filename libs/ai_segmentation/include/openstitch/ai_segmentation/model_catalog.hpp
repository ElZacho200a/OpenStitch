// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace openstitch::ai_segmentation {

// Taille de modèle SAM 2.1 proposée à l'utilisateur. C'est le SEUL choix
// exposé : jamais un couple (config, checkpoint) choisi indépendamment, pour
// rendre structurellement impossible un mélange config/checkpoint incohérent
// (cf. docs/source, section IA — catalogue central).
enum class ModelId {
    Tiny,
    Small,
    BasePlus,
    Large,
};

// Description complète d'une taille de modèle : les noms de fichiers exacts
// attendus par `build_sam2()` côté worker, et l'identifiant textuel envoyé
// dans le protocole JSON Lines (jamais des chemins choisis côté C++).
struct ModelDescriptor {
    ModelId id;
    std::string_view worker_id;        // "tiny" | "small" | "base_plus" | "large"
    std::string_view display_name;     // pour l'UI
    std::string_view config_name;      // ex. "sam2.1_hiera_t.yaml"
    std::string_view checkpoint_file;  // ex. "sam2.1_hiera_tiny.pt"
    double approx_size_mb;             // indicatif, pour l'UI (espace/temps de téléchargement)
};

// Catalogue fixe des quatre tailles supportées, dans l'ordre Tiny..Large.
[[nodiscard]] const ModelDescriptor& model_descriptor(ModelId id);

// Toutes les entrées du catalogue, dans l'ordre Tiny..Large — pour peupler
// un sélecteur d'UI sans dupliquer les données.
[[nodiscard]] const std::array<ModelDescriptor, 4>& all_models();

// Reconnaît un `worker_id` reçu depuis le worker Python (ex. dans les
// réponses `model_ready`) ; nullopt si la chaîne ne correspond à aucune
// entrée du catalogue.
[[nodiscard]] std::optional<ModelId> model_id_from_worker_id(std::string_view worker_id);

}  // namespace openstitch::ai_segmentation
