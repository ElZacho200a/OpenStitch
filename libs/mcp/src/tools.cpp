// SPDX-License-Identifier: Apache-2.0
#include "tools.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <format>
#include <map>
#include <string_view>

#include "guide.hpp"
#include "openstitch/project_io/machine_file.hpp"
#include "openstitch/project_ops/errors.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "openstitch/project_ops/inspect.hpp"
#include "openstitch/project_ops/operations.hpp"
#include "openstitch/project_ops/project_file.hpp"
#include "openstitch/project_ops/quality.hpp"
#include "openstitch/project_ops/render.hpp"
#include "openstitch/project_ops/report.hpp"
#include "openstitch/stitch/sequence.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

namespace openstitch::mcp {

namespace po = openstitch::project_ops;
namespace fs = std::filesystem;

namespace {

constexpr const char* kNoProject = "E_NO_PROJECT";

// Erreur d'usage levée par les assistants d'arguments ; convertie en résultat d'outil.
struct UsageError {
    std::string message;
    std::string hint;
    std::string code{po::code::kUsage};
};

ToolResult text_result(std::string text, bool is_error = false) {
    return ToolResult{{json{{"type", "text"}, {"text", std::move(text)}}}, is_error};
}

ToolResult error_result(const std::string& tool, const std::string& code,
                        const std::string& message, const std::string& hint = {}) {
    return text_result(po::error_json(tool, code, message, hint), true);
}

ToolResult engine_error(const std::string& tool, const std::string& code, const Error& error) {
    return error_result(tool, code, error.message);
}

std::string req_string(const json& a, const char* key) {
    if (!a.contains(key) || !a.at(key).is_string() || a.at(key).get<std::string>().empty()) {
        throw UsageError{std::format("argument « {} » obligatoire (chaîne non vide)", key),
                         "voir inputSchema de l'outil"};
    }
    return a.at(key).get<std::string>();
}

std::string opt_string(const json& a, const char* key, std::string fallback = {}) {
    return a.contains(key) && !a.at(key).is_null() ? a.at(key).get<std::string>() : fallback;
}

bool opt_bool(const json& a, const char* key, bool fallback) {
    return a.contains(key) && !a.at(key).is_null() ? a.at(key).get<bool>() : fallback;
}

double opt_double(const json& a, const char* key, double fallback) {
    return a.contains(key) && !a.at(key).is_null() ? a.at(key).get<double>() : fallback;
}

// Les chaînes JSON sont en UTF-8 ; `fs::path(std::string)` les lirait en page de code ANSI sous
// Windows (chemins accentués illisibles). On passe donc par `char8_t` dans les deux sens.
fs::path to_path(const std::string& utf8) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::string path_text(const fs::path& path) {
    const auto text = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

// Identifiant d'objet : entier >= 1 (un flottant, un négatif ou 0 sont des erreurs d'usage).
ObjectId object_arg(const json& a, const char* key) {
    const auto& v = a.at(key);
    if (!v.is_number_unsigned() || v.get<std::uint64_t>() == 0) {
        throw UsageError{std::format("« {} » : identifiant entier >= 1 attendu", key),
                         "listez les identifiants avec project_inspect"};
    }
    return ObjectId{v.get<std::uint64_t>()};
}

document::Project& need_project(Session& s) {
    if (!s.project) {
        throw UsageError{"aucun projet ouvert dans la session",
                         "appelez d'abord project_open, project_new ou project_digitize",
                         kNoProject};
    }
    return *s.project;
}

std::string base64(const std::vector<std::uint8_t>& bytes) {
    static constexpr std::string_view kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::uint32_t b0 = bytes[i];
        const std::uint32_t b1 = i + 1 < bytes.size() ? bytes[i + 1] : 0;
        const std::uint32_t b2 = i + 2 < bytes.size() ? bytes[i + 2] : 0;
        const std::uint32_t v = (b0 << 16) | (b1 << 8) | b2;
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += i + 1 < bytes.size() ? kAlphabet[(v >> 6) & 63] : '=';
        out += i + 2 < bytes.size() ? kAlphabet[v & 63] : '=';
    }
    return out;
}

// État de la session + inspection compacte (sans les points) : réponse commune aux outils qui
// ouvrent ou créent un projet.
ToolResult session_state(Session& s, const std::string& tool, json extra = json::object()) {
    po::InspectOptions options;
    options.with_stitches = false;
    const auto inspected = po::inspect_project_json(need_project(s), options);
    if (!inspected) {
        return engine_error(tool, po::code::kInternal, inspected.error());
    }
    json out{{"schema", "openstitch.mcp-session/1"},
             {"ok", true},
             {"path", path_text(s.path)},
             {"dirty", s.dirty},
             {"project", json::parse(*inspected)}};
    for (auto& [k, v] : extra.items()) {
        out[k] = v;
    }
    return text_result(out.dump(2));
}

// Cadre ajusté au motif : l'origine est au centre du cadre, donc la demi-largeur couvre
// l'abscisse la plus éloignée ; `margin_mm` est la marge de sécurité du cadre, auquel s'ajoute
// 1 mm de jeu. Passe par l'opération `set_canvas` (donc par une `ICommand`).
json fit_canvas(document::Project& project, double margin_mm) {
    if (!(margin_mm >= 0.0) || margin_mm > 100.0) {
        throw UsageError{"« margin_mm » doit être dans [0, 100]", ""};
    }
    const auto seq = stitch_generation::effective_sequence(project);
    if (!seq) {
        throw UsageError{seq.error().message, "", po::code::kRefused};
    }
    const auto stats = stitch::compute_stats(*seq);
    if (stats.stitches == 0) {
        throw UsageError{"aucun point : rien à cadrer", "ajoutez des objets avant de cadrer",
                         po::code::kNothingToDo};
    }
    const auto& b = stats.bounds;
    const double half_w = std::max(std::abs(b.min.x.value), std::abs(b.max.x.value)) / 1000.0;
    const double half_h = std::max(std::abs(b.min.y.value), std::abs(b.max.y.value)) / 1000.0;
    const auto round_up = [](double v) { return std::ceil(v * 10.0) / 10.0; };
    const double width = round_up(2.0 * (half_w + margin_mm + 1.0));
    const double height = round_up(2.0 * (half_h + margin_mm + 1.0));
    const json op{
        {"op", "set_canvas"}, {"width_mm", width}, {"height_mm", height}, {"margin_mm", margin_mm}};
    const auto report = po::apply_operation(project, op.dump());
    if (!report.ok) {
        throw UsageError{report.outcomes.empty() ? report.error_message
                                                 : report.outcomes[0].message,
                         "", po::code::kRefused};
    }
    return json{{"width_mm", width}, {"height_mm", height}, {"margin_mm", margin_mm}};
}

// Vue compacte de `project_inspect` : l'inspection complète pèse ~100 Ko pour un projet de
// 100 objets (contours, paramètres, régions), trop pour le contexte d'un agent. On garde le
// cadre, la séquence, les comptes et les objets groupés par type puis couleur (identifiants).
json summarize_inspection(const json& full) {
    json out = json::object();
    for (const char* key :
         {"schema", "canvas", "resolution", "image", "sequence", "counts", "warnings"}) {
        if (full.contains(key)) {
            out[key] = full.at(key);
        }
    }
    json groups = json::object();
    std::size_t objects = 0;
    for (const auto& o : full.value("embroidery_objects", json::array())) {
        groups[o.value("type", "?")][o.value("color", "?")].push_back(o.at("id"));
        ++objects;
    }
    out["embroidery_objects"] = objects;
    out["groups"] = groups;
    out["detail"] = "summary";
    out["hint"] = "detail=full : inspection complète (volumineuse) ; object=ID : un seul objet ; "
                  "project_select : filtrer par couleur, type, aire, zone";
    return out;
}

// ---- Outils ------------------------------------------------------------------------------

ToolResult tool_operations(Session&, const json&) {
    return text_result(po::operations_catalog_json());
}

ToolResult tool_new(Session& s, const json& a) {
    po::NewProjectOptions options;
    options.width_mm = opt_double(a, "width_mm", options.width_mm);
    options.height_mm = opt_double(a, "height_mm", options.height_mm);
    options.margin_mm = opt_double(a, "margin_mm", options.margin_mm);
    options.trim_threshold_mm = opt_double(a, "trim_threshold_mm", options.trim_threshold_mm);
    options.lock = opt_string(a, "lock", options.lock);
    auto created = po::create_empty_project(options);
    if (!created) {
        return error_result("project_new", po::code::kUsage, created.error().message,
                            "lock : none|backforth|triangle|zigzag ; dimensions > 0");
    }
    const fs::path path = to_path(opt_string(a, "path"));
    if (!path.empty()) {
        if (auto w =
                po::write_project(path, *created, nullptr, {}, opt_bool(a, "no_clobber", false));
            !w) {
            return engine_error("project_new", po::code::kIo, w.error());
        }
    }
    s.project = std::move(*created);
    s.info.reset();
    s.path = path;
    s.dirty = path.empty();
    return session_state(s, "project_new");
}

ToolResult tool_open(Session& s, const json& a) {
    const fs::path path = to_path(req_string(a, "path"));
    auto opened = po::open_project(path);
    if (!opened) {
        return engine_error("project_open", po::code::kInvalidProject, opened.error());
    }
    s.project = std::move(opened->project);
    s.info = opened->info;
    s.path = path;
    s.dirty = false;
    return session_state(s, "project_open");
}

// Réglages de numérisation acceptés dans `settings` (mêmes noms et défauts que les options de
// `openstitch-cli project digitize`, `-` remplacé par `_`).
void read_settings(const json& j, po::DigitizeSettings& s) {
    if (!j.is_object()) {
        throw UsageError{"« settings » doit être un objet", ""};
    }
    const auto positive = [](const char* key, double v) {
        if (!(v > 0.0)) {
            throw UsageError{std::format("« {} » doit être > 0", key), ""};
        }
        return v;
    };
    const auto non_negative = [](const char* key, double v) {
        if (v < 0.0) {
            throw UsageError{std::format("« {} » doit être >= 0", key), ""};
        }
        return v;
    };
    const auto member = [](const char* key, const std::string& v,
                           std::initializer_list<const char*> allowed) {
        for (const char* ok : allowed) {
            if (v == ok) {
                return v;
            }
        }
        std::string list;
        for (const char* ok : allowed) {
            list += (list.empty() ? "" : "|") + std::string(ok);
        }
        throw UsageError{std::format("« {} » : valeur « {} » inconnue", key, v),
                         "valeurs : " + list};
    };
    static constexpr std::string_view kKeys =
        "dpi, max_colors, min_region_px, smoothing_px, skip_background, min_feature_mm, "
        "min_region_mm2, mode, detail, technique, curve_tolerance_mm, satin_bands, single_path, "
        "coherent_lines, pre, dark_lines, dark_lines_width_mm, dark_lines_lightness, "
        "island_cleanup_mm2, fill_inset_mm, fill_overlap_mm, solid_block, rings_satin, flow_fill, "
        "trim_threshold_mm, lock";
    for (const auto& [key, v] : j.items()) {
        if (key == "dpi") {
            s.dpi = positive("dpi", v.get<double>());
        } else if (key == "max_colors") {
            s.max_colors = v.get<int>();
            if (s.max_colors < 2 || s.max_colors > 64) {
                throw UsageError{"« max_colors » doit être dans [2, 64]", ""};
            }
        } else if (key == "min_region_px") {
            s.min_region_px = static_cast<int>(positive("min_region_px", v.get<double>()));
        } else if (key == "smoothing_px") {
            s.smoothing_px = static_cast<int>(non_negative("smoothing_px", v.get<double>()));
        } else if (key == "skip_background") {
            const auto b =
                member("skip_background", v.get<std::string>(), {"auto", "yes", "no", "outer"});
            s.skip_background = b == "auto" ? -1 : b == "yes" ? 1 : b == "outer" ? 2 : 0;
        } else if (key == "min_feature_mm") {
            s.min_feature_mm = non_negative("min_feature_mm", v.get<double>());
        } else if (key == "min_region_mm2") {
            s.min_region_mm2 = non_negative("min_region_mm2", v.get<double>());
        } else if (key == "mode") {
            s.mode = member("mode", v.get<std::string>(), {"shapes", "contours", "stipple"});
        } else if (key == "detail") {
            s.detail = non_negative("detail", v.get<double>());
        } else if (key == "technique") {
            s.technique =
                member("technique", v.get<std::string>(), {"auto", "running", "legacy-satin"});
        } else if (key == "curve_tolerance_mm") {
            s.curve_tolerance_mm = non_negative("curve_tolerance_mm", v.get<double>());
        } else if (key == "satin_bands") {
            s.satin_bands = v.get<bool>();
        } else if (key == "single_path") {
            s.single_path = v.get<bool>();
        } else if (key == "coherent_lines") {
            s.coherent_lines = v.get<bool>();
        } else if (key == "pre") {
            s.pre = v.get<std::string>();
            if (auto ok = po::validate_pre_spec(s.pre); !ok) {
                throw UsageError{ok.error().message, "ex. « median:1+flatten:2 »"};
            }
        } else if (key == "dark_lines") {
            s.dark_lines = v.get<bool>();
        } else if (key == "dark_lines_width_mm") {
            s.dark_lines_width_mm = positive("dark_lines_width_mm", v.get<double>());
        } else if (key == "dark_lines_lightness") {
            s.dark_lines_lightness = non_negative("dark_lines_lightness", v.get<double>());
        } else if (key == "island_cleanup_mm2") {
            s.island_cleanup_mm2 = non_negative("island_cleanup_mm2", v.get<double>());
        } else if (key == "fill_inset_mm") {
            s.fill_inset_mm = non_negative("fill_inset_mm", v.get<double>());
        } else if (key == "fill_overlap_mm") {
            s.fill_overlap_mm = non_negative("fill_overlap_mm", v.get<double>());
        } else if (key == "solid_block") {
            s.solid_block = v.get<bool>();
        } else if (key == "rings_satin") {
            s.rings_satin = v.get<bool>();
        } else if (key == "flow_fill") {
            s.flow_fill = v.get<bool>();
        } else if (key == "trim_threshold_mm") {
            s.trim_threshold_mm = positive("trim_threshold_mm", v.get<double>());
        } else if (key == "lock") {
            s.lock =
                member("lock", v.get<std::string>(), {"none", "backforth", "triangle", "zigzag"});
        } else {
            throw UsageError{std::format("réglage inconnu « {} »", key),
                             "réglages valides : " + std::string(kKeys)};
        }
    }
}

ToolResult tool_digitize(Session& s, const json& a) {
    const std::string image = req_string(a, "image");
    po::DigitizeSettings settings;
    if (a.contains("settings")) {
        read_settings(a.at("settings"), settings);
    }
    const std::string out = opt_string(a, "out");
    const fs::path out_path = to_path(out);
    const bool no_clobber = opt_bool(a, "no_clobber", false);
    const bool fit = opt_bool(a, "fit_canvas", true);
    const double margin_mm = opt_double(a, "margin_mm", 3.0);
    if (!(margin_mm >= 0.0) || margin_mm > 100.0) { // avant la numérisation, qui peut être longue
        throw UsageError{"« margin_mm » doit être dans [0, 100]", ""};
    }
    if (!out.empty()) {
        const fs::path parent = out_path.parent_path();
        if (!parent.empty() && !fs::is_directory(parent)) {
            return error_result("project_digitize", po::code::kIo,
                                std::format("dossier de sortie absent : {}", path_text(parent)),
                                "créez le dossier avant la numérisation");
        }
        if (no_clobber && fs::exists(out_path)) {
            return error_result("project_digitize", po::code::kIo,
                                std::format("« {} » existe déjà (no_clobber)", out));
        }
    }
    auto created = po::create_empty_project({});
    if (!created) {
        return engine_error("project_digitize", po::code::kInternal, created.error());
    }
    document::Project project = std::move(*created);
    auto report = po::digitize_image_into(project, to_path(image), settings);
    if (!report) {
        return engine_error("project_digitize", po::code::kRefused, report.error());
    }
    if (report->embroidery_objects == 0) {
        return error_result(
            "project_digitize", po::code::kNothingToDo,
            std::format("aucun objet de broderie généré ({} région(s) segmentée(s))",
                        report->regions),
            "essayez min_region_px plus petit, max_colors plus grand, ou skip_background « no » "
            "si le fond a été ignoré");
    }
    const auto seq = stitch_generation::effective_sequence(project);
    if (!seq) {
        return engine_error("project_digitize", po::code::kRefused, seq.error());
    }
    if (stitch::compute_stats(*seq).stitches == 0) {
        return error_result("project_digitize", po::code::kNothingToDo, "aucun point généré",
                            "modifiez min_region_px ou max_colors");
    }
    json canvas_fit;
    if (fit) { // avant l'écriture : le .osp porte directement le bon cadre
        canvas_fit = fit_canvas(project, margin_mm);
    }
    if (!out.empty()) {
        if (auto w = po::write_project(out_path, project, nullptr, {}, no_clobber); !w) {
            return engine_error("project_digitize", po::code::kIo, w.error());
        }
    }
    const auto doc = po::digitize_report_json(project, *report, settings, image, out);
    if (!doc) {
        return engine_error("project_digitize", po::code::kInternal, doc.error());
    }
    json result = json::parse(*doc);
    json notes = json::array();
    if (fit) {
        result["canvas_fit"] = canvas_fit;
    } else {
        notes.push_back(
            "fit_canvas=false : le cadre reste à 100 x 100 mm ; si le motif est plus "
            "grand, project_check signalera des points hors cadre (project_fit_canvas)");
    }
    if (settings.rings_satin && !settings.dark_lines) {
        notes.push_back("rings_satin n'agit qu'avec dark_lines=true (anneaux SOMBRES reconnus sur "
                        "le masque des pixels sombres)");
    } else if (settings.rings_satin && report->rings_satin == 0) {
        notes.push_back("rings_satin n'a produit aucun anneau : il ne couvre que les anneaux "
                        "sombres ; pour des formes claires allongées, utilisez project_select "
                        "puis project_apply auto_satin");
    }
    if (!notes.empty()) {
        result["notes"] = notes;
    }
    s.project = std::move(project);
    s.info.reset();
    s.path = out_path;
    s.dirty = out.empty();
    return text_result(result.dump(2));
}

ToolResult tool_inspect(Session& s, const json& a) {
    const auto& project = need_project(s);
    po::InspectOptions options;
    options.with_stitches = opt_bool(a, "stitches", false);
    options.with_geometry = opt_bool(a, "geometry", false);
    if (a.contains("object") && !a.at("object").is_null()) {
        const ObjectId id = object_arg(a, "object");
        const auto doc = po::inspect_object_json(project, id, options);
        if (!doc) {
            return error_result("project_inspect", po::code::kUnknownObject, doc.error().message,
                                "listez les identifiants avec project_inspect sans « object »");
        }
        return text_result(*doc);
    }
    const std::string detail = opt_string(a, "detail", "summary");
    if (detail != "summary" && detail != "full") {
        throw UsageError{"« detail » : summary|full", ""};
    }
    if (detail == "summary") {
        options.with_stitches = true; // dimensions et nombre de points du résumé
        options.full_params = false;
    }
    const auto doc = po::inspect_project_json(project, options);
    if (!doc) {
        return engine_error("project_inspect", po::code::kInternal, doc.error());
    }
    if (detail == "full") {
        return text_result(*doc);
    }
    return text_result(summarize_inspection(json::parse(*doc)).dump(2));
}

ToolResult tool_select(Session& s, const json& a) {
    const auto& project = need_project(s);
    if (!a.contains("where") || !a.at("where").is_array()) {
        throw UsageError{"argument « where » obligatoire (tableau de critères)",
                         "ex. [\"color=#E8DBB0\", \"type=tatami\", \"area_gt=20\"]"};
    }
    const auto ids = po::select_objects(project, a.at("where").get<std::vector<std::string>>());
    if (!ids) {
        return error_result("project_select", po::code::kUsage, ids.error().message,
                            "critères : id, color, type, visible, locked, name, area_gt, area_lt, "
                            "within=x0,y0,x1,y1 (mm)");
    }
    json list = json::array();
    for (const auto id : *ids) {
        list.push_back(id.value);
    }
    return text_result(
        json{{"schema", po::kSelectionSchema}, {"count", list.size()}, {"ids", list}}.dump());
}

ToolResult tool_guide(Session&, const json& a) {
    const std::string topic = opt_string(a, "topic");
    if (topic.empty()) {
        return text_result(guide_index_text());
    }
    const GuideTopic* found = find_topic(topic);
    if (found == nullptr) {
        std::string names;
        for (const auto& t : guide_topics()) {
            names += (names.empty() ? "" : ", ") + t.name;
        }
        return error_result("digitize_guide", po::code::kUsage,
                            std::format("sujet inconnu : {}", topic), "sujets : " + names);
    }
    return text_result(found->text);
}

// Palette réellement cousue, dans l'ordre de couture (équivalent de `osp_palette.py --report`
// du skill) : un « bloc » commence à chaque changement de couleur entre deux objets visibles
// consécutifs. À consulter AVANT de juger un rendu.
ToolResult tool_palette(Session& s, const json&) {
    po::InspectOptions options;
    options.with_stitches = false;
    const auto inspected = po::inspect_project_json(need_project(s), options);
    if (!inspected) {
        return engine_error("project_palette", po::code::kInternal, inspected.error());
    }
    const json full = json::parse(*inspected);
    std::map<std::uint64_t, double> area;
    for (const auto& v : full.value("vector_objects", json::array())) {
        area[v.at("id").get<std::uint64_t>()] = v.value("area_mm2", 0.0);
    }
    std::vector<const json*> visible;
    // Références dans `full` (pas dans une copie temporaire : les pointeurs survivent à la boucle).
    if (full.contains("embroidery_objects") && full.at("embroidery_objects").is_array()) {
        for (const auto& e : full.at("embroidery_objects")) {
            if (e.value("visible", true)) {
                visible.push_back(&e);
            }
        }
    }
    std::stable_sort(visible.begin(), visible.end(), [](const json* x, const json* y) {
        return x->value("order", 0) < y->value("order", 0);
    });
    json blocks = json::array();
    std::map<std::string, std::pair<std::size_t, double>> per_color; // objets, aire
    std::string last;
    for (const json* e : visible) {
        const std::string color = e->value("color", "?");
        if (blocks.empty() || last != color) {
            blocks.push_back(color);
            last = color;
        }
        auto& [count, sum] = per_color[color];
        ++count;
        sum += area[e->value("source_vector", std::uint64_t{0})];
    }
    std::map<std::string, double> pixels;
    double total_pixels = 0.0;
    // Sans image source, `segmentation` n'est pas un objet : pas de parts, seulement les comptes.
    if (full.contains("segmentation") && full.at("segmentation").is_object() &&
        full.at("segmentation").contains("regions")) {
        for (const auto& r : full.at("segmentation").at("regions")) {
            pixels[r.value("color", "?")] += r.value("pixels", 0.0);
            total_pixels += r.value("pixels", 0.0);
        }
    }
    for (const auto& [color, ignored] : pixels) {
        per_color.try_emplace(color, std::pair<std::size_t, double>{0, 0.0});
    }
    json colors = json::array();
    for (const auto& [color, entry] : per_color) {
        colors.push_back(json{
            {"color", color},
            {"objects", entry.first},
            {"area_mm2", std::round(entry.second)},
            {"segmentation_pct",
             total_pixels > 0.0 ? std::round(1000.0 * pixels[color] / total_pixels) / 10.0 : 0.0}});
    }
    std::sort(colors.begin(), colors.end(), [](const json& x, const json& y) {
        return x.at("segmentation_pct").get<double>() > y.at("segmentation_pct").get<double>();
    });
    return text_result(json{{"schema", "openstitch.mcp-palette/1"},
                            {"block_count", blocks.size()},
                            {"blocks_in_sewing_order", blocks},
                            {"colors", colors},
                            {"hint", "une couleur à 0 objet mais présente dans la segmentation "
                                     "n'est pas cousue : fond ignoré, ou région fusionnée"}}
                           .dump(2));
}

ToolResult tool_fit_canvas(Session& s, const json& a) {
    auto& project = need_project(s);
    const json canvas = fit_canvas(project, opt_double(a, "margin_mm", 3.0));
    s.dirty = true;
    return text_result(json{{"ok", true}, {"canvas", canvas}}.dump());
}

ToolResult tool_apply(Session& s, const json& a) {
    auto& project = need_project(s);
    if (!a.contains("operations") || !a.at("operations").is_array()) {
        throw UsageError{"argument « operations » obligatoire (tableau)",
                         "chaque élément : {\"op\":\"...\", ...} ; catalogue : project_operations"};
    }
    po::BatchOptions options;
    options.dry_run = opt_bool(a, "dry_run", false);
    const auto make_batch = [](const json& ops) {
        return json{{"schema", po::kBatchSchema}, {"operations", ops}}.dump();
    };
    if (!opt_bool(a, "skip_refused", false)) {
        const auto report = po::apply_batch(project, make_batch(a.at("operations")), options);
        if (report.applied) {
            s.dirty = true;
        }
        return text_result(report.to_json(), !report.ok);
    }

    // skip_refused : le moteur s'arrête à la première opération refusée (tout ou rien). On
    // simule, on écarte l'opération fautive, on recommence, puis on applique le reste d'un coup.
    // Les écartées sont listées avec leur code, message et hint (rien n'est perdu en silence).
    if (a.at("operations").size() > 500) {
        throw UsageError{"skip_refused : 500 opérations au plus par appel",
                         "découpez le lot (chaque refus coûte une simulation complète)"};
    }
    json kept = a.at("operations");
    std::vector<std::size_t> origin(kept.size());
    for (std::size_t i = 0; i < origin.size(); ++i) {
        origin[i] = i;
    }
    json skipped = json::array();
    po::BatchOptions dry;
    dry.dry_run = true;
    while (!kept.empty()) {
        const auto probe = po::apply_batch(project, make_batch(kept), dry);
        if (probe.ok) {
            break;
        }
        const auto bad = std::find_if(probe.outcomes.begin(), probe.outcomes.end(),
                                      [](const po::OperationOutcome& o) { return !o.ok; });
        if (bad == probe.outcomes.end()) { // erreur globale (schéma…) : rien à écarter
            return text_result(probe.to_json(), true);
        }
        skipped.push_back(json{{"index", origin[bad->index]},
                               {"op", bad->op},
                               {"code", bad->code},
                               {"message", bad->message},
                               {"hint", bad->hint}});
        kept.erase(kept.begin() + static_cast<std::ptrdiff_t>(bad->index));
        origin.erase(origin.begin() + static_cast<std::ptrdiff_t>(bad->index));
    }
    json out = json{{"ok", !kept.empty()}, {"applied", false}, {"dry_run", options.dry_run}};
    if (!kept.empty()) {
        const auto report = po::apply_batch(project, make_batch(kept), options);
        out = json::parse(report.to_json());
        if (report.applied) {
            s.dirty = true;
        }
    }
    out["skipped"] = skipped;
    out["requested"] = a.at("operations").size();
    return text_result(out.dump(2), !out.value("ok", false));
}

ToolResult tool_check(Session& s, const json& a) {
    const auto& project = need_project(s);
    po::QualityOptions options;
    if (a.contains("object") && !a.at("object").is_null()) {
        options.only_object = object_arg(a, "object");
        if (project.findEmbroidery(options.only_object) == nullptr) {
            return error_result(
                "project_check", po::code::kUnknownObject,
                std::format("objet de broderie {} introuvable", options.only_object.value),
                "listez les identifiants avec project_inspect");
        }
    }
    const auto report = po::build_quality_report(project, options);
    if (!report) {
        return engine_error("project_check", po::code::kRefused, report.error());
    }
    return text_result(report->json);
}

ToolResult tool_render(Session& s, const json& a) {
    const auto& project = need_project(s);
    po::RenderOptions options;
    options.px_per_mm = opt_double(a, "px_per_mm", 3.0);
    if (!(options.px_per_mm > 0.0) || options.px_per_mm > 50.0) {
        throw UsageError{"« px_per_mm » doit être dans ]0, 50]", ""};
    }
    options.max_pixels = 1'500'000; // PNG ~1,5 Mo : raisonnable pour un contexte d'agent
    if (a.contains("crop") && !a.at("crop").is_null()) {
        const auto& c = a.at("crop");
        if (!c.is_array() || c.size() != 4) {
            throw UsageError{"« crop » : tableau de 4 nombres [x0,y0,x1,y1] dans [0,1]", ""};
        }
        for (std::size_t i = 0; i < 4; ++i) {
            options.crop[i] = c.at(i).get<double>();
        }
        const auto& k = options.crop;
        if (!(k[0] >= 0.0 && k[1] >= 0.0 && k[2] <= 1.0 && k[3] <= 1.0 && k[0] < k[2] &&
              k[1] < k[3])) {
            throw UsageError{"« crop » : 0 <= x0 < x1 <= 1 et 0 <= y0 < y1 <= 1", ""};
        }
    }
    if (a.contains("fabric") && !a.at("fabric").is_null()) {
        const auto rgb = po::rgb_from_hex(a.at("fabric").get<std::string>());
        if (!rgb) {
            throw UsageError{"« fabric » : couleur « #RRGGBB » attendue", ""};
        }
        options.fabric_rgb = *rgb;
    }
    const auto png = po::render_realistic_png(project, options);
    if (!png) {
        return engine_error("project_render", po::code::kRefused, png.error());
    }
    const json meta{{"width_px", png->width},
                    {"height_px", png->height},
                    {"px_per_mm", png->px_per_mm},
                    {"crop", options.crop}};
    ToolResult result;
    result.content.push_back(
        json{{"type", "image"}, {"data", base64(png->png)}, {"mimeType", "image/png"}});
    result.content.push_back(json{{"type", "text"}, {"text", meta.dump()}});
    return result;
}

ToolResult tool_save(Session& s, const json& a) {
    const auto& project = need_project(s);
    const fs::path target = a.contains("path") ? to_path(req_string(a, "path")) : s.path;
    if (target.empty()) {
        throw UsageError{"aucun chemin connu pour ce projet", "passez « path »"};
    }
    const bool same = !s.path.empty() && target.lexically_normal() == s.path.lexically_normal();
    if (auto w = po::write_project(target, project, same && s.info ? &*s.info : nullptr,
                                   same ? s.path : fs::path{}, opt_bool(a, "no_clobber", false));
        !w) {
        return engine_error("project_save", po::code::kIo, w.error());
    }
    s.path = target;
    s.info.reset();
    s.dirty = false;
    return text_result(json{{"ok", true}, {"path", path_text(target)}}.dump());
}

ToolResult tool_export(Session& s, const json& a) {
    const auto& project = need_project(s);
    const fs::path target = to_path(req_string(a, "path"));
    const std::string format = opt_string(a, "format", "dst");
    if (auto w = project_io::export_machine_file(project, format, target); !w) {
        return engine_error("project_export", po::code::kIo, w.error());
    }
    return text_result(json{{"ok", true}, {"format", format}, {"path", path_text(target)}}.dump());
}

const std::vector<Tool> kTools = {
    {"digitize_guide",
     "Guide de numérisation textile OpenStitch (méthode éprouvée : lecture sémantique, choix "
     "satin/tatami/contour, comparaison des trois satins, retouches, mesures, livrables). À "
     "APPELER AVANT de numériser une image. Sans « topic » : le sommaire ; topic=workflow : la "
     "méthode complète ; topic=mcp : correspondance avec les outils ; autres sujets au sommaire.",
     R"({"type":"object","properties":{"topic":{"type":"string"}}})", tool_guide},
    {"project_operations",
     "Catalogue des opérations de modification (nom, arguments, unités, plages, exemple). À lire "
     "avant project_apply.",
     R"({"type":"object","properties":{}})", tool_operations},
    {"project_new", "Crée un projet vide en mémoire (écrit sur disque si « path » est donné).",
     R"({"type":"object","properties":{"path":{"type":"string","description":"fichier .osp à écrire"},)"
     R"("width_mm":{"type":"number"},"height_mm":{"type":"number"},"margin_mm":{"type":"number"},)"
     R"("trim_threshold_mm":{"type":"number"},)"
     R"("lock":{"enum":["none","backforth","triangle","zigzag"]},"no_clobber":{"type":"boolean"}}})",
     tool_new},
    {"project_open", "Ouvre un projet .osp existant (remplace le projet de la session).",
     R"({"type":"object","properties":{"path":{"type":"string"}},"required":["path"]})", tool_open},
    {"project_digitize",
     "Numérise une image en projet .osp éditable (remplace le projet de la session). Retourne le "
     "rapport de numérisation. « settings » : mode (shapes|contours|stipple), max_colors, "
     "min_region_px, smoothing_px, skip_background (auto|yes|no|outer), min_region_mm2, "
     "min_feature_mm, pre, dark_lines, rings_satin, flow_fill, fill_overlap_mm, dpi, lock… "
     "(noms et défauts de la CLI, tirets remplacés par _). Le cadre est ajusté au motif par défaut "
     "(fit_canvas=false pour l'éviter, margin_mm=3). rings_satin exige dark_lines et ne couvre "
     "que les anneaux sombres ; pour du satin sur des formes claires allongées : project_select "
     "puis project_apply auto_satin. Petits détails (trous d'anneaux, points < 3 mm²) : le défaut "
     "min_region_mm2=3 les fusionne avec leur voisin ; abaisser à 0.3 pour les garder.",
     R"({"type":"object","properties":{"image":{"type":"string"},"out":{"type":"string",)"
     R"("description":"fichier .osp à écrire, dans un dossier existant"},)"
     R"("settings":{"type":"object"},"no_clobber":{"type":"boolean"},)"
     R"("fit_canvas":{"type":"boolean"},"margin_mm":{"type":"number"}},"required":["image"]})",
     tool_digitize},
    {"project_inspect",
     "Structure du projet. Par défaut un RÉSUMÉ compact : cadre, dimensions, nombre de points, "
     "objets groupés par type puis couleur (identifiants). detail=full : inspection complète "
     "(~100 Ko, à éviter) ; object=ID : un seul objet (geometry : contours en mm ; stitches : "
     "points).",
     R"({"type":"object","properties":{"object":{"type":"integer","minimum":1},)"
     R"("detail":{"enum":["summary","full"]},"geometry":{"type":"boolean"},)"
     R"("stitches":{"type":"boolean"}}})",
     tool_inspect},
    {"project_select",
     "Identifiants d'objets filtrés (critères combinés en ET) : id, color=#RRGGBB, "
     "type=running|tatami|satin|directional|auto_satin, visible, locked, name, area_gt / area_lt "
     "(mm²), within=x0,y0,x1,y1 (mm). Sert à cibler un lot project_apply.",
     R"({"type":"object","properties":{"where":{"type":"array","items":{"type":"string"}}},)"
     R"("required":["where"]})",
     tool_select},
    {"project_palette",
     "Palette réellement cousue dans l'ordre de couture (blocs de couleur), avec par couleur le "
     "nombre d'objets, l'aire et la part de la segmentation. À lire avant de juger un rendu.",
     R"({"type":"object","properties":{}})", tool_palette},
    {"project_fit_canvas",
     "Ajuste le cadre au motif (origine au centre) avec une marge de sécurité ; supprime les "
     "erreurs « hors-cadre » de project_check.",
     R"({"type":"object","properties":{"margin_mm":{"type":"number"}}})", tool_fit_canvas},
    {"project_apply",
     "Applique un lot d'opérations de façon transactionnelle sur le projet de la session : tout ou "
     "rien, refus nommé (code, message, hint) et opération fautive désignée. dry_run = simulation. "
     "skip_refused=true : écarte les opérations refusées (listées dans « skipped ») et applique le "
     "reste. "
     "Les opérations peuvent porter « as »:\"alias\" et réutiliser « $alias » ensuite. Ne "
     "sauvegarde pas : utiliser project_save.",
     R"({"type":"object","properties":{"operations":{"type":"array","items":{"type":"object",)"
     R"("properties":{"op":{"type":"string"}},"required":["op"]}},"dry_run":{"type":"boolean"},)"
     R"("skip_refused":{"type":"boolean"}},)"
     R"("required":["operations"]})",
     tool_apply},
    {"project_check",
     "Contrôle qualité (points trop courts/longs, sauts, hors cadre…) : constats par objet. "
     "Appeler "
     "après chaque lot de modifications.",
     R"({"type":"object","properties":{"object":{"type":"integer","minimum":1}}})", tool_check},
    {"project_render",
     "Rendu réaliste du projet (image PNG) pour VOIR le résultat. crop = [x0,y0,x1,y1] dans [0,1] "
     "pour zoomer sur une zone ; px_per_mm (défaut 3) est abaissé si l'image dépasse 1,5 Mpx.",
     R"({"type":"object","properties":{"px_per_mm":{"type":"number"},"crop":{"type":"array",)"
     R"("items":{"type":"number"},"minItems":4,"maxItems":4},"fabric":{"type":"string",)"
     R"("description":"couleur du tissu #RRGGBB"}}})",
     tool_render},
    {"project_save",
     "Écrit le projet de la session en .osp (atomique). Sans « path » : le chemin d'ouverture.",
     R"({"type":"object","properties":{"path":{"type":"string"},"no_clobber":{"type":"boolean"}}})",
     tool_save},
    {"project_export", "Exporte le projet en fichier machine (format « dst » par défaut).",
     R"({"type":"object","properties":{"path":{"type":"string"},"format":{"type":"string"}},)"
     R"("required":["path"]})",
     tool_export},
};

} // namespace

const std::vector<Tool>& tools() {
    return kTools;
}

ToolResult call_tool(Session& session, const std::string& name, const json& args) {
    for (const auto& tool : tools()) {
        if (name != tool.name) {
            continue;
        }
        try {
            if (!args.is_null() && !args.is_object()) {
                throw UsageError{"« arguments » doit être un objet JSON",
                                 "voir inputSchema de l'outil"};
            }
            return tool.run(session, args.is_null() ? json::object() : args);
        } catch (const UsageError& e) {
            return error_result(name, e.code, e.message, e.hint);
        } catch (const json::exception& e) {
            return error_result(name, po::code::kUsage, e.what(),
                                "types des arguments : voir inputSchema de l'outil");
        } catch (const std::exception& e) {
            return error_result(name, po::code::kInternal, e.what());
        }
    }
    return error_result(name, "E_UNKNOWN_TOOL", std::format("outil inconnu : {}", name),
                        "liste : tools/list");
}

} // namespace openstitch::mcp
