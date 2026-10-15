// SPDX-License-Identifier: Apache-2.0
// Contrôle qualité d'un projet : aucune mesure inventée ici, tout vient de `stitch_analysis`
// (seuils : `options_from_project`, `AnalysisOptions`) sur la séquence EFFECTIVE.
#include "openstitch/project_ops/quality.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "openstitch/formats/svg.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/polyline.hpp"
#include "openstitch/stitch/sequence.hpp"
#include "openstitch/stitch_analysis/analyze.hpp"
#include "openstitch/stitch_analysis/metrics.hpp"
#include "openstitch/stitch_analysis/object_coverage.hpp"
#include "openstitch/stitch_analysis/project_metrics.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

namespace openstitch::project_ops {

namespace {

// Ordre des clés FIXE : `ordered_json` conserve l'ordre d'insertion (nlohmann::json trierait
// alphabétiquement).
using Json = nlohmann::ordered_json;

double mm(Micrometers v) {
    return static_cast<double>(v.value) / 1000.0;
}

// Arrondi d'affichage (JSON lisible, texte identique d'un appel à l'autre).
double rnd(double v, int decimals = 3) {
    const double k = std::pow(10.0, decimals);
    return std::round(v * k) / k;
}

const char* severity_name(stitch_analysis::Severity s) {
    switch (s) {
    case stitch_analysis::Severity::Error:
        return "error";
    case stitch_analysis::Severity::Warning:
        return "warning";
    case stitch_analysis::Severity::Info:
        return "info";
    }
    return "info";
}

const char* type_name(const document::EmbroideryObject& o) {
    if (o.is_tatami()) {
        return "tatami";
    }
    if (o.is_satin()) {
        return "satin";
    }
    if (o.is_auto_satin()) {
        return "auto_satin";
    }
    if (o.is_directional()) {
        return "directional";
    }
    return "running";
}

// Cadre brodable du canevas : repère centre / Y vers le haut, zone réduite de la marge
// (même règle que le bureau, `main_window_analysis.cpp`).
stitch::BoundsUm hoop_of(const document::Canvas& canvas) {
    const std::int32_t hw = canvas.usable_width().value / 2;
    const std::int32_t hh = canvas.usable_height().value / 2;
    return {Vec2um{Micrometers{-hw}, Micrometers{-hh}}, Vec2um{Micrometers{hw}, Micrometers{hh}}};
}

Json finding_json(const stitch_analysis::Finding& f) {
    Json j;
    j["severity"] = severity_name(f.severity);
    j["category"] = f.category;
    j["message"] = f.message;
    j["object"] = f.object.value;
    j["x_mm"] = rnd(mm(f.location.x));
    j["y_mm"] = rnd(mm(f.location.y));
    j["hint"] = f.hint;
    return j;
}

// Séquence effective ; un projet sans aucun objet visible (le moteur le refuse) donne une
// séquence VIDE, que le rapport signale par un constat « aucun-point » plutôt que par une
// erreur d'API. Toute autre erreur du moteur est propagée.
Result<stitch::StitchSequence> effective_or_empty(const document::Project& project) {
    auto sequence = stitch_generation::effective_sequence(project);
    if (sequence) {
        return sequence;
    }
    const bool anyVisible =
        std::any_of(project.embroidery_objects.begin(), project.embroidery_objects.end(),
                    [](const document::EmbroideryObject& o) { return o.visible; });
    if (!anyVisible && !project.imported_design) {
        return stitch::StitchSequence{};
    }
    return sequence;
}

bool keep(const stitch_analysis::Finding& f, ObjectId only) {
    return !only.valid() || f.object == only;
}

} // namespace

Result<QualityReport> build_quality_report(const document::Project& project,
                                           const QualityOptions& options) {
    if (options.only_object.valid() && project.findEmbroidery(options.only_object) == nullptr) {
        return fail(ErrorCategory::UserInput, "objet de broderie " +
                                                  std::to_string(options.only_object.value) +
                                                  " introuvable");
    }
    // Séquence EFFECTIVE (jamais la séquence brute) : celle de l'aperçu et de l'export.
    auto sequence = effective_or_empty(project);
    if (!sequence) {
        return std::unexpected(sequence.error());
    }

    stitch_analysis::AnalysisOptions analysis = stitch_analysis::options_from_project(project);
    analysis.hoop = hoop_of(project.canvas);

    const stitch::StitchStats stats = stitch::compute_stats(*sequence);
    const bool empty = stats.stitches == 0;

    auto detailed = stitch_analysis::analyze_detailed(*sequence, analysis);
    std::vector<stitch_analysis::Finding> findings;
    for (auto& f : detailed.findings) {
        if (keep(f, options.only_object)) {
            findings.push_back(std::move(f));
        }
    }
    // Séquence sans point : `analyze_detailed` signale déjà l'Error « vide » (catégorie
    // « vide », objet global) ; le rapport est produit quand même, avec ce constat.
    std::vector<stitch_analysis::Finding> paramChecks;
    for (auto& f : stitch_analysis::check_stitch_limits(project)) {
        if (keep(f, options.only_object)) {
            paramChecks.push_back(std::move(f));
        }
    }

    std::size_t errors = 0, warnings = 0, infos = 0;
    for (const auto* list : {&findings, &paramChecks}) {
        for (const auto& f : *list) {
            switch (f.severity) {
            case stitch_analysis::Severity::Error:
                ++errors;
                break;
            case stitch_analysis::Severity::Warning:
                ++warnings;
                break;
            case stitch_analysis::Severity::Info:
                ++infos;
                break;
            }
        }
    }

    stitch_analysis::SequenceMetricsOptions seqOpts;
    seqOpts.trim_threshold = analysis.trim_threshold;
    seqOpts.short_stitch = analysis.min_stitch;
    const stitch_analysis::SequenceMetrics sm =
        stitch_analysis::sequence_metrics(*sequence, seqOpts);

    stitch_analysis::ProjectMetricsOptions pmOpts;
    pmOpts.short_stitch = analysis.min_stitch;
    const stitch_analysis::ProjectMetrics pm =
        stitch_analysis::project_metrics(project, *sequence, pmOpts);

    Json root;
    root["schema"] = kQualitySchema;

    Json summary;
    summary["errors"] = errors;
    summary["warnings"] = warnings;
    summary["infos"] = infos;
    summary["passed"] = errors == 0;
    root["summary"] = summary;

    Json seq;
    seq["stitches"] = stats.stitches;
    seq["jumps"] = stats.jumps;
    seq["trims"] = stats.trims;
    seq["color_changes"] = stats.color_changes;
    const double wmm =
        empty ? 0.0
              : static_cast<double>(stats.bounds.max.x.value - stats.bounds.min.x.value) / 1000.0;
    const double hmm =
        empty ? 0.0
              : static_cast<double>(stats.bounds.max.y.value - stats.bounds.min.y.value) / 1000.0;
    seq["width_mm"] = rnd(wmm);
    seq["height_mm"] = rnd(hmm);
    seq["thread_m"] = rnd(stats.thread_length_um / 1e6);
    seq["moves"] = sm.moves;
    seq["long_moves_without_trim"] = sm.long_moves_without_trim;
    seq["short_stitches"] = sm.short_stitches;
    seq["short_lock_stitches"] = sm.short_lock_stitches;
    Json histogram = Json::array();
    for (const std::size_t n : sm.direction_histogram) {
        histogram.push_back(n);
    }
    seq["direction_histogram_5deg"] = histogram;
    Json between;
    between["count"] = sm.moves_between_objects;
    between["travel_mm"] = rnd(sm.travel_between_objects_um / 1000.0);
    seq["moves_between"] = between;
    Json within;
    within["count"] = sm.moves_within_objects;
    within["travel_mm"] = rnd(sm.travel_within_objects_um / 1000.0);
    seq["moves_within"] = within;
    root["sequence"] = seq;

    Json frame;
    frame["width_mm"] = rnd(mm(project.canvas.usable_width()));
    frame["height_mm"] = rnd(mm(project.canvas.usable_height()));
    frame["margin_mm"] = rnd(mm(project.canvas.margin));
    frame["fits"] = empty || (stats.bounds.min.x.value >= analysis.hoop->min.x.value &&
                              stats.bounds.min.y.value >= analysis.hoop->min.y.value &&
                              stats.bounds.max.x.value <= analysis.hoop->max.x.value &&
                              stats.bounds.max.y.value <= analysis.hoop->max.y.value);
    root["frame"] = frame;

    Json findingsJson = Json::array();
    for (const auto& f : findings) {
        findingsJson.push_back(finding_json(f));
    }
    root["findings"] = findingsJson;
    Json suppressed = Json::object();
    for (const auto& [category, count] : detailed.suppressed) {
        suppressed[category] = count;
    }
    root["suppressed"] = suppressed;

    Json pmj;
    pmj["small_objects"] = pm.small_objects;
    pmj["uncovered_ratio"] = pm.uncovered_ratio ? Json(rnd(*pm.uncovered_ratio, 4)) : Json(nullptr);
    Json angles = Json::object();
    for (const auto& [deg, n] : pm.fill_angles_deg) {
        angles[std::to_string(deg)] = n;
    }
    pmj["fill_angles_deg"] = angles;
    Json movesByKind = Json::object();
    for (const auto& [k, n] : pm.moves_by_kind) {
        movesByKind[k] = n;
    }
    pmj["moves_by_kind"] = movesByKind;
    Json shortByKind = Json::object();
    for (const auto& [k, n] : pm.short_stitches_by_kind) {
        shortByKind[k] = n;
    }
    pmj["short_stitches_by_kind"] = shortByKind;
    root["project_metrics"] = pmj;

    if (options.per_object) {
        // Couverture : valeur exposée SANS verdict (le moteur ne documente aucun seuil).
        const auto coverage = stitch_analysis::object_coverage(project, *sequence);
        Json objects = Json::array();
        for (const auto& c : coverage) {
            if (options.only_object.valid() && c.object != options.only_object) {
                continue;
            }
            const auto* obj = project.findEmbroidery(c.object);
            std::size_t stitchCount = 0;
            for (const auto& cmd : sequence->commands) {
                stitchCount +=
                    (cmd.type == stitch::CommandType::Stitch && cmd.source == c.object) ? 1 : 0;
            }
            std::size_t count = 0;
            for (const auto* list : {&findings, &paramChecks}) {
                for (const auto& f : *list) {
                    count += f.object == c.object ? 1 : 0;
                }
            }
            Json o;
            o["id"] = c.object.value;
            o["name"] = obj != nullptr ? obj->name : std::string{};
            o["type"] = obj != nullptr ? type_name(*obj) : "?";
            o["region_area_mm2"] = rnd(c.region_area_mm2);
            o["covered_ratio"] = c.measured ? Json(rnd(c.covered_ratio, 4)) : Json(nullptr);
            o["overflow_ratio"] = c.measured ? Json(rnd(c.overflow_ratio, 4)) : Json(nullptr);
            o["measured"] = c.measured;
            o["reason"] = c.reason;
            o["stitches"] = stitchCount;
            o["findings_count"] = count;
            objects.push_back(o);
        }
        root["objects"] = objects;
    }

    // Vérifications de paramètres : `check_stitch_limits` (longueur de point demandée hors
    // de la plage du projet). Le moteur ne contrôle ni l'espacement ni la densité : aucun
    // seuil n'est inventé ici.
    Json checks = Json::array();
    for (const auto& f : paramChecks) {
        checks.push_back(finding_json(f));
    }
    root["fill_params_checks"] = checks;

    Json limits;
    limits["min_stitch_mm"] = rnd(mm(analysis.min_stitch));
    limits["max_stitch_mm"] = rnd(mm(analysis.max_stitch));
    limits["max_jump_mm"] = rnd(mm(analysis.max_jump));
    limits["trim_threshold_mm"] = rnd(mm(analysis.trim_threshold));
    limits["max_layer_thickness"] = analysis.max_layer_thickness;
    limits["source"] = "stitch_analysis::options_from_project + AnalysisOptions";
    root["limits"] = limits;

    QualityReport report;
    report.json = root.dump(2);
    report.errors = errors;
    report.warnings = warnings;
    return report;
}

namespace {

std::string xml_escape(const std::string& s) {
    std::string out;
    for (const char c : s) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        default:
            out += c;
        }
    }
    return out;
}

