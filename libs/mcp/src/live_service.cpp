// SPDX-License-Identifier: Apache-2.0
// Mode live : politique d'outils, instantané, approbation et commit sur le projet de l'hôte.
// Voir live.hpp et docs/adr/0001-compagnon-ia-session-live.md.
#include <algorithm>
#include <array>
#include <random>
#include <utility>

#include <nlohmann/json.hpp>

#include "openstitch/mcp/live.hpp"
#include "openstitch/project_ops/errors.hpp"
#include "openstitch/project_ops/report.hpp"
#include "tools.hpp"

namespace openstitch::mcp {

namespace po = openstitch::project_ops;

namespace {

constexpr std::array<std::string_view, 16> kReadTools = {
    "digitize_guide",   "project_operations",  "project_inspect",   "project_select",
    "project_palette",  "project_report",      "project_diagnose",  "project_density",
    "project_fidelity", "project_fabric_risk", "project_locate",    "project_satin_compare",
    "project_check",    "project_render",      "semantic_validate", "ai_backends"};

const std::vector<LiveExtraTool> kExtraTools = {
    {"live_status",
     "État du projet OUVERT dans OpenStitch Desktop : nom, modifié ou non, jamais enregistré, "
     "sélection courante (identifiants d'objets de broderie), session et révision. À appeler avant "
     "de modifier : une modification sur une révision périmée est refusée (E_STALE_REVISION).",
     R"({"type":"object","properties":{}})"},
};

json error_content(const std::string& tool, const char* code, const std::string& message,
                   const std::string& hint = {}) {
    return json{
        {"content", json::array({json{{"type", "text"},
                                      {"text", po::error_json(tool, code, message, hint)}}})},
        {"isError", true}};
}

json text_content(const std::string& text) {
    return json{{"content", json::array({json{{"type", "text"}, {"text", text}}})},
                {"isError", false}};
}

} // namespace

LivePolicy live_policy(std::string_view tool) {
    if (std::find(kReadTools.begin(), kReadTools.end(), tool) != kReadTools.end()) {
        return LivePolicy::Read;
    }
    if (tool == "project_apply") {
        return LivePolicy::Apply;
    }
    return LivePolicy::Denied;
}

const std::vector<LiveExtraTool>& live_extra_tools() {
    return kExtraTools;
}

// --- Appel en cours --------------------------------------------------------------------------

struct LivePendingCall::Impl {
    std::string tool;
    json args = json::object();
    LivePolicy policy{LivePolicy::Denied};
    bool dry_run{false};
    std::optional<json> immediate; // réponse prête (refus, live_status, argument invalide)
    project_ops::LiveBridge::Snapshot snapshot;
    ToolResult result;
    std::shared_ptr<const document::Project> final_project; // issu d'un project_apply réussi
    bool ran{false};
};

LivePendingCall::LivePendingCall() : impl_(std::make_unique<Impl>()) {}
LivePendingCall::~LivePendingCall() = default;

void LivePendingCall::run() {
    Impl& c = *impl_;
    if (c.immediate || c.ran) {
        return;
    }
    c.ran = true;
    Session session;
    session.project = *c.snapshot.project; // copie privée : le document hôte n'est jamais touché
    c.result = call_tool(session, c.tool, c.args);
    if (c.policy == LivePolicy::Apply && !c.dry_run && !c.result.is_error && session.dirty) {
        c.final_project = std::make_shared<const document::Project>(std::move(*session.project));
    }
}

// --- Service ---------------------------------------------------------------------------------

LiveService::LiveService(project_ops::LiveBridge& bridge, IApprovalGate& gate, InfoProvider info,
                         std::function<void()> on_applied)
    : bridge_(bridge), gate_(gate), info_(std::move(info)), on_applied_(std::move(on_applied)) {}

std::shared_ptr<LivePendingCall> LiveService::begin(const std::string& tool,
                                                    const std::string& args_json) {
    auto call = std::make_shared<LivePendingCall>();
    auto& c = *call->impl_;
    c.tool = tool;

    if (tool == "live_status") {
        const LiveDocumentInfo info = info_ ? info_() : LiveDocumentInfo{};
        json doc{{"schema", kLiveSchema},
                 {"session_id", bridge_.session_id()},
                 {"revision", bridge_.revision()},
                 {"document",
                  {{"name", info.name}, {"dirty", info.dirty}, {"never_saved", info.never_saved}}},
                 {"selection", info.selection}};
        c.immediate = text_content(doc.dump());
        return call;
    }
    c.policy = live_policy(tool);
    if (c.policy == LivePolicy::Denied) {
        c.immediate = error_content(
            tool, po::code::kRefused,
            "l'outil « " + tool + " » n'est pas disponible sur le projet ouvert (mode live)",
            "ouvrir, enregistrer, exporter ou créer un projet se fait depuis OpenStitch Desktop ; "
            "outils disponibles : tools/list");
        return call;
    }
    const json parsed =
        args_json.empty() ? json::object() : json::parse(args_json, nullptr, /*exceptions=*/false);
    if (parsed.is_discarded() || !(parsed.is_object() || parsed.is_null())) {
        c.immediate = error_content(tool, po::code::kUsage, "« arguments » doit être un objet JSON",
                                    "voir inputSchema de l'outil");
        return call;
    }
    c.args = parsed.is_null() ? json::object() : parsed;
    c.dry_run = c.args.contains("dry_run") && c.args.at("dry_run").is_boolean() &&
                c.args.at("dry_run").get<bool>();
    c.snapshot = bridge_.snapshot();
    return call;
}

std::string LiveService::finish(LivePendingCall& call) {
    auto& c = *call.impl_;
    if (c.immediate) {
        return c.immediate->dump();
    }
    json out{{"content", c.result.content}, {"isError", c.result.is_error}};
    bool applied = false;

    if (c.final_project) {
        const auto refuse = [&](const char* code, const std::string& message,
                                const std::string& hint) {
            out = error_content(c.tool, code, message, hint);
        };
        if (c.snapshot.session_id != bridge_.session_id() ||
            c.snapshot.revision != bridge_.revision()) {
            refuse(po::code::kStaleRevision,
                   "le projet a changé pendant le calcul : la modification n'est pas appliquée",
                   "relisez live_status puis recommencez (rien n'a été modifié)");
        } else {
            ApprovalRequest request;
            request.tool = c.tool;
            request.label = "IA : lot d'opérations";
            request.report_json = c.result.content.empty()
                                      ? std::string{}
                                      : c.result.content.front().value("text", std::string{});
            // Lignes lisibles : chaque opération du lot avec son objet cible (lien de focus).
            if (c.args.contains("operations") && c.args.at("operations").is_array()) {
                for (const auto& op : c.args.at("operations")) {
                    ApprovalItem item;
                    const std::string name = op.value("op", std::string("?"));
                    if (op.contains("object") && op.at("object").is_number_unsigned()) {
                        item.object_id = op.at("object").get<std::uint64_t>();
                        item.text = name + " #" + std::to_string(item.object_id);
                    } else {
                        item.text = name;
                    }
                    request.items.push_back(std::move(item));
                }
            }
            if (!c.result.content.empty()) {
                const json report = json::parse(request.report_json, nullptr, false);
                if (report.is_object() && report.contains("skipped") &&
                    report.at("skipped").is_array()) {
                    for (const auto& skipped : report.at("skipped")) {
                        request.warnings.push_back(
                            "écartée : " + skipped.value("op", std::string("?")) + " (" +
                            skipped.value("code", std::string("?")) + ") — " +
                            skipped.value("message", std::string{}));
                    }
                }
            }
            request.session_id = c.snapshot.session_id;
            request.revision = c.snapshot.revision;
            if (!gate_.approve(request)) {
                refuse(po::code::kApprovalDenied, "l'utilisateur a refusé la modification",
                       "aucune modification n'a été appliquée ; proposez une variante ou demandez "
                       "ce qui ne convient pas");
            } else {
                const auto r = bridge_.commit_project(c.snapshot.session_id, c.snapshot.revision,
                                                      c.snapshot.project, c.final_project,
                                                      request.label, true);
                if (r.ok && r.applied) {
                    applied = true;
                    if (on_applied_) {
                        on_applied_();
                    }
                } else {
                    refuse(r.code.empty() ? po::code::kInternal : r.code.c_str(), r.message,
                           "rien n'a été modifié");
                }
            }
        }
    }
    out["live"] = json{{"session_id", bridge_.session_id()},
                       {"revision", bridge_.revision()},
                       {"applied", applied}};
    return out.dump();
}

std::string LiveService::call(const std::string& tool, const std::string& args_json) {
    auto pending = begin(tool, args_json);
    pending->run();
    return finish(*pending);
}

std::string LiveService::attach_info_json() {
    json perms{{"read", json::array()}, {"apply", json::array({"project_apply"})}};
    for (const auto& t : kReadTools) {
        perms["read"].push_back(std::string(t));
    }
    perms["read"].push_back("live_status");
    return json{{"schema", kLiveSchema},
                {"version", kLiveVersion},
                {"session_id", bridge_.session_id()},
                {"revision", bridge_.revision()},
                {"permissions", perms}}
        .dump();
}

// --- Hôte (protocole) ------------------------------------------------------------------------

namespace {

std::string reply_error(const std::string& id_json, const char* code, const std::string& message,
                        const std::string& hint = {}) {
    return std::string(R"({"v":1,"id":)") + id_json + R"(,"ok":false,"error":)" +
           json{{"code", code}, {"message", message}, {"hint", hint}}.dump() + "}";
}

std::string reply_ok(const std::string& id_json, const std::string& result_json) {
    return std::string(R"({"v":1,"id":)") + id_json + R"(,"ok":true,"result":)" + result_json + "}";
}

// Comparaison à durée constante : le jeton ne doit pas fuir par le temps de réponse.
bool token_equal(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) {
        return false;
    }
    unsigned char diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return diff == 0;
}

} // namespace

