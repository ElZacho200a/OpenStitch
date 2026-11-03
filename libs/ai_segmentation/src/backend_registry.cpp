// SPDX-License-Identifier: Apache-2.0
#include "openstitch/ai_segmentation/backend_registry.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>

namespace openstitch::ai_segmentation {

namespace {

using json = nlohmann::json;

AiError config_error(std::string detail) {
    return AiError{AiErrorCode::WorkerNotConfigured,
                   default_message(AiErrorCode::WorkerNotConfigured), std::move(detail)};
}

std::string describe(const AiError& e) {
    return e.detail.empty() ? e.message : e.message + " (" + e.detail + ")";
}

} // namespace

AiResult<BackendSettings> parse_backend_settings(std::string_view json_text) {
    json j = json::parse(json_text, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) {
        return std::unexpected(config_error("réglages illisibles (JSON invalide)"));
    }
    if (j.value("schema", std::string{}) != "openstitch.ai.backends/1") {
        return std::unexpected(config_error("schéma attendu : openstitch.ai.backends/1"));
    }
    BackendSettings out;
    out.preferred_id = j.value("preferred", std::string{});
    const auto list = j.find("backends");
    if (list == j.end() || !list->is_array()) {
        return std::unexpected(config_error("tableau `backends` manquant"));
    }
    std::set<std::string> seen;
    for (const auto& item : *list) {
        BackendConfig c;
        c.id = item.value("id", std::string{});
        if (c.id.empty() || !seen.insert(c.id).second) {
            return std::unexpected(
                config_error("identifiant de backend vide ou en double : « " + c.id + " »"));
        }
        c.display_name = item.value("display_name", c.id);
        c.enabled = item.value("enabled", true);
        c.launch.executable = item.value("command", std::string{});
        if (c.launch.executable.empty()) {
            return std::unexpected(config_error("backend « " + c.id + " » sans `command`"));
        }
        if (const auto args = item.find("args"); args != item.end() && args->is_array()) {
            for (const auto& a : *args) {
                if (!a.is_string()) {
                    return std::unexpected(
                        config_error("`args` doit contenir des textes (backend « " + c.id + " »)"));
                }
                c.launch.args.push_back(a.get<std::string>());
            }
        }
        c.launch.working_dir = item.value("cwd", std::string{});
        out.backends.push_back(std::move(c));
    }
    return out;
}

std::string backend_settings_env_path() {
#ifdef _WIN32
    char* buffer = nullptr;
    std::size_t size = 0;
    std::string value;
    if (_dupenv_s(&buffer, &size, "OPENSTITCH_AI_BACKENDS") == 0 && buffer != nullptr) {
        value = buffer;
        std::free(buffer);
    }
    return value;
#else
    const char* value = std::getenv("OPENSTITCH_AI_BACKENDS");
    return value != nullptr ? value : "";
#endif
}

AiResult<BackendSettings> load_backend_settings(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return std::unexpected(config_error("fichier de réglages illisible : " + file.string()));
    }
    std::ostringstream text;
    text << in.rdbuf();
    auto settings = parse_backend_settings(text.str());
    if (!settings) {
        return settings;
    }
    for (BackendConfig& b : settings->backends) {
        if (!b.launch.working_dir.empty() && b.launch.working_dir.is_relative()) {
            b.launch.working_dir = file.parent_path() / b.launch.working_dir;
        }
    }
    return settings;
}

std::string_view backend_state_name(BackendState state) {
    switch (state) {
    case BackendState::Available:
        return "available";
    case BackendState::Unavailable:
        return "unavailable";
    case BackendState::Incompatible:
        return "incompatible";
    case BackendState::Failed:
        return "failed";
    case BackendState::Disabled:
        return "disabled";
    }
    return "failed";
}

TransportFactory process_transport_factory() {
    return [](const BackendConfig& config) { return spawn_process(config.launch); };
}

