// SPDX-License-Identifier: Apache-2.0
// Outils d'édition agentique du mode live (voir live_agent.hpp).
#include "live_agent.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <format>
#include <map>
#include <optional>
#include <set>

#include "openstitch/project_ops/analysis.hpp"
#include "openstitch/project_ops/errors.hpp"
#include "openstitch/project_ops/inspect.hpp"
#include "openstitch/project_ops/operations.hpp"
#include "openstitch/project_ops/quality.hpp"
#include "openstitch/project_ops/render.hpp"
#include "openstitch/project_ops/report.hpp"
#include "openstitch/stitch/sequence.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

namespace openstitch::mcp {

namespace po = openstitch::project_ops;

namespace {

struct Hsv {
    double h{0}; // degrés [0, 360[
    double s{0};
    double v{0};
};

Hsv to_hsv(std::uint8_t r8, std::uint8_t g8, std::uint8_t b8) {
    const double r = r8 / 255.0;
    const double g = g8 / 255.0;
    const double b = b8 / 255.0;
    const double mx = std::max({r, g, b});
    const double mn = std::min({r, g, b});
    const double d = mx - mn;
    Hsv out;
    out.v = mx;
    out.s = mx <= 0.0 ? 0.0 : d / mx;
    if (d > 0.0) {
        if (mx == r) {
            out.h = 60.0 * std::fmod((g - b) / d, 6.0);
        } else if (mx == g) {
            out.h = 60.0 * ((b - r) / d + 2.0);
        } else {
            out.h = 60.0 * ((r - g) / d + 4.0);
        }
        if (out.h < 0) {
            out.h += 360.0;
        }
    }
    return out;
}

std::string hex_of(const std::array<std::uint8_t, 3>& rgb) {
    return std::format("#{:02X}{:02X}{:02X}", rgb[0], rgb[1], rgb[2]);
}

struct Family {
    const char* name;
    double from; // inclus, degrés
    double to;   // exclu
};

constexpr std::array<Family, 8> kHueFamilies = {{{"rouge", 345.0, 375.0}, // 345..360 + 0..15
                                                 {"orange", 15.0, 45.0},
                                                 {"jaune", 45.0, 70.0},
                                                 {"vert", 70.0, 170.0},
                                                 {"cyan", 170.0, 200.0},
                                                 {"bleu", 200.0, 260.0},
                                                 {"violet", 260.0, 300.0},
                                                 {"rose", 300.0, 345.0}}};

// Distance (degrés) de `h` au bord le plus proche de la famille, négative si hors famille.
double hue_margin(const Family& f, double h) {
    double x = h;
    if (f.to > 360.0 && x < f.from) {
        x += 360.0; // rouge : 345..375
    }
    if (x < f.from || x >= f.to) {
        return -1.0;
    }
    return std::min(x - f.from, f.to - x);
}

const std::map<std::string, std::string>& aliases() {
    static const std::map<std::string, std::string> a = {
        {"rouge", "rouge"},    {"red", "rouge"}, {"orange", "orange"}, {"jaune", "jaune"},
        {"yellow", "jaune"},   {"vert", "vert"}, {"green", "vert"},    {"cyan", "cyan"},
        {"turquoise", "cyan"}, {"bleu", "bleu"}, {"blue", "bleu"},     {"violet", "violet"},
        {"purple", "violet"},  {"rose", "rose"}, {"pink", "rose"},     {"marron", "marron"},
        {"brown", "marron"},   {"noir", "noir"}, {"black", "noir"},    {"blanc", "blanc"},
        {"white", "blanc"},    {"gris", "gris"}, {"gray", "gris"},     {"grey", "gris"}};
    return a;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

ToolResult error_tool(const char* tool, const char* code, const std::string& message,
                      const std::string& hint = {}) {
    ToolResult r;
    r.is_error = true;
    r.content.push_back(
        json{{"type", "text"}, {"text", po::error_json(tool, code, message, hint)}});
    return r;
}

ToolResult text_tool(const json& doc) {
    ToolResult r;
    r.content.push_back(json{{"type", "text"}, {"text", doc.dump()}});
    return r;
}

json object_card(const document::EmbroideryObject& e) {
    return json{{"id", e.id.value},
                {"name", e.name},
                {"type", po::stitch_type_name(e)},
                {"color", hex_of(e.rgb)},
                {"family", color_family(e.rgb[0], e.rgb[1], e.rgb[2])},
                {"locked", e.locked},
                {"visible", e.visible}};
}

} // namespace

std::string color_family(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    const Hsv c = to_hsv(r, g, b);
    if (c.v < 0.18) {
        return "noir";
    }
    if (c.s < 0.12) {
        return c.v > 0.85 ? "blanc" : "gris";
    }
    if (c.s < 0.15) {
        return "gris";
    }
    for (const auto& f : kHueFamilies) {
        if (hue_margin(f, c.h) >= 0.0) {
            // Un orange sombre est un marron : la teinte seule ne distingue pas les deux.
            if (std::string(f.name) == "orange" && c.v < 0.55) {
                return "marron";
            }
            return f.name;
        }
    }
    return {};
}

ToolResult live_context_tool(const document::Project& project, const LiveContextInput& in) {
    constexpr std::size_t kMaxObjects = 200;
    json objects = json::array();
    std::map<std::string, std::pair<std::string, std::size_t>> palette; // hex -> (famille, objets)
    std::size_t locked = 0;
    std::size_t hidden = 0;
    std::size_t count = 0;
    for (const auto& e : project.embroidery_objects) {
        ++count;
        locked += e.locked ? 1 : 0;
        hidden += e.visible ? 0 : 1;
        auto& slot = palette[hex_of(e.rgb)];
        slot.first = color_family(e.rgb[0], e.rgb[1], e.rgb[2]);
        ++slot.second;
        if (objects.size() < kMaxObjects) {
            objects.push_back(object_card(e));
        }
    }
    json colors = json::array();
    for (const auto& [hex, v] : palette) {
        colors.push_back(json{{"color", hex}, {"family", v.first}, {"objects", v.second}});
    }
    json risks = json::array();
    if (locked > 0) {
        risks.push_back(std::to_string(locked) + " objet(s) verrouillé(s) : à ne pas modifier");
    }
    if (hidden > 0) {
        risks.push_back(std::to_string(hidden) + " objet(s) masqué(s)");
    }
    if (in.dirty) {
        risks.push_back("le projet a des modifications non enregistrées (état visible = état à "
                        "modifier)");
    }
    if (count > kMaxObjects) {
        risks.push_back("liste d'objets tronquée à " + std::to_string(kMaxObjects) +
                        " : utiliser live_target / project_select");
    }
    return text_tool(
        json{{"schema", "openstitch.live-context/1"},
             {"session_id", in.session_id},
             {"revision", in.revision},
             {"document",
              {{"name", in.document_name}, {"dirty", in.dirty}, {"never_saved", in.never_saved}}},
             {"selection", in.selection},
             {"canvas_mm",
              {{"width", project.canvas.width.value / 1000.0},
               {"height", project.canvas.height.value / 1000.0}}},
             {"object_count", count},
             {"thread_palette", colors},
             {"note",
              "les couleurs sont celles des FILS cousus (objets de broderie), pas les teintes de "
              "segmentation de l'image"},
             {"objects", objects},
             {"risks", risks}});
}

ToolResult live_target_tool(const document::Project& project, const LiveContextInput& in,
                            const json& args) {
    const std::string query = lower(args.value("query", std::string{}));
    if (query.empty()) {
        return error_tool("live_target", po::code::kUsage, "« query » obligatoire",
                          "« orange », « #E8742A », « selection »");
    }
    json candidates = json::array();
    json borderline = json::array();
    json colors = json::array();
    std::set<std::string> seen;
    bool ambiguous = false;
    std::string reason;
    std::string family;

    const bool selection_query = query == "selection" || query == "sélection" ||
                                 query == "cette sélection" || query == "ceux-là" ||
                                 query == "ceux-la" || query == "ces objets";
    if (selection_query) {
        if (in.selection.empty()) {
            ambiguous = true;
            reason = "aucune sélection active : demandez à l'utilisateur de désigner les objets";
        }
        for (const auto id : in.selection) {
            if (const auto* e = project.findEmbroidery(ObjectId{id})) {
                candidates.push_back(object_card(*e));
            }
        }
    } else if (query.starts_with('#')) {
        const auto parsed = [&]() -> std::optional<std::array<std::uint8_t, 3>> {
            if (query.size() != 7) {
                return std::nullopt;
            }
            std::array<std::uint8_t, 3> rgb{};
            for (int i = 0; i < 3; ++i) {
                const auto part = query.substr(1 + static_cast<std::size_t>(i) * 2, 2);
                char* end = nullptr;
                const long v = std::strtol(part.c_str(), &end, 16);
                if (end == nullptr || *end != '\0') {
                    return std::nullopt;
                }
                rgb[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v);
            }
            return rgb;
        }();
        if (!parsed) {
            return error_tool("live_target", po::code::kUsage, "couleur hexadécimale invalide",
                              "format #RRGGBB");
        }
        const double tolerance = args.value("tolerance", 24.0); // distance RVB euclidienne
        for (const auto& e : project.embroidery_objects) {
            const double dr = static_cast<double>(e.rgb[0]) - (*parsed)[0];
            const double dg = static_cast<double>(e.rgb[1]) - (*parsed)[1];
            const double db = static_cast<double>(e.rgb[2]) - (*parsed)[2];
            if (std::sqrt(dr * dr + dg * dg + db * db) <= tolerance) {
                candidates.push_back(object_card(e));
            }
        }
    } else {
        const auto it = aliases().find(query);
        if (it == aliases().end()) {
            ambiguous = true;
            reason = "teinte inconnue « " + query + " » : demandez à l'utilisateur de choisir";
        } else {
            family = it->second;
            for (const auto& e : project.embroidery_objects) {
                const std::string fam = color_family(e.rgb[0], e.rgb[1], e.rgb[2]);
                if (fam != family) {
                    continue;
                }
                const Hsv c = to_hsv(e.rgb[0], e.rgb[1], e.rgb[2]);
                bool edge = false;
                for (const auto& f : kHueFamilies) {
                    if (family == f.name ||
                        (family == "marron" && std::string(f.name) == "orange")) {
                        edge = hue_margin(f, c.h) >= 0.0 && hue_margin(f, c.h) < 5.0;
                    }
                }
                (edge ? borderline : candidates).push_back(object_card(e));
            }
            if (!borderline.empty()) {
                ambiguous = true;
                reason = "des fils sont à la frontière de la famille « " + family +
                         " » : l'utilisateur doit dire s'il faut les inclure";
            }
        }
    }
    for (const auto* list : {&candidates, &borderline}) {
        for (const auto& c : *list) {
            if (seen.insert(c.at("color").get<std::string>()).second) {
                colors.push_back(c.at("color"));
            }
        }
    }
    // Les verrouillés / masqués sont signalés, jamais retirés en silence.
    json blocked = json::array();
    for (const auto& c : candidates) {
        if (c.at("locked").get<bool>() || !c.at("visible").get<bool>()) {
            blocked.push_back(c.at("id"));
        }
    }
    return text_tool(
        json{{"schema", "openstitch.live-target/1"},
             {"query", query},
             {"family", family},
             {"candidates", candidates},
             {"borderline", borderline},
             {"thread_colors", colors},
             {"locked_or_hidden", blocked},
             {"ambiguous", ambiguous},
             {"reason", reason},
             {"next", ambiguous ? "appelez live_ask_user pour faire choisir "
                                  "l'utilisateur dans le panneau, AVANT toute modification"
                                : "vérifiez avec live_plan_satin puis live_preview"}});
}

ToolResult live_object_views_tool(const document::Project& project, const json& args) {
    constexpr std::size_t kMaxViews = 8;
    if (!args.contains("objects") || !args.at("objects").is_array() || args.at("objects").empty() ||
        args.at("objects").size() > kMaxViews) {
        return error_tool("live_object_views", po::code::kUsage,
                          "« objects » : 1 à 8 identifiants d'objets de broderie",
                          "découpez en lots de 8 ; liste des objets : live_context");
    }
    const double margin = std::clamp(args.value("margin", 0.35), 0.05, 2.0);
    // Boîte englobante du motif et de chaque objet (nœuds des contours, µm).
    struct Box {
        double x0{1e18}, y0{1e18}, x1{-1e18}, y1{-1e18};
        void add(double x, double y) {
            x0 = std::min(x0, x);
            y0 = std::min(y0, y);
            x1 = std::max(x1, x);
            y1 = std::max(y1, y);
        }
        [[nodiscard]] bool valid() const { return x1 > x0 && y1 > y0; }
    };
    const auto box_of = [&](const document::EmbroideryObject& e) {
        Box b;
        if (const auto* v = project.findObject(e.source_vector)) {
            for (const auto& set : v->paths) {
                for (const auto& n : set.outer.nodes) {
                    b.add(static_cast<double>(n.pos.x.value), static_cast<double>(n.pos.y.value));
                }
            }
        }
        return b;
    };
    Box all;
    for (const auto& e : project.embroidery_objects) {
        if (!e.visible) {
            continue;
        }
        const Box b = box_of(e);
        if (b.valid()) {
            all.add(b.x0, b.y0);
            all.add(b.x1, b.y1);
        }
    }
    if (!all.valid()) {
        return error_tool("live_object_views", po::code::kNothingToDo,
                          "aucun objet visible à rendre", {});
    }
    ToolResult result;
    json legend = json::array();
    int images = 0;
    Session scratch;
    scratch.project = project;
    for (const auto& item : args.at("objects")) {
        if (!item.is_number_unsigned()) {
            continue;
        }
        const auto* e = project.findEmbroidery(ObjectId{item.get<std::uint64_t>()});
        if (e == nullptr) {
            legend.push_back(json{{"object", item}, {"error", "objet introuvable"}});
            continue;
        }
        const Box b = box_of(*e);
        if (!b.valid()) {
            legend.push_back(json{{"object", e->id.value}, {"error", "géométrie vide"}});
            continue;
        }
        const double w = all.x1 - all.x0;
        const double h = all.y1 - all.y0;
        const double mx = (b.x1 - b.x0) * margin;
        const double my = (b.y1 - b.y0) * margin;
        // Repère image : origine en haut à gauche, Y vers le bas (la géométrie a Y vers le haut).
        const double cx0 = std::clamp((b.x0 - mx - all.x0) / w, 0.0, 1.0);
        const double cx1 = std::clamp((b.x1 + mx - all.x0) / w, 0.0, 1.0);
        const double cy0 = std::clamp(1.0 - (b.y1 + my - all.y0) / h, 0.0, 1.0);
        const double cy1 = std::clamp(1.0 - (b.y0 - my - all.y0) / h, 0.0, 1.0);
        if (cx1 - cx0 < 1e-4 || cy1 - cy0 < 1e-4) {
            legend.push_back(json{{"object", e->id.value}, {"error", "zone de rendu vide"}});
            continue;
        }
        ToolResult view = call_tool(scratch, "project_render",
                                    json{{"px_per_mm", 4.0}, {"crop", {cx0, cy0, cx1, cy1}}});
        bool attached = false;
        for (auto& block : view.content) {
            if (!view.is_error && block.value("type", std::string{}) == "image") {
                result.content.push_back(std::move(block));
                attached = true;
                ++images;
                break;
            }
        }
        json entry{{"object", e->id.value},
                   {"name", e->name},
                   {"type", po::stitch_type_name(*e)},
                   {"thread", hex_of(e->rgb)},
                   {"family", color_family(e->rgb[0], e->rgb[1], e->rgb[2])},
                   {"image_index", attached ? json(images - 1) : json(nullptr)}};
        legend.push_back(std::move(entry));
    }
    // Le texte vient en tête : les images suivent dans l'ordre de `legend` (image_index = rang).
    json doc{{"schema", "openstitch.live-object-views/1"},
             {"views", legend},
             {"note", "chaque image est un zoom réaliste de l'objet (ordre = image_index) ; nomme "
                      "ce que l'objet REPRÉSENTE (pétale, feuille, contour…) en te "
                      "servant aussi du contexte autour"}};
    result.content.insert(result.content.begin(), json{{"type", "text"}, {"text", doc.dump()}});
    return result;
}

ToolResult live_plan_satin_tool(const document::Project& project, const json& args) {
    if (!args.contains("objects") || !args.at("objects").is_array() || args.at("objects").empty() ||
        args.at("objects").size() > 20) {
        return error_tool("live_plan_satin", po::code::kUsage,
                          "« objects » : 1 à 20 identifiants d'objets de broderie",
                          "découpez en lots de 20 ; ciblage : live_target");
    }
    const std::string pull = args.value("pull_mode", std::string("predicted"));
    json targets = json::array();
    json operations = json::array();
    std::size_t convert = 0;
    std::size_t refused = 0;
    std::size_t skipped = 0;
    for (const auto& item : args.at("objects")) {
        if (!item.is_number_unsigned()) {
            continue;
        }
        const ObjectId id{item.get<std::uint64_t>()};
        json target{{"object", id.value}};
        const auto* emb = project.findEmbroidery(id);
        if (emb == nullptr) {
            target["verdict"] = "refused";
            target["reasons"] = json::array({"objet introuvable"});
            targets.push_back(target);
            ++refused;
            continue;
        }
        target["name"] = emb->name;
        target["current_type"] = po::stitch_type_name(*emb);
        json reasons = json::array();
        if (emb->locked) {
            reasons.push_back("objet verrouillé : non modifié sans décision de l'utilisateur");
        }
        if (!emb->visible) {
            reasons.push_back("objet masqué");
        }
        if (std::string(po::stitch_type_name(*emb)) == "auto_satin") {
            reasons.push_back("déjà en auto-satin");
        }
        if (!reasons.empty()) {
            target["verdict"] = "skipped";
            target["reasons"] = reasons;
            targets.push_back(target);
            ++skipped;
            continue;
        }
        // Compare les trois stratégies avec l'outil existant (copies isolées).
        Session scratch;
        scratch.project = project;
        const ToolResult compared = call_tool(
            scratch, "project_satin_compare",
            json{{"objects", json::array({id.value})},
                 {"strategies", json::array({"skeleton", "parallel_sweep", "medial_axis"})}});
        std::string best;
        double best_cover = -1.0;
        std::size_t best_jumps = 0;
        json trials = json::array();
        json rejections = json::array();
        if (!compared.is_error && !compared.content.empty()) {
            const json doc =
                json::parse(compared.content.front().value("text", "{}"), nullptr, false);
            if (doc.is_object() && doc.contains("objects") && !doc.at("objects").empty()) {
                for (const auto& t : doc.at("objects").front().value("trials", json::array())) {
                    const std::string strategy = t.value("strategy", std::string{});
                    const double cover =
                        t.contains("coverage_ratio") && t.at("coverage_ratio").is_number()
                            ? t.at("coverage_ratio").get<double>()
                            : -1.0;
                    const std::size_t jumps = t.value("project_jumps", std::size_t{0});
                    const bool ok = t.value("status", std::string{}) == "ok" &&
                                    t.value("rejected_because", json::array()).empty();
                    trials.push_back(
                        json{{"strategy", strategy},
                             {"status", t.value("status", std::string{})},
                             {"coverage_ratio", cover < 0 ? json(nullptr) : json(cover)},
                             {"columns", t.value("columns", 0)},
                             {"jumps", jumps},
                             {"rejected_because", t.value("rejected_because", json::array())},
                             {"message", t.value("message", std::string{})}});
                    if (!ok) {
                        for (const auto& r : t.value("rejected_because", json::array())) {
                            rejections.push_back(strategy + " : " + r.get<std::string>());
                        }
                        if (t.contains("message") && !t.at("message").get<std::string>().empty()) {
                            rejections.push_back(strategy + " : " +
                                                 t.at("message").get<std::string>());
                        }
                        continue;
                    }
                    if (cover > best_cover + 1e-9 ||
                        (std::abs(cover - best_cover) <= 1e-9 && jumps < best_jumps)) {
                        best = strategy;
                        best_cover = cover;
                        best_jumps = jumps;
                    }
                }
            }
        }
        target["trials"] = trials;
        if (best.empty()) {
            target["verdict"] = "refused";
            target["reasons"] = rejections.empty()
                                    ? json::array({"aucune variante de satin exploitable"})
                                    : rejections;
            target["decision"] = "tatami conservé";
            ++refused;
        } else {
            target["verdict"] = "convert";
            target["strategy"] = best;
            target["coverage_ratio"] = best_cover < 0 ? json(nullptr) : json(best_cover);
            operations.push_back(
                json{{"op", "auto_satin"}, {"object", id.value}, {"strategy", best}});
            if (pull != "none") {
                operations.push_back(json{
                    {"op", "set_params"}, {"object", id.value}, {"params", {{"pull_mode", pull}}}});
            }
            ++convert;
        }
        targets.push_back(target);
    }
    // Le lot est validé en simulation sur une copie : un refus de set_params (type incompatible)
    // apparaît ici, pas à l'application.
    json dry = nullptr;
    if (!operations.empty()) {
        document::Project copy = project;
        po::BatchOptions options;
        options.dry_run = true;
        const auto report = po::apply_batch(
            copy, json{{"schema", po::kBatchSchema}, {"operations", operations}}.dump(), options);
        dry = json::parse(report.to_json(), nullptr, false);
    }
    return text_tool(json{
        {"schema", "openstitch.live-satin-plan/1"},
        {"targets", targets},
        {"counts", {{"convert", convert}, {"refused", refused}, {"skipped", skipped}}},
        {"pull_mode", pull},
        {"batch", {{"operations", operations}}},
        {"batch_dry_run", dry},
        {"limits",
         json::array({"Predicted est une compensation EMPIRIQUE du tirage (PR #170) : elle "
                      "n'est pas calibrée sur machine (calibration physique en cours, PR "
                      "#152). Un essai sur tissu reste nécessaire.",
                      "Aucune forme n'est garantie convertible en satin : les refus sont "
                      "listés avec leurs motifs et l'objet reste en tatami.",
                      "Appliquez le lot avec project_apply (approbation de l'utilisateur)."})}});
}

namespace {

json measure(const document::Project& project) {
    json out;
    const auto seq = stitch_generation::effective_sequence(project);
    if (!seq) {
        out["error"] = seq.error().message;
        return out;
    }
    const auto totals = stitch::compute_stats(*seq);
    out["stitches"] = totals.stitches;
    out["jumps"] = totals.jumps;
    out["trims"] = totals.trims;
    if (const auto quality = po::build_quality_report(project, {})) {
        out["quality_errors"] = quality->errors;
        out["quality_warnings"] = quality->warnings;
    }
    if (const auto risk = po::measure_fabric_risk(project, {})) {
        out["fabric_risk_max"] = risk->max_score;
        out["fabric_risk_hotspot_mm2"] = risk->hotspot_area_mm2;
    }
    return out;
}

} // namespace

namespace {

std::string signed_delta(std::int64_t before, std::int64_t after) {
    const auto d = after - before;
    return std::to_string(before) + " → " + std::to_string(after) + " (" + (d >= 0 ? "+" : "") +
           std::to_string(d) + ")";
}

std::string render_png(const document::Project& project) {
    po::RenderOptions options;
    options.px_per_mm = 2.0;
    options.max_pixels = 1'000'000;
    options.fabric_rgb = {236, 236, 236}; // fond neutre : les fils ressortent
    const auto rendered = po::render_realistic_png(project, options);
    if (!rendered) {
        return {};
    }
    return std::string(rendered->png.begin(), rendered->png.end());
}

} // namespace

ApplyPreview build_apply_preview(const document::Project& before, const document::Project& after) {
    ApplyPreview preview;
    preview.before_png = render_png(before);
    preview.after_png = render_png(after);
    const json b = measure(before);
    const json a = measure(after);
    const auto line = [&](const char* label, const char* key) {
        if (b.contains(key) && a.contains(key)) {
            preview.metrics.push_back(
                std::string(label) + " : " +
                signed_delta(b.at(key).get<std::int64_t>(), a.at(key).get<std::int64_t>()));
        }
    };
    line("Points", "stitches");
    line("Sauts", "jumps");
    line("Coupes", "trims");
    line("Erreurs qualité", "quality_errors");
    line("Avertissements qualité", "quality_warnings");
    if (b.contains("fabric_risk_hotspot_mm2") && a.contains("fabric_risk_hotspot_mm2")) {
        preview.metrics.push_back("Risque textile (zone chaude, mm²) : " +
                                  std::to_string(static_cast<std::int64_t>(
                                      b.at("fabric_risk_hotspot_mm2").get<double>())) +
                                  " → " +
                                  std::to_string(static_cast<std::int64_t>(
                                      a.at("fabric_risk_hotspot_mm2").get<double>())));
    }
    return preview;
}

ToolResult live_preview_tool(const document::Project& project, const json& args) {
    if (!args.contains("operations") || !args.at("operations").is_array()) {
        return error_tool("live_preview", po::code::kUsage, "« operations » obligatoire (tableau)",
                          "même format que project_apply ; ex. le lot de live_plan_satin");
    }
    const json batch{{"schema", po::kBatchSchema}, {"operations", args.at("operations")}};
    document::Project after = project; // jamais le document : copie isolée
    const auto report = po::apply_batch(after, batch.dump());
    json out{{"schema", "openstitch.live-preview/1"},
             {"report", json::parse(report.to_json(), nullptr, false)}};
    ToolResult result;
    if (!report.ok) {
        result.is_error = true;
        out["note"] = "le lot est refusé : rien n'est appliqué (aperçu sur copie)";
        result.content.push_back(json{{"type", "text"}, {"text", out.dump()}});
        return result;
    }
    const json before_m = measure(project);
    const json after_m = measure(after);
    json delta = json::object();
    for (const char* key : {"stitches", "jumps", "trims", "quality_errors", "quality_warnings"}) {
        if (before_m.contains(key) && after_m.contains(key)) {
            delta[key] = after_m.at(key).get<std::int64_t>() - before_m.at(key).get<std::int64_t>();
        }
    }
    if (before_m.contains("fabric_risk_max") && after_m.contains("fabric_risk_max")) {
        delta["fabric_risk_max"] = after_m.at("fabric_risk_max").get<double>() -
                                   before_m.at("fabric_risk_max").get<double>();
    }
    out["before"] = before_m;
    out["after"] = after_m;
    out["delta"] = delta;
    out["note"] = "aperçu calculé sur une copie : le projet ouvert n'a pas changé";
    result.content.push_back(json{{"type", "text"}, {"text", out.dump()}});
    // Rendu de l'état APRÈS (image) pour que l'agent et l'utilisateur voient le résultat.
    Session scratch;
    scratch.project = std::move(after);
    ToolResult rendered = call_tool(scratch, "project_render", json{{"px_per_mm", 2.0}});
    if (!rendered.is_error) {
        for (auto& block : rendered.content) {
            if (block.value("type", std::string{}) == "image") {
                result.content.push_back(std::move(block));
            }
        }
    }
    return result;
}

} // namespace openstitch::mcp
