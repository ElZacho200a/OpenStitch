// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "openstitch/ai_segmentation/backend_session.hpp"
#include "openstitch/ai_segmentation/process_transport.hpp"

namespace openstitch::ai_segmentation {

// Backend déclaré par l'utilisateur (fichier `openstitch.ai.backends/1`). OpenStitch ne
// télécharge ni n'installe rien : il lance la commande donnée et interroge le worker.
struct BackendConfig {
    std::string id;
    std::string display_name;
    ProcessSpec launch;
    bool enabled{true};
};

struct BackendSettings {
    std::vector<BackendConfig> backends; // ordre du fichier = ordre des diagnostics
    std::string preferred_id;            // vide = aucun backend préféré (segmentation classique)
};

// {"schema":"openstitch.ai.backends/1","preferred":"sam2","backends":[{"id","display_name",
//   "command":"python","args":[...],"cwd":"...","enabled":true}]}
[[nodiscard]] AiResult<BackendSettings> parse_backend_settings(std::string_view json_text);

// Fichier de réglages désigné par la variable OPENSTITCH_AI_BACKENDS ("" si absente). Les agents
// (CLI/MCP) n'ont PAS le droit de choisir eux-mêmes la commande à lancer : seul ce fichier,
// maîtrisé par l'utilisateur, déclare les backends.
[[nodiscard]] std::string backend_settings_env_path();

// Lit et valide un fichier de réglages ; un `cwd` relatif est résolu depuis le dossier du fichier.
[[nodiscard]] AiResult<BackendSettings> load_backend_settings(const std::filesystem::path& file);

enum class BackendState {
    Available,   // hello + capabilities OK, santé « ready »
    Unavailable, // le worker répond mais signale ne pas pouvoir travailler (poids/paquets absents)
    Incompatible, // version de protocole non supportée
    Failed,       // démarrage, délai, flux ou réponse invalides
    Disabled,     // désactivé dans les réglages (non lancé)
};
[[nodiscard]] std::string_view backend_state_name(BackendState state);

struct BackendStatus {
    BackendConfig config;
    BackendState state{BackendState::Failed};
    std::optional<BackendCapabilities> capabilities;
    std::string health_state; // « ready », « unavailable »…
    std::string diagnostics;  // phrase utilisable telle quelle
    std::optional<AiErrorCode> error_code;
    std::int64_t probe_ms{0};
};

using TransportFactory = std::function<AiResult<std::unique_ptr<ITransport>>(const BackendConfig&)>;

// Fabrique réelle : `spawn_process(config.launch)`.
[[nodiscard]] TransportFactory process_transport_factory();

// Sonde chaque backend déclaré, séquentiellement et dans l'ordre : lance, lit `hello`,
// `capabilities`, `health`, arrête. Aucun état partagé entre sondes ; un backend en échec
// n'empêche pas les suivants.
[[nodiscard]] std::vector<BackendStatus> discover_backends(const BackendSettings& settings,
                                                           const TransportFactory& factory,
                                                           SessionOptions options = {});

struct Resolution {
    std::optional<std::string> backend_id; // nullopt = segmentation classique
    bool classical_fallback{true};
    std::string reason;
};

// Choisit le backend préféré s'il est disponible ET sait traiter `needed` ; sinon retombe sur la
// segmentation CIELAB classique avec la raison — l'application ne dépend d'aucun backend.
[[nodiscard]] Resolution resolve_backend(const std::vector<BackendStatus>& statuses,
                                         std::string_view preferred_id,
                                         PromptKind needed = PromptKind::Automatic);

// Rapport JSON `openstitch.ai.backends-report/1` pour `openstitch-cli ai backends --json` / MCP.
[[nodiscard]] std::string backends_report_json(const std::vector<BackendStatus>& statuses,
                                               const Resolution& resolution);

} // namespace openstitch::ai_segmentation
