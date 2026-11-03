// SPDX-License-Identifier: Apache-2.0
#include "ai_cmd.hpp"

#include <fmt/format.h>

#include <memory>

#include "openstitch/ai_segmentation/backend_registry.hpp"

namespace openstitch_cli {

namespace {

namespace ai = openstitch::ai_segmentation;

int g_exit = 0;

struct BackendsArgs {
    std::string config;
    std::string prefer;
    std::string prompt{"automatic"};
    int timeout_s{20};
    bool require_available{false};
    bool json{false};
};

int run_backends(const BackendsArgs& a) {
    std::string config_path = a.config.empty() ? ai::backend_settings_env_path() : a.config;
    ai::BackendSettings settings;
    if (!config_path.empty()) {
        auto loaded = ai::load_backend_settings(config_path);
        if (!loaded) {
            fmt::print(stderr, "openstitch-cli ai backends : {} ({})\n", loaded.error().message,
                       loaded.error().detail);
            return 1;
        }
        settings = std::move(*loaded);
    }
    if (!a.prefer.empty()) {
        settings.preferred_id = a.prefer;
    }
    const auto kind = ai::prompt_kind_from_name(a.prompt);
    if (!kind) {
        fmt::print(stderr, "openstitch-cli ai backends : invite inconnue « {} »\n", a.prompt);
        return 1;
    }
    ai::SessionOptions options;
    options.start_timeout = std::chrono::seconds{a.timeout_s};
    options.request_timeout = std::chrono::seconds{a.timeout_s};
    const auto statuses = ai::discover_backends(settings, ai::process_transport_factory(), options);
    const auto resolution = ai::resolve_backend(statuses, settings.preferred_id, *kind);

    if (a.json) {
        fmt::print("{}\n", ai::backends_report_json(statuses, resolution));
    } else {
        if (statuses.empty()) {
            fmt::print("Aucun backend IA déclaré (--config ou OPENSTITCH_AI_BACKENDS). "
                       "Segmentation classique.\n");
        }
        for (const auto& s : statuses) {
            fmt::print("{:<14} {:<12} {}\n", s.config.id, ai::backend_state_name(s.state),
                       s.diagnostics);
            if (s.capabilities) {
                fmt::print("               modèle {} · licence code {} · poids {}\n",
                           s.capabilities->model_id, s.capabilities->license_code,
                           s.capabilities->license_weights);
            }
        }
        fmt::print("Choix : {}\n", resolution.reason);
    }
    return (a.require_available && resolution.classical_fallback) ? 2 : 0;
}

} // namespace

CLI::App* register_ai_commands(CLI::App& app) {
    auto* ai_cmd =
        app.add_subcommand("ai", "Backends de vision IA (protocole openstitch.ai.backend/1)");
    ai_cmd->group("Projets");
    ai_cmd->require_subcommand(1);
    auto args = std::make_shared<BackendsArgs>();
    auto* backends = ai_cmd->add_subcommand(
        "backends",
        "Sonde les backends déclarés (découverte, capacités, santé) et indique le choix ou le "
        "repli classique. "
        "Codes de sortie : 0 ; 1 réglages invalides ; 2 avec --require-available si repli");
    backends->add_option(
        "--config", args->config,
        "Réglages JSON openstitch.ai.backends/1 (sinon variable OPENSTITCH_AI_BACKENDS)");
    backends->add_option("--prefer", args->prefer,
                         "Identifiant du backend préféré (remplace celui des réglages)");
    backends->add_option("--prompt", args->prompt,
                         "Invite requise : automatic|box|point|text|exemplar");
    backends->add_option("--timeout-s", args->timeout_s, "Délai de sonde par backend (s)")
        ->check(CLI::PositiveNumber);
    backends->add_flag("--require-available", args->require_available,
                       "Sortie 2 si la segmentation classique est utilisée");
    backends->add_flag("--json", args->json, "JSON openstitch.ai.backends-report/1");
    backends->callback([args] { g_exit = run_backends(*args); });
    return ai_cmd;
}

int ai_exit_code() {
    return g_exit;
}

} // namespace openstitch_cli
