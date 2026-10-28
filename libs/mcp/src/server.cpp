// SPDX-License-Identifier: Apache-2.0
#include "openstitch/mcp/server.hpp"

#include "guide.hpp"
#include "openstitch/mcp/live.hpp"
#include "tools.hpp"

namespace openstitch::mcp {

namespace {

constexpr const char* kDefaultProtocol = "2025-06-18";
constexpr std::size_t kMaxRequestBytes = 8 * 1024 * 1024;
std::string negotiated_protocol(const json& params) {
    const auto requested = params.value("protocolVersion", std::string{});
    if (requested == "2025-06-18" || requested == "2025-03-26" ||
        requested == "2024-11-05") {
        return requested;
    }
    return kDefaultProtocol;
}
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
    LiveRelay* relay{nullptr}; // mode live : appels relayés au desktop

    json list_tools() const {
        json list = json::array();
        const auto add = [&](const char* name, const char* description, const char* schema,
                             bool read_only) {
            list.push_back(json{{"name", name}, {"description", description},
                                {"inputSchema", json::parse(schema)},
                                {"annotations", {{"readOnlyHint", read_only},
                                                 {"destructiveHint", !read_only},
                                                 {"openWorldHint", false}}}});
        };
        for (const auto& tool : tools()) {
            if (relay != nullptr && live_policy(tool.name) == LivePolicy::Denied) {
                continue;
            }
            const std::string_view name = tool.name;
            const bool readonly = name == "project_status" || name == "project_operations" ||
                                  name == "digitize_guide" || name == "project_inspect" ||
                                  name == "project_select" || name == "project_palette" ||
                                  name == "semantic_validate" || name == "semantic_map" ||
                                  name == "ai_backends" || name == "project_report" ||
                                  name == "project_diagnose" || name == "project_density" ||
                                  name == "project_fidelity" || name == "project_fabric_risk" ||
                                  name == "project_locate" || name == "project_check" ||
                                  name == "project_render" || name == "project_compare" ||
                                  name == "project_checkpoints" || name == "project_satin_compare";
            add(tool.name, tool.description, tool.input_schema, readonly);
        }
        if (relay != nullptr) {
            for (const auto& extra : live_extra_tools()) {
                add(extra.name, extra.description, extra.input_schema, true);
            }
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
            task += ", largeur brodée " + (args.at("width_mm").is_string() ? args.at("width_mm").get<std::string>() : args.at("width_mm").dump()) + " mm";
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
        if (relay != nullptr) {
            return json::parse(relay->call(name, args.dump()));
        }
        const ToolResult result = call_tool(session, name, args);
        return json{{"content", result.content}, {"isError", result.is_error}};
    }
};

Server::Server() : impl_(std::make_unique<Impl>()) {}
Server::Server(LiveRelay* relay) : impl_(std::make_unique<Impl>()) {
    impl_->relay = relay;
}
Server::~Server() = default;

std::optional<std::string> Server::handle(std::string_view message) {
    if (message.size() > kMaxRequestBytes) {
        return error_response(nullptr, kInvalidRequest, "requete de plus de 8 Mio").dump();
    }
    const json request = json::parse(message, nullptr, /*allow_exceptions=*/false);
    if (request.is_discarded()) {
        return error_response(nullptr, kParseError, "JSON invalide").dump();
    }
    if (!request.is_object() || !request.contains("method") || !request.at("method").is_string()) {
        return error_response(request.is_object() ? request.value("id", json{}) : json{},
                              kInvalidRequest, "requête JSON-RPC invalide")
            .dump();
    }
    if (!request.contains("jsonrpc") || request.at("jsonrpc") != "2.0") {
        return error_response(nullptr, kInvalidRequest, "jsonrpc doit valoir 2.0").dump();
    }
    if (request.contains("id") && !request.at("id").is_null() &&
        !request.at("id").is_string() && !request.at("id").is_number()) {
        return error_response(nullptr, kInvalidRequest, "id JSON-RPC invalide").dump();
    }
    const std::string method = request.at("method").get<std::string>();
    const bool is_notification = !request.contains("id");
    const json id = request.value("id", json{});
    const json params = request.value("params", json::object());

    if (is_notification) {
        return std::nullopt; // notifications/initialized, notifications/cancelled… : rien à
                             // répondre
    }
    if (!params.is_object()) {
        return error_response(id, kInvalidParams, "params doit etre un objet").dump();
    }
    try {
        if (method == "initialize") {
            const std::string protocol = negotiated_protocol(params);
            return ok_response(
                       id, {{"protocolVersion", protocol},
                            {"capabilities",
                             {{"tools", {{"listChanged", false}}},
                              {"resources", {{"subscribe", false}, {"listChanged", false}}},
                              {"prompts", {{"listChanged", false}}}}},
                            {"serverInfo", {{"name", kServerName}, {"version", kServerVersion}}},
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

std::string self_test_json(bool& ok) {
    Server server;
    json checks = json::array();
    ok = true;
    int id = 0;
    const auto step = [&](const char* name, bool pass, const std::string& detail) {
        checks.push_back(json{{"name", name}, {"ok", pass}, {"detail", detail}});
        ok = ok && pass;
    };
    const auto rpc = [&](const char* method, json params) {
        const auto reply = server.handle(json{
            {"jsonrpc", "2.0"},
            {"id", ++id},
            {"method", method},
            {"params", std::move(params)}}.dump());
        return reply ? json::parse(*reply) : json(nullptr);
    };
    const auto tool = [&](const char* name, json args) {
        return rpc("tools/call", json{{"name", name}, {"arguments", std::move(args)}});
    };
    const auto succeeded = [](const json& r) {
        return r.contains("result") && !r.at("result").value("isError", true);
    };

    const json init = rpc("initialize", {{"protocolVersion", kDefaultProtocol}});
    step("initialize",
         init.contains("result") && init.at("result").at("serverInfo").at("name") == kServerName,
         init.contains("result") ? "serveur " + init.at("result").at("serverInfo").dump()
                                 : "échec");
    const json list = rpc("tools/list", json::object());
    const std::size_t tools = list.contains("result") ? list.at("result").at("tools").size() : 0;
    step("tools/list", tools >= 20, std::to_string(tools) + " outils");
    step("project_new", succeeded(tool("project_new", {{"width_mm", 40}, {"height_mm", 30}})), "");
    step("project_apply",
         succeeded(
             tool("project_apply", {{"operations", json::array({{{"op", "add_shape"},
                                                                 {"shape", "rect"},
                                                                 {"width_mm", 20},
                                                                 {"height_mm", 10},
                                                                 {"center_mm", {0, 0}},
                                                                 {"stitch_type", "tatami"}}})}})),
         "forme ajoutée par lot transactionnel");
    step("project_check", succeeded(tool("project_check", json::object())), "");
    const json render = tool("project_render", {{"px_per_mm", 2}});
    bool png = false;
    if (succeeded(render)) {
        for (const auto& block : render.at("result").at("content")) {
            png = png || (block.at("type") == "image" &&
                          block.at("data").get<std::string>().starts_with("iVBOR"));
        }
    }
    step("project_render", png, png ? "PNG valide" : "aucune image PNG");
    const json guide = tool("digitize_guide", {{"topic", "workflow"}});
    step("digitize_guide",
         succeeded(guide) && guide.at("result").at("content")[0].at("text").get<std::string>().find(
                                 "Phase") != std::string::npos,
         "guide embarqué lisible");
    return json{{"schema", "openstitch.mcp-doctor/1"},
                {"ok", ok},
                {"server", kServerName},
                {"version", kServerVersion},
                {"protocol", kDefaultProtocol},
                {"checks", checks}}
        .dump(2);
}

} // namespace openstitch::mcp