namespace {

BackendStatus probe(const BackendConfig& config, const TransportFactory& factory,
                    const SessionOptions& options) {
    BackendStatus status;
    status.config = config;
    if (!config.enabled) {
        status.state = BackendState::Disabled;
        status.diagnostics = "désactivé dans les réglages";
        return status;
    }
    const auto started = std::chrono::steady_clock::now();
    const auto finish = [&](BackendState state, std::string diagnostics,
                            std::optional<AiErrorCode> code = {}) {
        status.state = state;
        status.diagnostics = std::move(diagnostics);
        status.error_code = code;
        status.probe_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - started)
                              .count();
        return status;
    };
    auto transport = factory(config);
    if (!transport) {
        return finish(BackendState::Failed, describe(transport.error()), transport.error().code);
    }
    auto session = BackendSession::open(std::move(*transport), options);
    if (!session) {
        const AiError& e = session.error();
        return finish(e.code == AiErrorCode::ProtocolMismatch ? BackendState::Incompatible
                                                              : BackendState::Failed,
                      describe(e), e.code);
    }
    auto caps = session->capabilities();
    if (!caps) {
        return finish(BackendState::Failed, describe(caps.error()), caps.error().code);
    }
    status.capabilities = *caps;
    auto health = session->health();
    if (!health) {
        return finish(BackendState::Failed, describe(health.error()), health.error().code);
    }
    status.health_state = health->state;
    if (health->state == "ready") {
        return finish(BackendState::Available, "prêt");
    }
    return finish(BackendState::Unavailable,
                  health->detail.empty() ? health->state : health->detail,
                  AiErrorCode::BackendUnavailable);
}

} // namespace

std::vector<BackendStatus> discover_backends(const BackendSettings& settings,
                                             const TransportFactory& factory,
                                             SessionOptions options) {
    std::vector<BackendStatus> out;
    out.reserve(settings.backends.size());
    for (const BackendConfig& config : settings.backends) {
        out.push_back(probe(config, factory, options));
    }
    return out;
}

Resolution resolve_backend(const std::vector<BackendStatus>& statuses,
                           std::string_view preferred_id, PromptKind needed) {
    if (preferred_id.empty()) {
        return {std::nullopt, true, "aucun backend IA préféré : segmentation classique"};
    }
    const auto it = std::find_if(statuses.begin(), statuses.end(), [&](const BackendStatus& s) {
        return s.config.id == preferred_id;
    });
    if (it == statuses.end()) {
        return {std::nullopt, true,
                "backend « " + std::string(preferred_id) +
                    " » non déclaré : segmentation classique"};
    }
    if (it->state != BackendState::Available) {
        return {std::nullopt, true,
                "backend « " + it->config.id + " » " + std::string(backend_state_name(it->state)) +
                    " (" + it->diagnostics + ") : segmentation classique"};
    }
    if (!it->capabilities || !it->capabilities->supports(needed)) {
        return {std::nullopt, true,
                "backend « " + it->config.id + " » sans invite « " +
                    std::string(prompt_kind_name(needed)) + " » : segmentation classique"};
    }
    return {it->config.id, false, "backend « " + it->config.id + " » disponible"};
}

std::string backends_report_json(const std::vector<BackendStatus>& statuses,
                                 const Resolution& resolution) {
    json rows = json::array();
    for (const BackendStatus& s : statuses) {
        json row{{"id", s.config.id},
                 {"display_name", s.config.display_name},
                 {"state", std::string(backend_state_name(s.state))},
                 {"diagnostics", s.diagnostics},
                 {"probe_ms", s.probe_ms},
                 {"health", s.health_state}};
        if (s.error_code) {
            row["error_code"] = ai_error_code_name(*s.error_code);
        }
        if (s.capabilities) {
            json prompts = json::array();
            for (const PromptKind k : s.capabilities->prompts) {
                prompts.push_back(std::string(prompt_kind_name(k)));
            }
            row["capabilities"] =
                json{{"backend_id", s.capabilities->backend_id},
                     {"backend_revision", s.capabilities->backend_revision},
                     {"model_id", s.capabilities->model_id},
                     {"model_hash", s.capabilities->model_hash ? json(*s.capabilities->model_hash)
                                                               : json(nullptr)},
                     {"prompts", prompts},
                     {"devices", s.capabilities->devices},
                     {"license_code", s.capabilities->license_code},
                     {"license_weights", s.capabilities->license_weights},
                     {"experimental", s.capabilities->experimental}};
        }
        rows.push_back(std::move(row));
    }
    json out{{"schema", "openstitch.ai.backends-report/1"},
             {"protocol", std::string(kBackendProtocol)},
             {"backends", std::move(rows)},
             {"resolution", json{{"backend_id", resolution.backend_id ? json(*resolution.backend_id)
                                                                      : json(nullptr)},
                                 {"classical_fallback", resolution.classical_fallback},
                                 {"reason", resolution.reason}}}};
    return out.dump(2);
}

} // namespace openstitch::ai_segmentation
