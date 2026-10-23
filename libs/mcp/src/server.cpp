// SPDX-License-Identifier: Apache-2.0
#include "openstitch/mcp/server.hpp"

#include "tools.hpp"

namespace openstitch::mcp {

namespace {

constexpr const char* kDefaultProtocol = "2025-06-18";
constexpr const char* kInstructions =
    "OpenStitch : numérisation de broderie machine (image -> objets -> points -> DST). Une session "
    "garde UN projet ouvert. Boucle conseillée : project_digitize (ou project_open) ; "
    "project_inspect ; project_render pour voir ; project_check pour mesurer ; project_apply "
    "(lire project_operations avant ; dry_run d'abord si doute) ; recontrôler et re-rendre ; "
    "project_save ; project_export. Unités : mm et degrés. Chaque refus porte un code E_* stable "
    "et un hint.";

// Codes JSON-RPC standard.
constexpr int kParseError = -32700;
constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;

json error_response(const json& id, int code, const std::string& message) {
    return json{{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}

json ok_response(const json& id, json result) {
    return json{{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
}

} // namespace

struct Server::Impl {
    Session session;

    json list_tools() const {
        json list = json::array();
        for (const auto& tool : tools()) {
            list.push_back(json{{"name", tool.name},
                                {"description", tool.description},
                                {"inputSchema", json::parse(tool.input_schema)}});
        }
        return json{{"tools", list}};
    }

    json call(const json& params) {
        const std::string name = params.at("name").get<std::string>();
        const json args = params.value("arguments", json::object());
        const ToolResult result = call_tool(session, name, args);
        return json{{"content", result.content}, {"isError", result.is_error}};
    }
};

Server::Server() : impl_(std::make_unique<Impl>()) {}
Server::~Server() = default;

std::optional<std::string> Server::handle(std::string_view message) {
    const json request = json::parse(message, nullptr, /*allow_exceptions=*/false);
    if (request.is_discarded()) {
        return error_response(nullptr, kParseError, "JSON invalide").dump();
    }
    if (!request.is_object() || !request.contains("method") || !request.at("method").is_string()) {
        return error_response(request.is_object() ? request.value("id", json{}) : json{},
                              kInvalidRequest, "requête JSON-RPC invalide")
            .dump();
    }
    const std::string method = request.at("method").get<std::string>();
    const bool is_notification = !request.contains("id");
    const json id = request.value("id", json{});
    const json params = request.value("params", json::object());

    if (is_notification) {
        return std::nullopt; // notifications/initialized, notifications/cancelled… : rien à
                             // répondre
    }
    try {
        if (method == "initialize") {
            const std::string protocol = params.is_object() && params.contains("protocolVersion") &&
                                                 params.at("protocolVersion").is_string()
                                             ? params.at("protocolVersion").get<std::string>()
                                             : kDefaultProtocol;
            return ok_response(id, {{"protocolVersion", protocol},
                                    {"capabilities", {{"tools", {{"listChanged", false}}}}},
                                    {"serverInfo", {{"name", kServerName}, {"version", "1.0.0"}}},
                                    {"instructions", kInstructions}})
                .dump();
        }
        if (method == "ping") {
            return ok_response(id, json::object()).dump();
        }
        if (method == "tools/list") {
            return ok_response(id, impl_->list_tools()).dump();
        }
        if (method == "tools/call") {
            if (!params.is_object() || !params.contains("name") || !params.at("name").is_string()) {
                return error_response(id, kInvalidParams, "« name » (chaîne) obligatoire").dump();
            }
            return ok_response(id, impl_->call(params)).dump();
        }
    } catch (const json::exception& e) {
        return error_response(id, kInvalidParams, e.what()).dump();
    }
    return error_response(id, kMethodNotFound, "méthode inconnue : " + method).dump();
}

} // namespace openstitch::mcp
