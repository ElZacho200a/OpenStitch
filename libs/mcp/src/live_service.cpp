// SPDX-License-Identifier: Apache-2.0
// Mode live : politique d'outils, instantané, approbation et commit sur le projet de l'hôte.
// Voir live.hpp et docs/adr/0001-compagnon-ia-session-live.md.
#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <random>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

#include "live_agent.hpp"
#include "openstitch/mcp/live.hpp"
#include "openstitch/project_ops/errors.hpp"
#include "openstitch/project_ops/report.hpp"
#include "tools.hpp"

namespace openstitch::mcp {

namespace po = openstitch::project_ops;
namespace fs = std::filesystem;

namespace {

fs::path path_from_utf8(const std::string& text) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::string utf8_text(const fs::path& path) {
    const auto u8 = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::string lower(std::string text) {
    for (char& ch : text) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return text;
}

// Note ajoutée à la description des outils de fichier en mode live (la description d'origine parle
// du projet « de la session »).
const char* live_file_note(std::string_view tool) {
    if (tool == "project_digitize") {
        return " [Mode live : calculé sur une copie puis REMPLACE le document ouvert en une étape "
               "d'annulation, après l'accord de l'utilisateur (aperçu avant/après). « out » est "
               "refusé : enregistrez ensuite avec project_save.]";
    }
    if (tool == "project_open") {
        return " [Mode live : demande l'accord de l'utilisateur ; remplace le document ouvert "
               "(nouvelle session, historique vierge) ; refusé si des modifications ne sont pas "
               "enregistrées sauf discard_changes=true. Relire live_status ensuite.]";
    }
    if (tool == "project_save") {
        return " [Mode live : écrit le document OUVERT en .osp après l'accord de l'utilisateur. "
               "Sans « path » : le chemin courant du document ; extension .osp obligatoire.]";
    }
    if (tool == "project_export") {
        return " [Mode live : exporte le document OUVERT après l'accord de l'utilisateur ; "
               "l'extension du fichier doit correspondre au format.]";
    }
    return "";
}

constexpr std::array<std::string_view, 17> kReadTools = {
    "digitize_guide",   "project_operations",  "project_inspect",   "project_select",
    "project_palette",  "project_report",      "project_diagnose",  "project_density",
    "project_fidelity", "project_fabric_risk", "project_locate",    "project_satin_compare",
    "project_check",    "project_render",      "semantic_validate", "ai_backends",
    "semantic_map"};

const std::vector<LiveExtraTool> kExtraTools = {
    {"live_status",
     "État du projet OUVERT dans OpenStitch Desktop : nom, modifié ou non, jamais enregistré, "
     "sélection courante (identifiants d'objets de broderie), session et révision. À appeler avant "
     "de modifier : une modification sur une révision périmée est refusée (E_STALE_REVISION).",
     R"({"type":"object","properties":{}})"},
    {"live_context",
     "Paquet de contexte COMPACT du projet ouvert : session/révision, sélection, cadre, palette "
     "des "
     "FILS cousus (pas les teintes de segmentation), objets à identifiants stables (type, fil, "
     "verrou, visibilité) et risques. À appeler avant de cibler ou de modifier.",
     R"({"type":"object","properties":{}})"},
    {"live_target",
     "Cible des objets de broderie par teinte de FIL : « orange », « rouge »… (familles), "
     "« #RRGGBB » (tolérance), ou « selection » (sélection du canevas). Signale "
     "verrouillés/masqués, "
     "fils à la frontière d'une famille et cas ambigus (ambiguous=true : appeler live_ask_user "
     "AVANT de modifier).",
     R"({"type":"object","properties":{"query":{"type":"string"},"tolerance":{"type":"number"}},)"
     R"("required":["query"]})"},
    {"live_object_views",
     "Vues ZOOMÉES (rendu réaliste recadré) de 1 à 8 objets de broderie pour comprendre ce que "
     "chacun REPRÉSENTE (analyse sémantique : pétale, feuille, contour, ombre…). Retourne une "
     "légende (type, fil, famille) puis une image par objet. Lecture seule.",
     R"({"type":"object","properties":{"objects":{"type":"array","items":{"type":"integer",)"
     R"("minimum":1},"minItems":1,"maxItems":8},"margin":{"type":"number"}},)"
     R"("required":["objects"]})"},
    {"live_plan_satin",
     "Plan de conversion en satin pour 1 à 20 objets : compare skeleton / parallel_sweep / "
     "medial_axis (copies isolées), choisit la meilleure variante non rejetée, JUSTIFIE chaque "
     "refus "
     "(le tatami est conservé), saute verrouillés/masqués/déjà satin, et prépare le lot "
     "(auto_satin + pull_mode=predicted par défaut) validé en simulation. N'applique rien. "
     "Donne la largeur de colonne (width_mm) de chaque cible et refuse les formes trop larges "
     "(moyenne > 6 mm ou max > 9 mm) sauf allow_wide=true.",
     R"({"type":"object","properties":{"objects":{"type":"array","items":{"type":"integer",)"
     R"("minimum":1},"minItems":1,"maxItems":20},"pull_mode":{"enum":["predicted","adaptive",)"
     R"("auto","manual","none"]},"allow_wide":{"type":"boolean"}},"required":["objects"]})"},
    {"live_preview",
     "Aperçu AVANT/APRÈS d'un lot d'opérations sur une copie : rapport du lot, points, sauts, "
     "coupes, constats qualité, risque textile (avant, après, écart) et rendu de l'état après. Le "
     "projet ouvert ne change pas. Zoom AVANT application avec objects=[id,...] (cadrage "
     "automatique sur 1 à 20 objets), ou crop=[x0,y0,x1,y1] (coordonnées normalisées). "
     "px_per_mm règle la résolution, sans modifier le projet.",
     R"({"type":"object","properties":{"operations":{"type":"array","items":{"type":"object",)"
     R"("properties":{"op":{"type":"string"}},"required":["op"]}},"objects":{"type":"array",)"
     R"("items":{"type":"integer","minimum":1},"minItems":1,"maxItems":20},)"
     R"("crop":{"type":"array","items":{"type":"number"},"minItems":4,"maxItems":4},)"
     R"("px_per_mm":{"type":"number"}},"required":["operations"]})"},
    {"live_permission",
     "Outil INTERNE du compagnon : décide si Claude Code peut lire un fichier hors du projet "
     "(Read, Glob, Grep seulement). L'utilisateur est invité à autoriser chaque accès ; ne "
     "l'appelez pas vous-même.",
     R"({"type":"object","properties":{"tool_name":{"type":"string"},"input":{"type":"object"},)"
     R"("tool_use_id":{"type":"string"}},"required":["tool_name","input"]})"},
    {"live_ask_user",
     "Pose une question à l'utilisateur DANS LE PANNEAU du compagnon quand la demande est ambiguë "
     "(plusieurs teintes, sélection vide…). 2 à 8 options, chacune avec ses identifiants d'objets. "
     "Retourne le choix ; ne modifie rien.",
     R"({"type":"object","properties":{"question":{"type":"string"},"options":{"type":"array",)"
     R"("minItems":2,"maxItems":8,"items":{"type":"object","properties":{"label":{"type":"string"},)"
     R"("object_ids":{"type":"array","items":{"type":"integer"}}},"required":["label"]}}},)"
     R"("required":["question","options"]})"},
};

json error_content(const std::string& tool, const char* code, const std::string& message,
                   const std::string& hint = {}) {
    return json{
        {"content", json::array({json{{"type", "text"},
                                      {"text", po::error_json(tool, code, message, hint)}}})},
        {"isError", true}};
}

// Chemins qu'aucun accord ne peut ouvrir : secrets et identifiants.
bool sensitive_path(std::string path) {
    for (char& ch : path) {
        ch = ch == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    for (const char* marker :
         {"/.ssh/", "/.gnupg/", "/.aws/", "/.azure/", "/.kube/", "id_rsa", "id_ed25519",
          ".claude.json", "/.claude/.credentials", "credentials", "/.env", "known_hosts"}) {
        if (path.find(marker) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// Résultat d'outil trop long que Claude Code a lui-même enregistré pour l'espace de travail
// du compagnon : …/.claude/projects/<…studio-companion-workspace…>/<session>/tool-results/<f>.
// Le relire est la suite normale d'un appel MCP, jamais une ouverture d'autre chose.
bool own_tool_result(std::string path) {
    for (char& ch : path) {
        ch = ch == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (path.find("/../") != std::string::npos || path.ends_with("/..")) {
        return false;
    }
    const std::string root = "/.claude/projects/";
    const auto at = path.find(root);
    if (at == std::string::npos) {
        return false;
    }
    // Segments après projects/ : <projet>/<session>/tool-results/<fichier>, exactement.
    std::vector<std::string> parts;
    std::size_t begin = at + root.size();
    while (begin <= path.size()) {
        const auto end = path.find('/', begin);
        parts.push_back(
            path.substr(begin, end == std::string::npos ? std::string::npos : end - begin));
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    return parts.size() == 4 && parts[0].find("studio-companion-workspace") != std::string::npos &&
           !parts[1].empty() && parts[2] == "tool-results" && !parts[3].empty();
}

std::string normalized_path(std::string path) {
    for (char& ch : path) {
        ch = ch == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    while (path.size() > 1 && path.back() == '/') {
        path.pop_back();
    }
    return path;
}

json permission_reply(bool allow, const json& input, const std::string& message = {}) {
    json decision = allow ? json{{"behavior", "allow"}, {"updatedInput", input}}
                          : json{{"behavior", "deny"}, {"message", message}};
    return json{{"content", json::array({json{{"type", "text"}, {"text", decision.dump()}}})},
                {"isError", false}};
}

json text_content(const std::string& text) {
    return json{{"content", json::array({json{{"type", "text"}, {"text", text}}})},
                {"isError", false}};
}

} // namespace

LivePolicy live_policy(std::string_view tool) {
    if (tool.starts_with("live_") && tool != "live_commit") {
        for (const auto& extra : kExtraTools) {
            if (tool == extra.name) {
                return LivePolicy::Read;
            }
        }
        return LivePolicy::Denied;
    }
    if (std::find(kReadTools.begin(), kReadTools.end(), tool) != kReadTools.end()) {
        return LivePolicy::Read;
    }
    // Import sémantique : crée / remplace des objets depuis un manifeste ; calculé sur une copie
    // puis installé en une étape d'annulation, comme project_apply.
    if (tool == "project_apply" || tool == "semantic_import" || tool == "semantic_replace") {
        return LivePolicy::Apply;
    }
    if (tool == "project_digitize") {
        return LivePolicy::Replace;
    }
    if (tool == "project_open" || tool == "project_save" || tool == "project_export") {
        return LivePolicy::HostFile;
    }
    return LivePolicy::Denied;
}

const std::vector<LiveExtraTool>& live_extra_tools() {
    return kExtraTools;
}

std::string live_tools_json() {
    json list = json::array();
    for (const auto& tool : tools()) {
        if (live_policy(tool.name) == LivePolicy::Denied) {
            continue;
        }
        list.push_back(json{{"name", tool.name},
                            {"description", std::string(tool.description) +
                                                live_file_note(tool.name)},
                            {"input_schema", json::parse(tool.input_schema)}});
    }
    for (const auto& extra : live_extra_tools()) {
        list.push_back(json{{"name", extra.name},
                            {"description", extra.description},
                            {"input_schema", json::parse(extra.input_schema)}});
    }
    return list.dump();
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
    LiveContextInput context;         // live_context / live_target
    std::optional<ChoiceRequest> ask; // live_ask_user (réponse au retour sur le fil GUI)
    std::shared_ptr<const document::Project> final_project; // issu d'un project_apply réussi
    ApplyPreview apply_preview; // rendus et écarts montrés avant l'approbation
    // Outils de fichier (HostFile) : cible validée, format, fichier d'export calculé hors du fil
    // GUI dans un dossier temporaire (copié vers la cible seulement après approbation).
    std::string target_text;
    std::string format;
    fs::path target;
    fs::path temp_file;
    bool dirty_host{false};
    bool ran{false};

    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    ~Impl() {
        if (!temp_file.empty()) {
            std::error_code ec;
            fs::remove_all(temp_file.parent_path(), ec);
        }
    }
};

LivePendingCall::LivePendingCall() : impl_(std::make_unique<Impl>()) {}
LivePendingCall::~LivePendingCall() = default;

void LivePendingCall::run() {
    Impl& c = *impl_;
    if (c.immediate || c.ran) {
        return;
    }
    c.ran = true;
    if (c.tool.starts_with("live_")) {
        if (c.ask || c.permission) {
            return; // question à l'utilisateur : rien à calculer, la réponse vient au retour GUI
        }
        try {
            if (c.tool == "live_context") {
                c.result = live_context_tool(*c.snapshot.project, c.context);
            } else if (c.tool == "live_target") {
                c.result = live_target_tool(*c.snapshot.project, c.context, c.args);
            } else if (c.tool == "live_object_views") {
                c.result = live_object_views_tool(*c.snapshot.project, c.args);
            } else if (c.tool == "live_plan_satin") {
                c.result = live_plan_satin_tool(*c.snapshot.project, c.args);
            } else {
                c.result = live_preview_tool(*c.snapshot.project, c.args);
            }
        } catch (const std::exception& e) {
            c.result.is_error = true;
            c.result.content.push_back(
                json{{"type", "text"},
                     {"text", po::error_json(c.tool, po::code::kInternal, e.what(), {})}});
        }
        return;
    }
    if (c.policy == LivePolicy::HostFile) {
        try {
            if (c.tool == "project_open") { // contrôle le fichier avant de déranger l'utilisateur
                Session probe;
                c.result = call_tool(probe, c.tool, c.args);
            } else if (c.tool == "project_export") { // calcul hors du fil GUI, dossier temporaire
                std::error_code ec;
                std::random_device rd;
                const fs::path dir = fs::temp_directory_path(ec) /
                                     ("openstitch-live-" + std::to_string(rd()) +
                                      std::to_string(rd()));
                fs::create_directories(dir, ec);
                if (ec) {
                    c.result.is_error = true;
                    c.result.content.push_back(
                        json{{"type", "text"},
                             {"text", po::error_json(c.tool, po::code::kIo,
                                                     "dossier temporaire inaccessible", {})}});
                    return;
                }
                c.temp_file = dir / ("export." + c.format);
                json args = c.args;
                args["path"] = utf8_text(c.temp_file);
                Session session;
                session.project = *c.snapshot.project;
                c.result = call_tool(session, c.tool, args);
            }
        } catch (const std::exception& e) {
            c.result.is_error = true;
            c.result.content.push_back(
                json{{"type", "text"},
                     {"text", po::error_json(c.tool, po::code::kInternal, e.what(), {})}});
        }
        return;
    }
    Session session;
    session.project = *c.snapshot.project; // copie privée : le document hôte n'est jamais touché
    c.result = call_tool(session, c.tool, c.args);
    if (c.policy == LivePolicy::Replace && !c.result.is_error && session.project) {
        c.final_project = std::make_shared<const document::Project>(std::move(*session.project));
        try {
            c.apply_preview = build_apply_preview(*c.snapshot.project, *c.final_project);
        } catch (const std::exception&) {
            // Confort seulement : l'échec de l'aperçu n'empêche pas la demande d'approbation.
        }
    } else if (c.policy == LivePolicy::Apply && !c.dry_run && !c.result.is_error &&
               session.dirty) {
        c.final_project = std::make_shared<const document::Project>(std::move(*session.project));
        try {
            if (c.want_preview) {
                c.apply_preview = build_apply_preview(*c.snapshot.project, *c.final_project);
            }
        } catch (const std::exception&) {
            // Confort seulement : l'échec de l'aperçu n'empêche pas la demande d'approbation.
        }
    } else if (c.policy == LivePolicy::Apply && !c.dry_run && !c.result.is_error && session.dirty) {
        c.final_project = std::make_shared<const document::Project>(std::move(*session.project));
        try {
            if (c.want_preview) {
                c.apply_preview = build_apply_preview(*c.snapshot.project, *c.final_project);
            }
        } catch (const std::exception&) {
            // L'aperçu est un confort : son échec n'empêche jamais la demande d'approbation.
        }
    }
}

// --- Service ---------------------------------------------------------------------------------

bool LiveService::grant_read_dir(const std::string& directory) {
    std::error_code ec;
    const fs::path dir = path_from_utf8(directory);
    if (directory.empty() || sensitive_path(directory + "/") || !fs::is_directory(dir, ec)) {
        return false;
    }
    const std::string key = normalized_path(directory);
    if (std::find(granted_dirs_.begin(), granted_dirs_.end(), key) == granted_dirs_.end()) {
        granted_dirs_.push_back(key);
    }
    return true;
}

LiveService::LiveService(project_ops::LiveBridge& bridge, IApprovalGate& gate, InfoProvider info,
                         std::function<void()> on_applied)
    : bridge_(bridge), gate_(gate), info_(std::move(info)), on_applied_(std::move(on_applied)) {}

std::shared_ptr<LivePendingCall> LiveService::begin(const std::string& tool,
                                                    const std::string& args_json) {
    auto call = std::make_shared<LivePendingCall>();
    auto& c = *call->impl_;
    c.tool = tool;
    c.want_preview = gate_.wants_preview(tool);

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
            "créer ou fermer un projet se fait depuis OpenStitch Desktop ; "
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
    if (tool == "live_permission") {
        const auto text_of = [](const json& j, const char* key) {
            return j.contains(key) && j.at(key).is_string() ? j.at(key).get<std::string>()
                                                            : std::string{};
        };
        const std::string name = text_of(c.args, "tool_name");
        const json input = c.args.contains("input") && c.args.at("input").is_object()
                               ? c.args.at("input")
                               : json::object();
        std::string path = text_of(input, "file_path");
        if (path.empty()) {
            path = text_of(input, "path");
        }
        if (name != "Read" && name != "Glob" && name != "Grep") {
            c.immediate = permission_reply(
                false, input, "seuls Read, Glob et Grep sont autorisés : utilisez les outils MCP");
        } else if (!path.empty() && sensitive_path(path)) {
            c.immediate = permission_reply(
                false, input, "accès refusé : ce chemin contient des secrets ou identifiants");
        } else if (path.empty() || own_tool_result(path) ||
                   read_allowed_.contains(name + "|" + path) ||
                   std::any_of(granted_dirs_.begin(), granted_dirs_.end(),
                               [&path](const std::string& dir) {
                                   const std::string p = normalized_path(path);
                                   // « .. » sortirait du dossier accordé malgré le préfixe.
                                   const bool climbs = p.find("/../") != std::string::npos ||
                                                       p.ends_with("/..") || p.starts_with("../");
                                   return !climbs && (p == dir || p.starts_with(dir + "/"));
                               })) {
            c.immediate = permission_reply(true, input); // espace de travail, ou déjà accordé
        } else {
            c.permission = std::make_pair(name, path);
            c.permission_input = input;
        }
        return call;
    }
    if (tool == "live_ask_user") {
        ChoiceRequest ask;
        ask.question = c.args.value("question", std::string{});
        if (c.args.contains("options") && c.args.at("options").is_array()) {
            for (const auto& o : c.args.at("options")) {
                ChoiceOption option;
                option.label = o.value("label", std::string{});
                if (o.contains("object_ids") && o.at("object_ids").is_array()) {
                    for (const auto& id : o.at("object_ids")) {
                        if (id.is_number_unsigned()) {
                            option.object_ids.push_back(id.get<std::uint64_t>());
                        }
                    }
                }
                if (!option.label.empty() && ask.options.size() < 8) {
                    ask.options.push_back(std::move(option));
                }
            }
        }
        if (ask.question.empty() || ask.options.size() < 2) {
            c.immediate = error_content(tool, po::code::kUsage,
                                        "« question » et 2 à 8 « options » (avec « label ») requis",
                                        "voir inputSchema de l'outil");
        } else {
            c.ask = std::move(ask);
        }
        return call;
    }
    if (c.policy == LivePolicy::Replace || c.policy == LivePolicy::HostFile) {
        const LiveDocumentInfo doc = info_ ? info_() : LiveDocumentInfo{};
        const auto fail = [&](const char* code, const std::string& message,
                              const std::string& hint) {
            c.immediate = error_content(tool, code, message, hint);
        };
        const auto text_arg = [&c](const char* key) {
            return c.args.contains(key) && c.args.at(key).is_string()
                       ? c.args.at(key).get<std::string>()
                       : std::string{};
        };
        const auto flag_arg = [&c](const char* key) {
            return c.args.contains(key) && c.args.at(key).is_boolean() && c.args.at(key).get<bool>();
        };
        const bool no_clobber = flag_arg("no_clobber");
        c.dirty_host = doc.dirty;
        if (tool == "project_digitize") {
            if (!text_arg("out").empty()) {
                fail(po::code::kUsage, "« out » n'est pas accepté en mode live",
                     "la numérisation remplace le document ouvert ; enregistrez ensuite avec "
                     "project_save");
            }
        } else if (tool == "project_open") {
            if (!host_.open_project) {
                fail(po::code::kRefused, "l'ouverture de projet n'est pas disponible dans cet hôte",
                     "ouvrez le fichier depuis OpenStitch");
            } else if (text_arg("path").empty()) {
                fail(po::code::kUsage, "« path » obligatoire", "chemin d'un fichier .osp");
            } else if (doc.dirty && !flag_arg("discard_changes")) {
                fail(po::code::kRefused,
                     "le document ouvert a des modifications non enregistrées : abandon refusé",
                     "appelez project_save, ou passez discard_changes=true pour confirmer");
            } else {
                c.target_text = text_arg("path");
                c.target = path_from_utf8(c.target_text);
            }
        } else if (tool == "project_save") {
            std::string path = text_arg("path");
            if (path.empty() && host_.current_path) {
                path = host_.current_path();
            }
            if (!host_.save_project) {
                fail(po::code::kRefused, "l'enregistrement n'est pas disponible dans cet hôte",
                     "enregistrez depuis OpenStitch");
            } else if (path.empty()) {
                fail(po::code::kUsage, "aucun chemin connu pour ce projet", "passez « path »");
            } else if (lower(path_from_utf8(path).extension().string()) != ".osp") {
                fail(po::code::kUsage, "l'extension du fichier doit être .osp",
                     "ex. C:/Users/…/motif.osp");
            } else if (no_clobber && fs::exists(path_from_utf8(path))) {
                fail(po::code::kIo, "« " + path + " » existe déjà (no_clobber)", "");
            } else {
                c.target_text = path;
                c.target = path_from_utf8(path);
            }
        } else { // project_export
            const std::string given = text_arg("format");
            c.format = lower(given.empty() ? std::string("dst") : given);
            const std::string path = text_arg("path");
            if (path.empty()) {
                fail(po::code::kUsage, "« path » obligatoire", "chemin du fichier à écrire");
            } else if (lower(path_from_utf8(path).extension().string()) != "." + c.format) {
                fail(po::code::kUsage,
                     "l'extension du fichier doit correspondre au format ." + c.format,
                     "ex. motif." + c.format);
            } else if (no_clobber && fs::exists(path_from_utf8(path))) {
                fail(po::code::kIo, "« " + path + " » existe déjà (no_clobber)", "");
            } else {
                c.target_text = path;
                c.target = path_from_utf8(path);
            }
        }
        if (c.immediate) {
            return call;
        }
    }
    c.snapshot = bridge_.snapshot();
    if (tool == "live_context" || tool == "live_target") {
        const LiveDocumentInfo info = info_ ? info_() : LiveDocumentInfo{};
        c.context.document_name = info.name;
        c.context.dirty = info.dirty;
        c.context.never_saved = info.never_saved;
        c.context.selection = info.selection;
        c.context.revision = c.snapshot.revision;
        c.context.session_id = c.snapshot.session_id;
    }
    return call;
}

std::string LiveService::finish(LivePendingCall& call) {
    auto& c = *call.impl_;
    if (c.immediate) {
        return c.immediate->dump();
    }
    if (c.ask) {
        const int index = gate_.choose(*c.ask);
        json answer{{"schema", "openstitch.live-choice/1"}, {"chosen", false}};
        if (index >= 0 && static_cast<std::size_t>(index) < c.ask->options.size()) {
            const auto& picked = c.ask->options[static_cast<std::size_t>(index)];
            answer = json{{"schema", "openstitch.live-choice/1"},
                          {"chosen", true},
                          {"index", index},
                          {"label", picked.label},
                          {"object_ids", picked.object_ids}};
        } else {
            answer["note"] = "l'utilisateur n'a rien choisi : ne modifiez rien et demandez-lui de "
                             "préciser sa demande";
        }
        json reply = text_content(answer.dump());
        reply["live"] = json{{"session_id", bridge_.session_id()},
                             {"revision", bridge_.revision()},
                             {"applied", false}};
        return reply.dump();
    }
    if (c.permission) {
        const auto& [name, path] = *c.permission;
        ApprovalRequest request;
        request.tool = "live_permission";
        request.label = "IA : accès à un fichier";
        request.items.push_back({0, std::string(name == "Read" ? "lire " : "parcourir ") + path});
        request.warnings.push_back(
            "hors du projet : le contenu lu est envoyé à Claude ; refusez si ce n'est pas voulu");
        request.session_id = bridge_.session_id();
        request.revision = bridge_.revision();
        json reply;
        if (gate_.approve(request)) {
            read_allowed_.insert(name + "|" + path);
            reply = permission_reply(true, c.permission_input);
        } else {
            reply = permission_reply(false, c.permission_input,
                                     "l'utilisateur a refusé l'accès à ce fichier");
        }
        reply["live"] = json{{"session_id", bridge_.session_id()},
                             {"revision", bridge_.revision()},
                             {"applied", false}};
        return reply.dump();
    }
    json out{{"content", c.result.content}, {"isError", c.result.is_error}};
    bool applied = false;
    const auto refuse = [&](const char* code, const std::string& message,
                            const std::string& hint) {
        out = error_content(c.tool, code, message, hint);
    };

    if (c.policy == LivePolicy::HostFile && !c.result.is_error) {
        if (bridge_.closed() || c.snapshot.session_id != bridge_.session_id()) {
            refuse(po::code::kSessionClosed,
                   "le document ouvert a été fermé ou remplacé : l'action n'est pas exécutée",
                   "relisez live_status sur le nouveau document (rien n'a été modifié)");
        } else if (c.snapshot.revision != bridge_.revision()) {
            refuse(po::code::kStaleRevision,
                   "le projet a changé pendant le calcul : l'action n'est pas exécutée",
                   "relisez live_status puis recommencez (rien n'a été modifié)");
        } else {
            std::error_code ec;
            const bool exists = fs::exists(c.target, ec);
            ApprovalRequest request;
            request.tool = c.tool;
            request.session_id = c.snapshot.session_id;
            request.revision = c.snapshot.revision;
            if (c.tool == "project_open") {
                request.label = "IA : ouvrir un projet";
                request.items.push_back({0, "ouvrir " + c.target_text});
                request.warnings.push_back(
                    "remplace le document ouvert ; l'historique d'annulation repart de zéro");
                if (c.dirty_host) {
                    request.warnings.push_back(
                        "les modifications non enregistrées du document actuel seront PERDUES "
                        "(non annulable)");
                }
            } else if (c.tool == "project_save") {
                request.label = "IA : enregistrer le projet";
                request.items.push_back({0, "enregistrer sous " + c.target_text});
                if (exists) {
                    request.warnings.push_back("le fichier existe déjà : il sera remplacé");
                }
            } else {
                request.label = "IA : exporter en " + c.format;
                request.items.push_back({0, "exporter vers " + c.target_text});
                const auto size = fs::file_size(c.temp_file, ec);
                if (!ec) {
                    request.metrics.push_back("Taille : " + std::to_string(size) + " octets");
                }
                if (exists) {
                    request.warnings.push_back("le fichier existe déjà : il sera remplacé");
                }
            }
            if (!gate_.approve(request)) {
                refuse(po::code::kApprovalDenied, "l'utilisateur a refusé l'action",
                       "rien n'a été écrit ni ouvert ; demandez ce qui ne convient pas");
            } else if (c.tool == "project_open") {
                std::string error;
                if (host_.open_project(c.target_text, error)) {
                    out = text_content(
                        json{{"ok", true},
                             {"path", c.target_text},
                             {"note", "ouverture lancée : le document est remplacé (nouvelle "
                                      "session) ; relisez live_status avant de continuer"}}
                            .dump());
                } else {
                    refuse(po::code::kIo, error.empty() ? "ouverture impossible" : error, "");
                }
            } else if (c.tool == "project_save") {
                std::string error;
                if (host_.save_project(c.target_text, error)) {
                    out = text_content(json{{"ok", true}, {"path", c.target_text}}.dump());
                } else {
                    refuse(po::code::kIo, error.empty() ? "enregistrement impossible" : error, "");
                }
            } else { // export : le fichier est prêt, il est installé de façon atomique
                fs::path part = c.target;
                part += ".part";
                fs::copy_file(c.temp_file, part, fs::copy_options::overwrite_existing, ec);
                if (!ec) {
                    fs::rename(part, c.target, ec);
                }
                if (ec) {
                    std::error_code ignore;
                    fs::remove(part, ignore);
                    refuse(po::code::kIo, "écriture impossible : " + ec.message(), "");
                } else {
                    out = text_content(json{{"ok", true},
                                            {"format", c.format},
                                            {"path", c.target_text},
                                            {"bytes", fs::file_size(c.target, ec)}}
                                           .dump());
                }
            }
        }
    }

    if (c.final_project) {
        if (bridge_.closed() || c.snapshot.session_id != bridge_.session_id()) {
            refuse(po::code::kSessionClosed,
                   "le document ouvert a été fermé ou remplacé : la modification n'est pas "
                   "appliquée",
                   "relisez live_status sur le nouveau document (rien n'a été modifié)");
        } else if (c.snapshot.revision != bridge_.revision()) {
            refuse(po::code::kStaleRevision,
                   "le projet a changé pendant le calcul : la modification n'est pas appliquée",
                   "relisez live_status puis recommencez (rien n'a été modifié)");
        } else {
            ApprovalRequest request;
            request.tool = c.tool;
            request.label = c.policy == LivePolicy::Replace ? "IA : numérisation d'une image"
                                                            : "IA : lot d'opérations";
            request.report_json = c.result.content.empty()
                                      ? std::string{}
                                      : c.result.content.front().value("text", std::string{});
            if (c.policy == LivePolicy::Replace) {
                const std::string image =
                    c.args.contains("image") && c.args.at("image").is_string()
                        ? c.args.at("image").get<std::string>()
                        : std::string("?");
                request.items.push_back({0, "numériser « " + image + " »"});
                request.warnings.push_back(
                    "remplace tout le contenu du document ouvert (annulable avec Ctrl+Z)");
            }
            if (c.tool == "semantic_import" || c.tool == "semantic_replace") {
                const std::string manifest =
                    c.args.contains("manifest") && c.args.at("manifest").is_string()
                        ? c.args.at("manifest").get<std::string>()
                        : std::string("?");
                request.items.push_back({0, c.tool + " « " + manifest + " »"});
            }
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
            request.before_png = c.apply_preview.before_png;
            request.after_png = c.apply_preview.after_png;
            request.metrics = c.apply_preview.metrics;
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
    json perms{{"read", json::array()},
               {"apply", json::array({"project_apply", "project_digitize", "semantic_import",
                                      "semantic_replace"})},
               {"file", json::array({"project_open", "project_save", "project_export"})}};
    for (const auto& t : kReadTools) {
        perms["read"].push_back(std::string(t));
    }
    for (const auto& extra : kExtraTools) {
        perms["read"].push_back(extra.name);
    }
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
    if (line.size() > kLiveMaxRequestBytes) {
        reply.immediate =
            reply_error("null", "E_USAGE", "requête trop volumineuse", "limite : 8 Mo par ligne");
        return reply;
    }
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
    "ouvrez OpenStitch, panneau Compagnon IA ▸ Activer le pont (ou Aide ▸ Pont IA local dans le "
    "bureau classique) puis relancez le "
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
