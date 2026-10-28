// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "tools.hpp"

// Outils propres au mode live pour l'édition agentique (AI Companion 5) : contexte compact,
// ciblage par teinte de fil, plan de conversion en satin, aperçu avant/après. Aucun algorithme
// réimplémenté : tout passe par `project_ops` et les outils MCP existants, sur une COPIE.
namespace openstitch::mcp {

struct LiveContextInput {
    std::string document_name;
    bool dirty{false};
    bool never_saved{false};
    std::vector<std::uint64_t> selection;
    std::uint64_t revision{0};
    std::string session_id;
};

// Famille de teinte lisible (« orange », « rouge »…) d'un fil #RRGGBB ; vide si indéterminée.
[[nodiscard]] std::string color_family(std::uint8_t r, std::uint8_t g, std::uint8_t b);

[[nodiscard]] ToolResult live_context_tool(const document::Project& project,
                                           const LiveContextInput& in);
[[nodiscard]] ToolResult live_target_tool(const document::Project& project,
                                          const LiveContextInput& in, const json& args);
[[nodiscard]] ToolResult live_plan_satin_tool(const document::Project& project, const json& args);
// Aperçu montré à l'utilisateur AVANT d'approuver une modification : rendus avant/après et écarts
// de métriques (calculés sur des copies, hors fil d'interface).
struct ApplyPreview {
    std::string before_png;
    std::string after_png;
    std::vector<std::string> metrics;
};
[[nodiscard]] ApplyPreview build_apply_preview(const document::Project& before,
                                               const document::Project& after);

[[nodiscard]] ToolResult live_preview_tool(const document::Project& project, const json& args);

} // namespace openstitch::mcp
