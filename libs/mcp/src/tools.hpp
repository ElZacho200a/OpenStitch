// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "openstitch/document/project.hpp"
#include "openstitch/project_io/project_io.hpp"

namespace openstitch::mcp {

using json = nlohmann::json;

// État d'une session : un seul projet ouvert, gardé en mémoire entre les appels d'outils.
struct Session {
    std::optional<document::Project> project;
    std::optional<project_io::LoadInfo> info; // version lue (sauvegarde `.bak` si migré)
    std::filesystem::path path;               // dernier chemin connu du projet
    bool dirty{false};                        // modifié depuis la dernière ouverture/sauvegarde
};

// Résultat d'un outil : blocs `content` MCP (texte, image) et indicateur d'erreur.
struct ToolResult {
    std::vector<json> content;
    bool is_error{false};
};

struct Tool {
    const char* name;
    const char* description;
    const char* input_schema; // JSON Schema (objet) sous forme de texte
    ToolResult (*run)(Session&, const json& args);
};

[[nodiscard]] const std::vector<Tool>& tools();

// Exécute un outil par son nom ; nom inconnu ou arguments mal typés → résultat d'erreur
// `E_UNKNOWN_TOOL` / `E_USAGE` (jamais d'exception vers l'appelant).
[[nodiscard]] ToolResult call_tool(Session& session, const std::string& name, const json& args);

} // namespace openstitch::mcp
