// SPDX-License-Identifier: Apache-2.0
#include "openstitch/ai_segmentation/model_catalog.hpp"

#include <array>

namespace openstitch::ai_segmentation {

namespace {

// Catalogue central : c'est la SEULE association config<->checkpoint de tout
// le projet (C++ et worker Python). Le worker reçoit `worker_id`, jamais
// deux chemins choisis indépendamment.
// Tailles de fichier vérifiées auprès des checkpoints officiels
// (dl.fbaipublicfiles.com/segment_anything_2/092824/...). `config_name` est
// le chemin EXACT attendu par `build_sam2()` (relatif à la racine du paquet
// `sam2`, avec le préfixe "configs/" -- vérifié empiriquement contre le
// paquet installé : sam2/__init__.py appelle
// `initialize_config_module("sam2")`, dont la racine de recherche Hydra est
// la racine du paquet, pas son sous-dossier configs/).
constexpr std::array<ModelDescriptor, 4> kCatalog{{
    {ModelId::Tiny, "tiny", "Tiny (rapide)", "configs/sam2.1/sam2.1_hiera_t.yaml",
     "sam2.1_hiera_tiny.pt", 149.0},
    {ModelId::Small, "small", "Small", "configs/sam2.1/sam2.1_hiera_s.yaml",
     "sam2.1_hiera_small.pt", 176.0},
    {ModelId::BasePlus, "base_plus", "Base+", "configs/sam2.1/sam2.1_hiera_b+.yaml",
     "sam2.1_hiera_base_plus.pt", 309.0},
    {ModelId::Large, "large", "Large (qualité maximale)", "configs/sam2.1/sam2.1_hiera_l.yaml",
     "sam2.1_hiera_large.pt", 857.0},
}};

}  // namespace

const ModelDescriptor& model_descriptor(ModelId id) {
    for (const auto& entry : kCatalog) {
        if (entry.id == id) {
            return entry;
        }
    }
    return kCatalog[0];  // inatteignable : ModelId est un enum fermé couvrant kCatalog
}

const std::array<ModelDescriptor, 4>& all_models() {
    return kCatalog;
}

std::optional<ModelId> model_id_from_worker_id(std::string_view worker_id) {
    for (const auto& entry : kCatalog) {
        if (entry.worker_id == worker_id) {
            return entry.id;
        }
    }
    return std::nullopt;
}

}  // namespace openstitch::ai_segmentation
