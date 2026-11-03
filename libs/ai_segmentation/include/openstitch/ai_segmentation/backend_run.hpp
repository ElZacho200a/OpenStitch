// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "openstitch/ai_segmentation/backend_registry.hpp"
#include "openstitch/ai_segmentation/mask_result.hpp"

namespace openstitch::ai_segmentation {

// Une analyse complète par UN backend déclaré : lancement, `capabilities`, `health`, `predict`,
// conversion des masques. Sans Qt : le desktop l'appelle depuis `BusyIndicator::run`, le CLI et
// le MCP peuvent s'en servir. Aucun échec n'est silencieux : tout ce qui n'est pas « masques
// obtenus » ou « annulé » devient un repli EXPLICITE vers la segmentation CIELAB, avec la raison.
struct BackendRunRequest {
    BackendConfig backend;
    PromptKind prompt{PromptKind::Automatic};
    std::filesystem::path job_dir; // contient `image_file` ; reçoit masks/*.png
    std::string image_file{"input.png"};
    // Options propres au backend (« profile », « max_resolution »…), transmises telles quelles.
    std::vector<std::pair<std::string, std::string>> options;
    // Dimensions de l'image envoyée : un masque rendu à une autre résolution est refusé.
    std::optional<std::pair<int, int>> expected_size;
    SessionOptions session;
};

// Ce que l'interface doit pouvoir afficher et tracer : quel modèle a réellement répondu.
struct BackendRunReport {
    std::string backend_id;
    std::string display_name;
    std::string model_id;
    std::optional<std::string> model_hash;
    std::string license_code;
    std::string license_weights;
    std::int64_t elapsed_ms{0};
    std::vector<std::string> warnings;
};

enum class BackendRunStatus {
    Completed, // `masks` est exploitable
    Cancelled, // l'utilisateur a annulé : ni masque, ni repli
    FellBack,  // backend absent, en échec ou sans masque : `fallback_reason` explique
};

struct BackendRunOutcome {
    BackendRunStatus status{BackendRunStatus::FellBack};
    MaskCollection masks; // identifiants 0..n-1 dans l'ordre du backend
    BackendRunReport report;
    std::string fallback_reason; // phrase utilisable telle quelle (français)
    std::string detail;          // diagnostic technique (infobulle / « Afficher le détail »)
    std::optional<AiErrorCode> error_code;
};

using BackendProgressFn = BackendSession::ProgressFn;

// `cancel` est interrogé pendant `predict` (voir BackendSession) ; s'il est déjà vrai avant le
// lancement, le résultat est Cancelled sans démarrer le processus.
[[nodiscard]] BackendRunOutcome run_backend_segmentation(const BackendRunRequest& request,
                                                         const TransportFactory& factory,
                                                         const BackendProgressFn& progress = {},
                                                         const std::atomic<bool>* cancel = nullptr);

// Masques d'un `predict` -> collection au format du dialogue de revue. `confidence` du backend
// devient `predicted_iou` ; un score absent n'est jamais inventé (`*_reported` = false).
// Un chemin de fichier absolu ou sortant du dossier de tâche est refusé.
[[nodiscard]] AiResult<MaskCollection> masks_from_predict(const PredictResult& result,
                                                          std::string_view model_id);

// Libellé d'état lisible (une ligne) pour les préférences : état, modèle, licences, empreinte.
[[nodiscard]] std::string backend_status_line(const BackendStatus& status);

} // namespace openstitch::ai_segmentation