LiveHost::LiveHost(LiveService& service, std::string token)
    : service_(service), token_(std::move(token)) {}

LiveHost::Reply LiveHost::begin(const std::string& line) {
    Reply reply;
    reply.id_json = "null";
    const json request = json::parse(line, nullptr, /*exceptions=*/false);
    if (request.is_discarded() || !request.is_object()) {
        reply.immediate = reply_error("null", "E_USAGE", "requête JSON invalide");
        return reply;
    }
    reply.id_json = request.contains("id") ? request.at("id").dump() : "null";
    if (!request.contains("token") || !request.at("token").is_string() ||
        !token_equal(request.at("token").get<std::string>(), token_)) {
        reply.immediate = reply_error(reply.id_json, "E_UNAUTHORIZED", "jeton d'accès invalide",
                                      "relancez openstitch-mcp --live : il relit le fichier de "
                                      "découverte de l'application");
        return reply;
    }
    if (request.value("v", 0) != kLiveVersion) {
        reply.immediate = reply_error(reply.id_json, "E_UNSUPPORTED_VERSION",
                                      "version du protocole live non prise en charge",
                                      "versions prises en charge : 1");
        return reply;
    }
    const std::string op = request.value("op", std::string{});
    if (op == "ping") {
        reply.immediate = reply_ok(reply.id_json, "{}");
    } else if (op == "attach") {
        reply.immediate = reply_ok(reply.id_json, service_.attach_info_json());
    } else if (op == "call") {
        if (!request.contains("tool") || !request.at("tool").is_string()) {
            reply.immediate = reply_error(reply.id_json, "E_USAGE", "« tool » obligatoire");
            return reply;
        }
        const std::string args =
            request.contains("args") ? request.at("args").dump() : std::string("{}");
        reply.call = service_.begin(request.at("tool").get<std::string>(), args);
    } else {
        reply.immediate = reply_error(reply.id_json, "E_USAGE", "opération inconnue : " + op);
    }
    return reply;
}

