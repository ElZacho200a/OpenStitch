// SPDX-License-Identifier: Apache-2.0
#include "openstitch/mcp/server.hpp"

#include "guide.hpp"
#include "tools.hpp"

namespace openstitch::mcp {

namespace {

constexpr const char* kDefaultProtocol = "2025-06-18";
constexpr const char* kInstructions =
    "OpenStitch : numérisation de broderie machine (image -> objets -> points -> DST). Une session "
    "garde UN projet ouvert.\n"
    "AVANT de numériser une image, appelle digitize_guide (topic=workflow) : c'est la méthode "
    "éprouvée du projet, à suivre plutôt que d'improviser.\n"
    "Principes : le rôle textile prime sur la couleur (deux zones de même fil ne se fusionnent "
    "pas sur la seule teinte) ; préserver les détails structurants plutôt que réduire les "
    "coupes ; comparer les trois satins (skeleton, parallel_sweep, medial_axis) avant de "
    "choisir ; 0 erreur du contrôle qualité ne prouve pas la brodabilité (densités simulées, "
    "essai physique nécessaire) ; ne jamais inventer une mesure.\n"
    "Boucle : project_digitize (ou project_open) ; project_render pour voir ; project_check pour "
    "mesurer ; project_palette ; project_select + project_apply (project_operations avant ; "
    "dry_run d'abord si doute) ; recontrôler et re-rendre ; project_save ; project_export. "
    "Unités : mm et degrés. Chaque refus porte un code E_* stable et un hint. Le guide est aussi "
    "disponible en ressources (openstitch://guide/...) et en prompt « digitize ».";

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

    static json resources_list() {
        json list = json::array();
        for (const auto& t : guide_topics()) {
            list.push_back(json{{"uri", "openstitch://guide/" + t.name},
                                {"name", t.name},
                                {"title", t.title},
                                {"description", t.description},
                                {"mimeType", "text/markdown"}});
        }
        return json{{"resources", list}};
    }

    // Résultat de `resources/read`, ou `null` si l'URI est inconnue.
    static json resource_read(const std::string& uri) {
        constexpr std::string_view kPrefix = "openstitch://guide/";
        if (uri.starts_with(kPrefix)) {
            if (const GuideTopic* t = find_topic(std::string_view(uri).substr(kPrefix.size()))) {
                return json{{"contents", json::array({json{{"uri", uri},
                                                           {"mimeType", "text/markdown"},
                                                           {"text", t->text}}})}};
            }
        }
        return nullptr;
    }

    static json prompts_list() {
        return json{
            {"prompts",
             json::array({json{
                 {"name", "digitize"},
                 {"title", "Numériser une image en broderie"},
                 {"description", "Numérise une image en projet .osp en suivant la méthode "
                                 "du guide OpenStitch (sémantique, satins, retouches, "
                                 "mesures)."},
                 {"arguments", json::array({json{{"name", "image"},
                                                 {"description", "chemin de l'image source"},
                                                 {"required", true}},
                                            json{{"name", "width_mm"},
                                                 {"description", "largeur brodée souhaitée (mm)"},
                                                 {"required", false}}})}}})}};
    }

    static json prompt_get(const json& params) {
        const std::string name = params.value("name", "");
        if (name != "digitize") {
            return nullptr;
        }
        const json args = params.value("arguments", json::object());
        std::string task =
            "Numérise l'image « " + args.value("image", "<chemin à préciser>") + " »";
        if (args.contains("width_mm")) {
            task += ", largeur brodée " + args.at("width_mm").get<std::string>() + " mm";
        }
        task +=
            ", en suivant la méthode ci-dessous avec les outils du serveur openstitch "
            "(project_digitize, project_render, project_check, project_palette, "
            "project_select, project_apply…). Si la largeur manque, ne demande que celle-là.\n\n";
        const GuideTopic* workflow = find_topic("workflow");
        const GuideTopic* mcp = find_topic("mcp");
        const std::string guide = (workflow != nullptr ? workflow->text : std::string{}) +
                                  "\n\n---\n\n" + (mcp != nullptr ? mcp->text : std::string{});
        return json{
            {"description", "Numérisation guidée OpenStitch"},
            {"messages",
             json::array({json{{"role", "user"},
                               {"content", json{{"type", "text"}, {"text", task + guide}}}}})}};
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
                                    {"capabilities",
                                     {{"tools", {{"listChanged", false}}},
                                      {"resources", {{"subscribe", false}, {"listChanged", false}}},
                                      {"prompts", {{"listChanged", false}}}}},
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
        if (method == "resources/list") {
            return ok_response(id, Impl::resources_list()).dump();
        }
        if (method == "resources/read") {
            const json result =
                params.is_object() && params.contains("uri") && params.at("uri").is_string()
                    ? Impl::resource_read(params.at("uri").get<std::string>())
                    : json(nullptr);
            if (result.is_null()) {
                return error_response(id, kInvalidParams,
                                      "ressource inconnue (voir resources/list)")
                    .dump();
            }
            return ok_response(id, result).dump();
        }
        if (method == "prompts/list") {
            return ok_response(id, Impl::prompts_list()).dump();
        }
        if (method == "prompts/get") {
            const json result = params.is_object() ? Impl::prompt_get(params) : json(nullptr);
            if (result.is_null()) {
                return error_response(id, kInvalidParams, "prompt inconnu (voir prompts/list)")
                    .dump();
            }
            return ok_response(id, result).dump();
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
