// SPDX-License-Identifier: Apache-2.0
#include "openstitch/project_ops/report.hpp"

#include <algorithm>
#include <map>
#include <set>

#include <nlohmann/json.hpp>

#include "openstitch/project_ops/inspect.hpp"
#include "openstitch/stitch/sequence.hpp"
#include "openstitch/stitch_analysis/metrics.hpp"
#include "openstitch/stitch_analysis/project_metrics.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

namespace openstitch::project_ops {

namespace {

using json = nlohmann::ordered_json;

double mm(Micrometers um) {
    return static_cast<double>(um.value) / 1000.0;
}

} // namespace

std::string json_quote(std::string_view text) {
    return json(std::string(text)).dump();
}

Result<std::string> operations_catalog_text(const std::string& catalog_json) {
    const json j = json::parse(catalog_json, nullptr, false);
    if (j.is_discarded() || !j.contains("operations") || !j["operations"].is_array()) {
        return fail(ErrorCategory::Internal, "catalogue d'opérations illisible");
    }
    std::string out;
    for (const auto& op : j["operations"]) {
        out += op.value("name", std::string("?")) + " : " + op.value("summary", std::string()) + "
";
        if (op.contains("args") && op["args"].is_array()) {
            for (const auto& a : op["args"]) {
                out += std::string("    ") + (a.value("required", false) ? "* " : "  ") +
                       a.value("name", std::string("?")) + " (" + a.value("type", std::string("?"));
                if (!a.value("unit", std::string()).empty()) {
                    out += ", " + a.value("unit", std::string());
                }
                out += ")";
                if (!a.value("range", std::string()).empty()) {
                    out += " " + a.value("range", std::string());
                }
                out += " - " + a.value("description", std::string()) + "
";
            }
        }
    }
    return out;
}

Result<std::string> quality_report_text(const std::string& quality_json) {
    const json j = json::parse(quality_json, nullptr, false);
    if (j.is_discarded() || !j.contains("summary") || !j.contains("findings")) {
        return fail(ErrorCategory::Internal, "rapport qualité illisible");
    }
    const auto& sum = j["summary"];
    std::string out = "Contrôle qualité : " + std::to_string(sum.value("errors", 0)) +
                      " erreur(s), " + std::to_string(sum.value("warnings", 0)) +
                      " avertissement(s) -> " + (sum.value("passed", false) ? "OK" : "ÉCHEC") +
                      "
";
    for (const auto& f : j["findings"]) {
        out += "  [" + f.value("severity", std::string("?")) + "] " +
               f.value("category", std::string("?")) + " : " + f.value("message", std::string()) +
               "
";
    }
    return out;
}

std::string error_json(const std::string& command, const std::string& code,
                       const std::string& message, const std::string& hint) {
    json j{{"schema", kErrorSchema},
           {"ok", false},
           {"command", command},
           {"error", json{{"code", code}, {"message", message}, {"hint", hint}}}};
    return j.dump(2);
}

Result<std::string> digitize_report_json(const document::Project& project,
                                         const DigitizeReport& report,
                                         const DigitizeSettings& settings,
                                         const std::string& imagePath,
                                         const std::string& projectPath) {
    const auto sequence = stitch_generation::effective_sequence(project);
    if (!sequence) {
        return std::unexpected(sequence.error());
    }
    const auto stats = stitch::compute_stats(*sequence);
    const auto metrics = stitch_analysis::sequence_metrics(*sequence, {});
    stitch_analysis::ProjectMetricsOptions pmo;
    if (report.background_skipped && report.background_candidate) {
        pmo.excluded_rgb = report.background_candidate->rgb;
    }
    const auto quality = stitch_analysis::project_metrics(project, *sequence, pmo);

    json j = json::object();
    j["schema"] = kDigitizeReportSchema;
    j["ok"] = true;
    j["image"] = json{{"path", imagePath},
                      {"width_px", report.image_width_px},
                      {"height_px", report.image_height_px},
                      {"had_alpha", report.image_had_alpha},
                      {"dpi", settings.dpi}};
    j["project"] = json{{"path", projectPath}};
    j["settings"] = json{{"mode", settings.mode},
                         {"max_colors", settings.max_colors},
                         {"min_region_px", settings.min_region_px},
                         {"smoothing_px", settings.smoothing_px},
                         {"skip_background", settings.skip_background},
                         {"trim_threshold_mm", settings.trim_threshold_mm},
                         {"lock", settings.lock},
                         {"hidden_travel", settings.hidden_travel},
                         {"detail", settings.detail},
                         {"technique", settings.technique},
                         {"curve_tolerance_mm", settings.curve_tolerance_mm},
                         {"satin_bands", settings.satin_bands}};
    json background = json::object();
    background["skipped"] = report.background_skipped;
    background["decision"] = report.background_auto ? "automatic" : "explicit";
    if (report.background_candidate) {
        const auto& c = report.background_candidate;
        background["candidate"] = json{{"color", rgb_to_hex(c->rgb)},
                                       {"area_ratio", c->area_ratio},
                                       {"lightness", c->lightness},
                                       {"sides_touched", c->sides_touched},
                                       {"recommended", c->recommended}};
    } else {
        background["candidate"] = nullptr;
    }
    j["segmentation"] = json{{"regions", report.regions}, {"background", background}};

    json ids = json::object();
    for (const auto& e : project.embroidery_objects) {
        ids[stitch_type_name(e)].push_back(e.id.value);
    }
    j["objects"] = json{{"vector", report.vector_objects},
                        {"total", report.embroidery_objects},
                        {"satin", report.satin},
                        {"tatami", report.tatami},
                        {"running", report.running},
                        {"ids_by_type", ids}};
    j["sequence"] = json{{"stitches", stats.stitches},
                         {"jumps", stats.jumps},
                         {"trims", stats.trims},
                         {"color_changes", stats.color_changes},
                         {"width_mm", mm(stats.bounds.max.x - stats.bounds.min.x)},
                         {"height_mm", mm(stats.bounds.max.y - stats.bounds.min.y)},
                         {"thread_m", stats.thread_length_um / 1.0e6}};
    json fill = json::object();
    for (const auto& [deg, n] : quality.fill_angles_deg) {
        fill[std::to_string(deg)] = n;
    }
    j["quality"] = json{{"moves", metrics.moves},
                        {"long_moves_without_trim", metrics.long_moves_without_trim},
                        {"short_stitches", metrics.short_stitches},
                        {"small_objects", quality.small_objects},
                        {"uncovered_ratio", quality.uncovered_ratio ? json(*quality.uncovered_ratio)
                                                                    : json(nullptr)},
                        {"fill_angles_deg", fill}};
    json warnings = json::array();
    for (const auto& w : report.warnings) {
        warnings.push_back(w);
    }
    j["warnings"] = warnings;
    if (report.contour) {
        const auto& c = *report.contour;
        j["contours"] = json{{"components", c.components},
                             {"segments", c.segments},
                             {"junctions", c.junctions},
                             {"endpoints", c.endpoints},
                             {"running_length_mm", c.running_length_mm},
                             {"fallbacks", c.fallbacks},
                             {"rejected", c.rejected}};
    }
    if (report.stipple) {
        const auto& s = *report.stipple;
        j["stipple"] = json{{"points", s.points},
                            {"polylines", s.polylines},
                            {"tour_length_mm", s.tour_length_mm}};
    }
    return j.dump(2);
}

Result<std::string> diff_projects_json(const document::Project& a, const document::Project& b) {
    InspectOptions options;
    options.with_stitches = true;
    options.full_params = true;
    const auto sa = inspect_project_json(a, options);
    if (!sa) {
        return std::unexpected(sa.error());
    }
    const auto sb = inspect_project_json(b, options);
    if (!sb) {
        return std::unexpected(sb.error());
    }
    const json ja = json::parse(*sa);
    const json jb = json::parse(*sb);

    json out = json::object();
    out["schema"] = kDiffSchema;
    json sections = json::object();
    for (const char* key : {"canvas", "resolution", "finishing", "counts", "sequence"}) {
        if (ja[key] != jb[key]) {
            sections[key] = json{{"from", ja[key]}, {"to", jb[key]}};
        }
    }
    out["project"] = sections;

    const auto index = [](const json& doc) {
        std::map<std::uint64_t, const json*> byId;
        for (const auto& o : doc["embroidery_objects"]) {
            byId[o["id"].get<std::uint64_t>()] = &o;
        }
        return byId;
    };
    const auto ia = index(ja);
    const auto ib = index(jb);
    json added = json::array();
    json removed = json::array();
    json changed = json::array();
    for (const auto& [id, o] : ib) {
        if (ia.find(id) == ia.end()) {
            added.push_back(json{{"id", id}, {"type", (*o)["type"]}, {"name", (*o)["name"]}});
        }
    }
    for (const auto& [id, o] : ia) {
        const auto it = ib.find(id);
        if (it == ib.end()) {
            removed.push_back(json{{"id", id}, {"type", (*o)["type"]}, {"name", (*o)["name"]}});
            continue;
        }
        json fields = json::object();
        for (const auto& [k, v] : o->items()) {
            const auto& other = *it->second;
            if (!other.contains(k) || other[k] != v) {
                fields[k] = json{{"from", v}, {"to", other.contains(k) ? other[k] : json(nullptr)}};
            }
        }
        if (!fields.empty()) {
            changed.push_back(json{{"id", id}, {"fields", fields}});
        }
    }
    out["objects"] = json{{"added", added}, {"removed", removed}, {"changed", changed}};
    out["identical"] = sections.empty() && added.empty() && removed.empty() && changed.empty();
    return out.dump(2);
}

Result<std::string> operation_json_from_args(std::string_view op,
                                             const std::vector<std::string>& kv) {
    json j = json::object();
    j["op"] = std::string(op);
    for (const auto& item : kv) {
        const auto eq = item.find('=');
        if (eq == std::string::npos || eq == 0) {
            return fail(ErrorCategory::UserInput,
                        "argument « " + item + " » invalide : forme attendue clé=valeur",
                        "ex. --arg row_spacing_mm=0.45 --arg params='{\"angle_deg\":30}'");
        }
        const std::string key = item.substr(0, eq);
        const std::string value = item.substr(eq + 1);
        // Littéral JSON (nombre, booléen, tableau, objet, null) sinon chaîne brute ; une chaîne
        // entre guillemets est lue comme chaîne JSON.
        json parsed = json::parse(value, nullptr, /*allow_exceptions=*/false);
        if (parsed.is_discarded()) {
            parsed = value;
        }
        if (j.contains(key)) {
            return fail(ErrorCategory::UserInput, "argument « " + key + " » répété");
        }
        j[key] = std::move(parsed);
    }
    return j.dump();
}

} // namespace openstitch::project_ops