std::string LiveHost::finish(Reply& reply) {
    if (!reply.call) {
        return reply.immediate;
    }
    return reply_ok(reply.id_json, service_.finish(*reply.call));
}

// --- Relais ----------------------------------------------------------------------------------

LiveRelay::LiveRelay(ILiveTransport& transport, std::function<std::string()> token)
    : transport_(transport), token_(std::move(token)) {}

namespace {

const char* kNoDesktopHint =
    "ouvrez OpenStitch Desktop, activez le compagnon (menu Aide ▸ Pont IA local) puis relancez le "
    "client MCP ; sans desktop, utilisez openstitch-mcp sans --live (mode autonome)";

} // namespace

std::optional<std::string> LiveRelay::attach() {
    const json req{{"v", kLiveVersion}, {"id", next_id_++}, {"token", token_()}, {"op", "attach"}};
    const auto line = transport_.exchange(req.dump());
    if (!line) {
        return std::string("OpenStitch Desktop est injoignable : ") + kNoDesktopHint;
    }
    const json reply = json::parse(*line, nullptr, false);
    if (reply.is_discarded() || !reply.value("ok", false)) {
        const std::string message = reply.is_object() && reply.contains("error")
                                        ? reply.at("error").value("message", std::string("refusé"))
                                        : std::string("réponse illisible");
        return "connexion au desktop refusée : " + message;
    }
    return std::nullopt;
}

std::string LiveRelay::call(const std::string& tool, const std::string& args_json) {
    const json args = args_json.empty() ? json::object() : json::parse(args_json, nullptr, false);
    const json req{{"v", kLiveVersion}, {"id", next_id_++},
                   {"token", token_()}, {"op", "call"},
                   {"tool", tool},      {"args", args.is_discarded() ? json::object() : args}};
    const auto line = transport_.exchange(req.dump());
    if (!line) {
        return error_content(tool, po::code::kSessionClosed,
                             "OpenStitch Desktop ne répond plus (application fermée ?)",
                             kNoDesktopHint)
            .dump();
    }
    const json reply = json::parse(*line, nullptr, false);
    if (reply.is_discarded() || !reply.is_object()) {
        return error_content(tool, po::code::kInternal, "réponse du desktop illisible").dump();
    }
    if (!reply.value("ok", false)) {
        const json err = reply.value("error", json::object());
        return error_content(tool, err.value("code", std::string("E_INTERNAL")).c_str(),
                             err.value("message", std::string("refusé")),
                             err.value("hint", std::string{}))
            .dump();
    }
    return reply.at("result").dump();
}

// --- Découverte ------------------------------------------------------------------------------

std::string make_live_token() {
    std::random_device rd;
    constexpr std::string_view digits = "0123456789abcdef";
    std::string out;
    for (int i = 0; i < 8; ++i) { // 256 bits
        std::uint32_t v = rd();
        for (int k = 0; k < 8; ++k) {
            out.push_back(digits[v & 0xFU]);
            v >>= 4;
        }
    }
    return out;
}

} // namespace openstitch::mcp