struct SvgBounds {
    double minX{1e300}, minY{1e300}, maxX{-1e300}, maxY{-1e300};
    void grow(double x, double y) {
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
    }
};

// Direction (radians, modulo π) des rangées d'un objet, si le moteur la connaît.
std::optional<double> row_direction(const document::EmbroideryObject& obj) {
    if (const auto* t = std::get_if<document::TatamiParams>(&obj.params)) {
        return t->angle.radians;
    }
    if (const auto* d = std::get_if<document::DirectionalFillParams>(&obj.params)) {
        // Direction moyenne des tangentes des guides (moyenne sur l'angle doublé : la
        // direction est définie modulo π). Sans guide, le moteur se replie sur l'axe de la
        // forme : aucune direction connue ici, la flèche est omise.
        double sx = 0.0, sy = 0.0;
        for (const auto& g : d->guides) {
            const auto line = geometry::flatten(g, Micrometers{50}).points;
            for (std::size_t i = 1; i < line.size(); ++i) {
                const double dx = static_cast<double>((line[i] - line[i - 1]).x.value);
                const double dy = static_cast<double>((line[i] - line[i - 1]).y.value);
                const double len = std::hypot(dx, dy);
                if (len <= 0.0) {
                    continue;
                }
                const double a = std::atan2(dy, dx);
                sx += len * std::cos(2.0 * a);
                sy += len * std::sin(2.0 * a);
            }
        }
        if (sx == 0.0 && sy == 0.0) {
            return std::nullopt;
        }
        return 0.5 * std::atan2(sy, sx);
    }
    return std::nullopt;
}

} // namespace

