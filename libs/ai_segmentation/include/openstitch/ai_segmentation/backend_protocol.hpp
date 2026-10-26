// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "openstitch/ai_segmentation/error.hpp"

namespace openstitch::ai_segmentation {

// Protocole `openstitch.ai.backend/1` : JSON Lines entre OpenStitch et un worker de vision
// (SAM 2.1, HQ-SAM 2, Florence-2…), indépendant du modèle. Spécification :
// docs/ai/backend-protocol.md. Aucun type JSON ne sort de cette bibliothèque (ADR-006).
inline constexpr std::string_view kBackendProtocol = "openstitch.ai.backend/1";
inline constexpr int kBackendProtocolMajor = 1;

template <typename T> using AiResult = std::expected<T, AiError>;

// "openstitch.ai.backend/N" -> N ; nullopt si le texte n'est pas de ce protocole.
[[nodiscard]] std::optional<int> backend_protocol_major(std::string_view protocol);

enum class PromptKind { Automatic, Box, Point, Text, Exemplar };
[[nodiscard]] std::string_view prompt_kind_name(PromptKind kind);
[[nodiscard]] std::optional<PromptKind> prompt_kind_from_name(std::string_view name);

struct BackendCapabilities {
    std::string backend_id;
    std::string backend_revision;
    std::string display_name;
    std::string model_id;
    std::optional<std::string> model_hash; // SHA-256 du checkpoint ; nullopt si absent
    std::vector<PromptKind> prompts;
    std::vector<std::string> devices;
    std::string license_code;
    std::string license_weights; // distincte de la licence du code
    bool experimental{false};

    [[nodiscard]] bool supports(PromptKind kind) const;
};

// Ce que le backend a fait à l'image en interne. Les masques rendus sont TOUJOURS à la
// résolution de l'image source ; la transformation n'est donnée que pour la traçabilité.
struct ImageTransform {
    int source_width{0};
    int source_height{0};
    int width{0};
    int height{0};
    double scale{1.0};
    std::optional<std::array<int, 4>> crop; // x, y, w, h dans l'image source
};

struct MaskProposal {
    std::string id;
    std::string file; // PNG 8 bits relatif au dossier de tâche, 255 = dans le masque
    std::array<int, 4> bbox_xywh{};
    std::int64_t area_px{0};
    std::optional<double> confidence; // rapportée par le backend, jamais synthétisée
    std::optional<int> prompt_index;
};

struct PredictResult {
    std::string request_id;
    std::string image_sha256;
    ImageTransform transform;
    std::vector<MaskProposal> masks;
    std::int64_t elapsed_ms{0};
    std::vector<std::string> warnings;
};

struct Prompt {
    PromptKind kind{PromptKind::Automatic};
    std::optional<std::array<int, 4>> box_xywh;
    std::string text;
};

struct PredictRequest {
    std::string id;
    std::string job_dir; // contient l'image d'entrée ; reçoit masks/*.png
    std::string image_file{"input.png"};
    std::vector<Prompt> prompts{Prompt{}};
    // Options propres au backend (profil, max_resolution…). Les valeurs entières et booléennes
    // sont émises comme nombres/booléens JSON, le reste comme texte.
    std::vector<std::pair<std::string, std::string>> options;
};

// -- Messages du worker --------------------------------------------------------------
struct HelloMsg {
    std::string protocol;
    std::string backend_id;
    std::int64_t pid{0};
};
struct CapabilitiesMsg {
    std::string id;
    BackendCapabilities backend;
};
struct ProgressMsg {
    std::string id;
    std::string stage;
    std::optional<double> fraction;
};
struct CancelledMsg {
    std::string id;
    std::string target_id;
};
struct HealthMsg {
    std::string id;
    std::string state; // "ready" | "loading" | "unavailable" | "error"
    std::string detail;
};
struct ErrorMsg {
    std::string id; // vide si l'erreur n'est liée à aucune requête
    std::string code_name;
    AiError error;
};
struct UnloadedMsg {
    std::string id;
};
struct ByeMsg {
    std::string id;
};
using WorkerMessage = std::variant<HelloMsg, CapabilitiesMsg, ProgressMsg, PredictResult,
                                   CancelledMsg, HealthMsg, ErrorMsg, UnloadedMsg, ByeMsg>;

// Une ligne -> un message. Une ligne qui n'est pas du JSON du protocole est une erreur
// InvalidWorkerResponse : stdout d'un worker ne doit porter aucune sortie parasite.
[[nodiscard]] AiResult<WorkerMessage> parse_worker_message(std::string_view line);

// Identifiant de requête porté par un message ("" pour hello).
[[nodiscard]] std::string message_id(const WorkerMessage& message);

// -- Requêtes (une ligne JSON sans retour à la ligne final) ------------------------------
[[nodiscard]] std::string serialize_capabilities_request(std::string_view id);
[[nodiscard]] std::string serialize_health_request(std::string_view id);
[[nodiscard]] std::string serialize_predict_request(const PredictRequest& request);
[[nodiscard]] std::string serialize_cancel_request(std::string_view id, std::string_view target_id);
[[nodiscard]] std::string serialize_unload_request(std::string_view id);
[[nodiscard]] std::string serialize_shutdown_request(std::string_view id);

} // namespace openstitch::ai_segmentation
