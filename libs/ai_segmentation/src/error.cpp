// SPDX-License-Identifier: Apache-2.0
#include "openstitch/ai_segmentation/error.hpp"

#include <array>
#include <utility>

namespace openstitch::ai_segmentation {

namespace {

struct CodeEntry {
    AiErrorCode code;
    const char* name;
    const char* message;
};

constexpr std::array<CodeEntry, 17> kCodes{{
    {AiErrorCode::WorkerNotConfigured, "WORKER_NOT_CONFIGURED",
     "La segmentation par IA n'est pas configurée. Ouvrez Préférences > Intelligence "
     "artificielle pour la mettre en place."},
    {AiErrorCode::WorkerStartFailed, "WORKER_START_FAILED",
     "Le worker de segmentation IA n'a pas pu démarrer."},
    {AiErrorCode::PythonNotFound, "PYTHON_NOT_FOUND",
     "Aucun interpréteur Python n'a été trouvé pour le worker de segmentation IA."},
    {AiErrorCode::WslNotFound, "WSL_NOT_FOUND",
     "WSL est introuvable ou n'est pas installé. Installez WSL ou changez d'environnement "
     "d'exécution dans les préférences."},
    {AiErrorCode::VenvNotFound, "VENV_NOT_FOUND",
     "L'environnement virtuel Python configuré pour le worker IA est introuvable."},
    {AiErrorCode::Sam2NotInstalled, "SAM2_NOT_INSTALLED",
     "Le paquet SAM 2 n'est pas installé dans l'environnement Python configuré."},
    {AiErrorCode::ModelNotInstalled, "MODEL_NOT_INSTALLED",
     "Le modèle sélectionné n'est pas installé."},
    {AiErrorCode::ConfigNotFound, "CONFIG_NOT_FOUND",
     "Le fichier de configuration du modèle est introuvable."},
    {AiErrorCode::CheckpointNotFound, "CHECKPOINT_NOT_FOUND",
     "Le fichier de poids (checkpoint) du modèle est introuvable."},
    {AiErrorCode::ModelCheckpointMismatch, "MODEL_CHECKPOINT_MISMATCH",
     "Le fichier de poids ne correspond pas à la configuration du modèle sélectionné. "
     "Réinstallez ce modèle."},
    {AiErrorCode::CudaUnavailable, "CUDA_UNAVAILABLE",
     "Aucun GPU CUDA disponible : la segmentation utilisera le processeur (plus lente)."},
    {AiErrorCode::CudaOutOfMemory, "CUDA_OUT_OF_MEMORY",
     "Mémoire GPU insuffisante pour ce modèle. Essayez un modèle plus petit (Tiny ou Small) "
     "ou réduisez la résolution maximale d'analyse dans les préférences."},
    {AiErrorCode::ImageLoadFailed, "IMAGE_LOAD_FAILED",
     "Le worker n'a pas pu charger l'image à segmenter."},
    {AiErrorCode::InferenceFailed, "INFERENCE_FAILED", "L'analyse par le modèle IA a échoué."},
    {AiErrorCode::InvalidWorkerResponse, "INVALID_WORKER_RESPONSE",
     "Réponse inattendue du worker de segmentation IA."},
    {AiErrorCode::WorkerCrashed, "WORKER_CRASHED",
     "Le worker de segmentation IA s'est arrêté de façon inattendue. Un nouveau worker va "
     "être relancé."},
    {AiErrorCode::Cancelled, "CANCELLED", "Segmentation annulée."},
}};

}  // namespace

std::string ai_error_code_name(AiErrorCode code) {
    for (const auto& entry : kCodes) {
        if (entry.code == code) {
            return entry.name;
        }
    }
    return "UNKNOWN";
}

AiErrorCode ai_error_code_from_name(const std::string& name) {
    for (const auto& entry : kCodes) {
        if (name == entry.name) {
            return entry.code;
        }
    }
    return AiErrorCode::InvalidWorkerResponse;
}

std::string default_message(AiErrorCode code) {
    for (const auto& entry : kCodes) {
        if (entry.code == code) {
            return entry.message;
        }
    }
    return "Erreur de segmentation IA inconnue.";
}

}  // namespace openstitch::ai_segmentation