Result<std::string> render_quality_svg(const document::Project& project,
                                       const QualitySvgOptions& options) {
    if (options.only_object.valid() && project.findEmbroidery(options.only_object) == nullptr) {
        return fail(ErrorCategory::UserInput, "objet de broderie " +
                                                  std::to_string(options.only_object.value) +
                                                  " introuvable");
    }
    // Séquence complète : sert aux constats (inchangés par le filtre) ; la copie `sequence`
    // est celle qui est dessinée.
    auto full = effective_or_empty(project);
    if (!full) {
        return std::unexpected(full.error());
    }
    stitch::StitchSequence sequence = *full;
    if (options.only_object.valid()) {
        std::erase_if(sequence.commands, [&](const stitch::StitchCommand& c) {
            return c.source != options.only_object;
        });
    }

    SvgBounds b;
    for (const auto& c : sequence.commands) {
        b.grow(mm(c.pos.x), -mm(c.pos.y));
    }

    std::string extra;
    const auto onlyVector = [&]() -> ObjectId {
        if (!options.only_object.valid()) {
            return {};
        }
        return project.findEmbroidery(options.only_object)->source_vector;
    }();
    const auto drawn = [&](ObjectId vector) { return !onlyVector.valid() || vector == onlyVector; };

    if (options.outlines) {
        std::vector<ObjectId> done;
        for (const auto& obj : project.embroidery_objects) {
            if (!obj.visible || !drawn(obj.source_vector) ||
                std::find(done.begin(), done.end(), obj.source_vector) != done.end()) {
                continue;
            }
            done.push_back(obj.source_vector);
            const auto* vec = project.findObject(obj.source_vector);
            if (vec == nullptr) {
                continue;
            }
            for (const auto& set : vec->paths) {
                const geometry::PathSet flat = geometry::flatten_to_polygon(set, Micrometers{50});
                std::vector<const geometry::Path*> rings{&flat.outer};
                for (const auto& h : flat.holes) {
                    rings.push_back(&h);
                }
                for (const auto* ring : rings) {
                    std::string d;
                    for (std::size_t i = 0; i < ring->nodes.size(); ++i) {
                        const double x = mm(ring->nodes[i].pos.x);
                        const double y = -mm(ring->nodes[i].pos.y);
                        b.grow(x, y);
                        d += std::format("{}{:.3f},{:.3f}", i == 0 ? "M" : "L", x, y);
                    }
                    extra += "<path d=\"" + d +
                             "Z\" fill=\"none\" stroke=\"#888\" stroke-width=\"0.08\"/>\n";
                }
            }
        }
    }

    if (options.directions) {
        for (const auto& obj : project.embroidery_objects) {
            if (!obj.visible || (options.only_object.valid() && obj.id != options.only_object)) {
                continue;
            }
            const auto dir = row_direction(obj);
            const auto* vec = project.findObject(obj.source_vector);
            if (!dir || vec == nullptr || vec->paths.empty()) {
                continue;
            }
            // Flèche au centre de l'emprise de la région, longueur = 40 % du petit côté.
            SvgBounds rb;
            for (const auto& set : vec->paths) {
                for (const auto& n : set.outer.nodes) {
                    rb.grow(mm(n.pos.x), mm(n.pos.y));
                }
            }
            if (rb.minX > rb.maxX) {
                continue;
            }
            const double cx = (rb.minX + rb.maxX) / 2.0;
            const double cy = (rb.minY + rb.maxY) / 2.0;
            const double half = 0.2 * std::min(rb.maxX - rb.minX, rb.maxY - rb.minY);
            const double ux = std::cos(*dir);
            const double uy = std::sin(*dir);
            const double x0 = cx - ux * half, y0 = -(cy - uy * half);
            const double x1 = cx + ux * half, y1 = -(cy + uy * half);
            const double head = std::min(1.0, half * 0.5);
            // Pointe : deux traits à ±150° de la direction (repère SVG, Y inversé).
            const double a = std::atan2(y1 - y0, x1 - x0);
            const double pa = a + std::numbers::pi * 5.0 / 6.0;
            const double pb = a - std::numbers::pi * 5.0 / 6.0;
            extra += std::format(
                "<path "
                "d=\"M{:.3f},{:.3f}L{:.3f},{:.3f}M{:.3f},{:.3f}L{:.3f},{:.3f}L{:.3f},{:.3f}\" "
                "fill=\"none\" stroke=\"#0066cc\" stroke-width=\"0.25\"><title>objet {} : "
                "direction {:.1f} deg</title></path>\n",
                x0, y0, x1, y1, x1 + head * std::cos(pa), y1 + head * std::sin(pa), x1, y1,
                x1 + head * std::cos(pb), y1 + head * std::sin(pb), obj.id.value,
                std::fmod(*dir * 180.0 / std::numbers::pi + 360.0, 180.0));
            b.grow(x0, y0);
            b.grow(x1, y1);
        }
    }

    if (options.findings) {
        stitch_analysis::AnalysisOptions analysis = stitch_analysis::options_from_project(project);
        analysis.hoop = hoop_of(project.canvas);
        auto all = stitch_analysis::analyze_detailed(*full, analysis).findings;
        for (auto& f : stitch_analysis::check_stitch_limits(project)) {
            all.push_back(std::move(f));
        }
        for (const auto& f : all) {
            if (!keep(f, options.only_object)) {
                continue;
            }
            const char* color = f.severity == stitch_analysis::Severity::Error     ? "#d00000"
                                : f.severity == stitch_analysis::Severity::Warning ? "#e68a00"
                                                                                   : "#0066cc";
            const double x = mm(f.location.x);
            const double y = -mm(f.location.y);
            b.grow(x, y);
            extra +=
                std::format("<circle cx=\"{:.3f}\" cy=\"{:.3f}\" r=\"0.9\" fill=\"none\" "
                            "stroke=\"{}\" stroke-width=\"0.25\"><title>{}: {}</title></circle>\n",
                            x, y, color, f.category, xml_escape(f.message));
        }
    }

    std::string svg = formats::to_diagnostic_svg(sequence);
    // Le diagnostic cadre uniquement les points : on réécrit l'en-tête pour englober aussi
    // les surcouches (contours, flèches, marqueurs).
    if (b.minX <= b.maxX) {
        const double margin = 2.0;
        const double vx = b.minX - margin, vy = b.minY - margin;
        const double vw = b.maxX - b.minX + 2.0 * margin, vh = b.maxY - b.minY + 2.0 * margin;
        const auto eol = svg.find('\n');
        if (eol != std::string::npos) {
            svg.replace(
                0, eol,
                std::format("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"{:.3f} "
                            "{:.3f} {:.3f} {:.3f}\" width=\"{:.3f}mm\" height=\"{:.3f}mm\">",
                            vx, vy, vw, vh, vw, vh));
        }
    }
    const auto close = svg.rfind("</svg>");
    if (close != std::string::npos) {
        svg.insert(close, extra);
    }
    return svg;
}

} // namespace openstitch::project_ops
