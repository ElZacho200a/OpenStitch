// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

namespace openstitch::ai_segmentation {

// Taxonomie des échecs propres au sous-système IA (worker, modèle,
// inférence). Distincte de `openstitch::ErrorCategory` : chaque code a un
// message utilisateur par défaut ET une cause technique précise, nécessaires
// pour distinguer par exemple "WSL absent" de "checkpoint absent" à l'écran.
enum class AiErrorCode {
    WorkerNotConfigured,
    WorkerStartFailed,
    PythonNotFound,
    WslNotFound,
    VenvNotFound,
    Sam2NotInstalled,
    ModelNotInstalled,
    ConfigNotFound,
    CheckpointNotFound,
    ModelCheckpointMismatch,
    CudaUnavailable,
    CudaOutOfMemory,
    ImageLoadFailed,
    InferenceFailed,
    InvalidWorkerResponse,
    WorkerCrashed,
    Cancelled,
};

struct AiError {
    AiErrorCode code{AiErrorCode::WorkerNotConfigured};
    std::string message;  // montrable tel quel (par défaut : default_message(code))
    std::string detail;   // réservé aux logs / au panneau développeur
};

// Nom stable du code, tel qu'échangé avec le worker Python (`code` du JSON
// d'erreur) — ex. "MODEL_CHECKPOINT_MISMATCH". Utilisé pour le round-trip
// protocole <-> enum, indépendamment du texte affiché.
[[nodiscard]] std::string ai_error_code_name(AiErrorCode code);
[[nodiscard]] AiErrorCode ai_error_code_from_name(const std::string& name);

// Message utilisateur par défaut (français) pour un code donné, avant tout
// enrichissement avec le `detail` reçu du worker.
[[nodiscard]] std::string default_message(AiErrorCode code);

}  // namespace openstitch::ai_segmentation
