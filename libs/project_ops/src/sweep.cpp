// SPDX-License-Identifier: Apache-2.0
#include "openstitch/project_ops/sweep.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>

#include <nlohmann/json.hpp>

#include "openstitch/image/image.hpp"
#include "openstitch/project_ops/analysis.hpp"
#include "openstitch/project_ops/project_file.hpp"
#include "openstitch/project_ops/render.hpp"

namespace openstitch::project_ops {

namespace {

using nlohmann::json;

Error make_error(ErrorCategory category, std::string message) {
    return Error{category, std::move(message), {}};
}

bool parse_double(const std::string& s, double& out) {
    char* end = nullptr;
    errno = 0;
    out = std::strtod(s.c_str(), &end);
    return !s.empty() && end != nullptr && *end == '\0' && errno == 0;
}

bool parse_bool(const std::string& s, bool& out) {
    if (s == "1" || s == "true" || s == "yes" || s == "on" || s == "oui") {
        out = true;
        return true;
    }
    if (s == "0" || s == "false" || s == "no" || s == "off" || s == "non") {
        out = false;
        return true;
    }
    return false;
}

struct Setting {
    const char* key;
    std::function<bool(DigitizeSettings&, const std::string&)> apply;
};

template <typename T>
std::function<bool(DigitizeSettings&, const std::string&)> number(T DigitizeSettings::*m) {
    return [m](DigitizeSettings& s, const std::string& v) {
        double d = 0.0;
        if (!parse_double(v, d)) {
            return false;
        }
        s.*m = static_cast<T>(d);
        return true;
    };
}

std::function<bool(DigitizeSettings&, const std::string&)>
optional_number(std::optional<double> DigitizeSettings::*m) {
    return [m](DigitizeSettings& s, const std::string& v) {
        double d = 0.0;
        if (!parse_double(v, d)) {
            return false;
        }
        s.*m = d;
        return true;
    };
}

std::function<bool(DigitizeSettings&, const std::string&)> flag(bool DigitizeSettings::*m,
                                                                bool invert = false) {
    return [m, invert](DigitizeSettings& s, const std::string& v) {
        bool b = false;
        if (!parse_bool(v, b)) {
            return false;
        }
        s.*m = invert ? !b : b;
        return true;
    };
}

std::function<bool(DigitizeSettings&, const std::string&)>
choice(std::string DigitizeSettings::*m, std::vector<std::string> allowed) {
    return [m, allowed = std::move(allowed)](DigitizeSettings& s, const std::string& v) {
        if (std::find(allowed.begin(), allowed.end(), v) == allowed.end()) {
            return false;
        }
        s.*m = v;
        return true;
    };
}

const std::vector<Setting>& settings_table() {
    static const std::vector<Setting> table = {
        {"dpi", number(&DigitizeSettings::dpi)},
        {"max-colors", number(&DigitizeSettings::max_colors)},
        {"min-region-px", number(&DigitizeSettings::min_region_px)},
        {"smoothing-px", number(&DigitizeSettings::smoothing_px)},
        {"min-feature-mm", optional_number(&DigitizeSettings::min_feature_mm)},
        {"min-region-mm2", optional_number(&DigitizeSettings::min_region_mm2)},
        {"island-cleanup", number(&DigitizeSettings::island_cleanup_mm2)},
        {"island-max-delta-e", number(&DigitizeSettings::island_max_delta_e)},
        {"flow-fill", flag(&DigitizeSettings::flow_fill)},
        {"rings-satin", flag(&DigitizeSettings::rings_satin)},
        {"flow-min-area", number(&DigitizeSettings::flow_min_area_mm2)},
        {"flow-min-elongation", number(&DigitizeSettings::flow_min_elongation)},
        {"trim-threshold", number(&DigitizeSettings::trim_threshold_mm)},
        {"detail", number(&DigitizeSettings::detail)},
        {"curve-tolerance", number(&DigitizeSettings::curve_tolerance_mm)},
        {"satin-bands", flag(&DigitizeSettings::satin_bands)},
        {"dark-lines", flag(&DigitizeSettings::dark_lines)},
        {"dark-lines-width", number(&DigitizeSettings::dark_lines_width_mm)},
        {"dark-lines-lightness", number(&DigitizeSettings::dark_lines_lightness)},
        {"dark-lines-bars", number(&DigitizeSettings::dark_lines_bars_mm)},
        {"dark-lines-min-length", number(&DigitizeSettings::dark_lines_min_length_mm)},
        {"dark-lines-objects-only", flag(&DigitizeSettings::dark_lines_objects_only)},
        {"fill-inset", optional_number(&DigitizeSettings::fill_inset_mm)},
        {"fill-overlap", number(&DigitizeSettings::fill_overlap_mm)},
        {"no-underpath", flag(&DigitizeSettings::fill_hidden_underpath, true)},
        {"hidden-travel", flag(&DigitizeSettings::hidden_travel)},
        {"hidden-travel-max", number(&DigitizeSettings::hidden_max_mm)},
        {"single-path", flag(&DigitizeSettings::single_path)},
        {"coherent-lines", flag(&DigitizeSettings::coherent_lines)},
        {"pre",
         [](DigitizeSettings& s, const std::string& v) {
             // `none` = aucun ; `+` enchaîne les opérations (la virgule sépare les valeurs).
             const std::string spec = v == "none" ? std::string{} : v;
             if (!validate_pre_spec(spec)) {
                 return false;
             }
             s.pre = spec;
             return true;
         }},
        {"stipple-points", number(&DigitizeSettings::stipple_points)},
        {"mode", choice(&DigitizeSettings::mode, {"shapes", "contours", "stipple"})},
        {"technique",
         choice(&DigitizeSettings::technique, {"auto", "running", "legacy-satin", "satin"})},
        {"lock", choice(&DigitizeSettings::lock, {"none", "backforth", "triangle", "zigzag"})},
        {"skip-background",
         [](DigitizeSettings& s, const std::string& v) {
             if (v == "auto" || v == "-1") {
                 s.skip_background = -1;
             } else if (v == "yes" || v == "1") {
                 s.skip_background = 1;
             } else if (v == "no" || v == "0") {
                 s.skip_background = 0;
             } else {
                 return false;
             }
             return true;
         }},
    };
    return table;
}

std::string fixed(double v, int digits) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", digits, v);
    return buf;
}

double round_to(double v, int digits) {
    const double k = std::pow(10.0, digits);
    return std::round(v * k) / k;
}

} // namespace

std::string digitize_setting_keys() {
    std::string out;
    for (const auto& s : settings_table()) {
        out += (out.empty() ? "" : ", ") + std::string(s.key);
    }
    return out;
}

Result<void> apply_digitize_setting(DigitizeSettings& settings, const std::string& key,
                                    const std::string& value) {
    for (const auto& s : settings_table()) {
        if (key == s.key) {
            if (!s.apply(settings, value)) {
                return std::unexpected(
                    make_error(ErrorCategory::UserInput,
                               "valeur invalide pour « " + key + " » : « " + value + " »"));
            }
            return {};
        }
    }
    return std::unexpected(
        make_error(ErrorCategory::UserInput, "option de balayage inconnue : « " + key +
                                                 " » (valides : " + digitize_setting_keys() + ")"));
}

Result<SweepReport> run_sweep(const std::filesystem::path& image, const SweepOptions& options,
                              const StatusLog& log) {
    for (const auto& axis : options.axes) {
        if (axis.values.empty()) {
            return std::unexpected(
                make_error(ErrorCategory::UserInput, "axe sans valeur : « " + axis.key + " »"));
        }
        DigitizeSettings probe = options.base;
        for (const auto& v : axis.values) {
            if (auto ok = apply_digitize_setting(probe, axis.key, v); !ok) {
                return std::unexpected(ok.error());
            }
        }
    }
    // Produit cartésien, le premier axe variant le plus lentement.
    std::vector<std::vector<std::pair<std::string, std::string>>> variants(1);
    for (const auto& axis : options.axes) {
        std::vector<std::vector<std::pair<std::string, std::string>>> next;
        for (const auto& partial : variants) {
            for (const auto& v : axis.values) {
                auto copy = partial;
                copy.emplace_back(axis.key, v);
                next.push_back(std::move(copy));
            }
        }
        variants = std::move(next);
    }
    if (variants.size() > 64) {
        return std::unexpected(make_error(
            ErrorCategory::UserInput, "balayage trop grand : " + std::to_string(variants.size()) +
                                          " variantes (maximum 64)"));
    }

    SweepReport report;
    std::vector<std::vector<std::uint8_t>> tiles;
    for (std::size_t vi = 0; vi < variants.size(); ++vi) {
        SweepRow row;
        row.settings = variants[vi];
        for (const auto& [k, v] : row.settings) {
            row.label += (row.label.empty() ? "" : " ") + k + "=" + v;
        }
        if (row.label.empty()) {
            row.label = "(réglages de base)";
        }
        if (log) {
            log("variante " + std::to_string(vi + 1) + "/" + std::to_string(variants.size()) +
                " : " + row.label);
        }
        DigitizeSettings settings = options.base;
        for (const auto& [k, v] : row.settings) {
            if (auto ok = apply_digitize_setting(settings, k, v); !ok) {
                return std::unexpected(ok.error());
            }
        }
        std::vector<std::uint8_t> tile;
        auto created = create_empty_project({});
        if (!created) {
            row.error = created.error().message;
            report.rows.push_back(std::move(row));
            tiles.emplace_back();
            continue;
        }
        document::Project project = std::move(*created);
        const auto digitized = digitize_image_into(project, image, settings);
        if (!digitized) {
            row.error = digitized.error().message;
            report.rows.push_back(std::move(row));
            tiles.emplace_back();
            continue;
        }
        row.objects = digitized->embroidery_objects;
        row.satin = digitized->satin;
        row.tatami = digitized->tatami;
        row.running = digitized->running;
        const auto diag = diagnose_project(project);
        if (!diag) {
            row.error = diag.error().message;
            report.rows.push_back(std::move(row));
            tiles.emplace_back();
            continue;
        }
        row.ok = true;
        row.stitches = diag->stitches;
        row.jumps = diag->jumps;
        row.trims = diag->trims;
        row.color_changes = diag->color_changes;
        row.thread_m = diag->thread_m;
        row.width_mm = diag->motif_width_mm;
        row.height_mm = diag->motif_height_mm;
        row.hole_percent = 100.0 * diag->interior_hole_ratio;
        row.lost_percent = 100.0 * diag->lost_detail_ratio;
        row.hotspot_mm2 = diag->hotspot_area_mm2;
        row.layers_max = diag->layers_max;
        row.connector_percent = 100.0 * diag->connector_ratio;
        row.errors = diag->errors;
        row.warnings = diag->warnings;

        if (!options.keep_dir.empty()) {
            const auto path = options.keep_dir / ("variante_" + std::to_string(vi + 1) + ".osp");
            if (auto w = write_project(path, project); !w) {
                row.error = "écriture du projet impossible : " + w.error().message;
            }
        }
        if (options.sheet) {
            RenderOptions ro;
            ro.crop = options.sheet_crop;
            const double cropW = std::max(0.05, ro.crop[2] - ro.crop[0]);
            ro.px_per_mm =
                std::clamp(options.sheet_tile_px / std::max(1.0, row.width_mm * cropW), 0.5, 40.0);
            if (auto png = render_realistic_png(project, ro)) {
                tile = std::move(png->png);
            }
        }
        report.rows.push_back(std::move(row));
        tiles.push_back(std::move(tile));
    }

    // Variantes non dominées : aucune autre variante n'est au moins aussi bonne partout et
    // meilleure quelque part, sur (coupes, tissu visible, détails perdus, surépaisseurs).
    const auto metrics = [](const SweepRow& r) {
        return std::array<double, 4>{static_cast<double>(r.trims), r.hole_percent, r.lost_percent,
                                     r.hotspot_mm2};
    };
    for (auto& a : report.rows) {
        a.pareto = a.ok;
        if (!a.ok) {
            continue;
        }
        for (const auto& b : report.rows) {
            if (&a == &b || !b.ok) {
                continue;
            }
            const auto ma = metrics(a);
            const auto mb = metrics(b);
            bool allLe = true;
            bool anyLt = false;
            for (std::size_t k = 0; k < ma.size(); ++k) {
                allLe = allLe && mb[k] <= ma[k] + 1e-9;
                anyLt = anyLt || mb[k] < ma[k] - 1e-9;
            }
            if (allLe && anyLt) {
                a.pareto = false;
                break;
            }
        }
    }

    // Tableaux.
    std::string md =
        "| # | réglages | objets | points | coupes | fils | tissu visible % | détails perdus % | "
        "surépaisseur mm² | liaisons % | défauts | non dominée |\n"
        "|---|---|---|---|---|---|---|---|---|---|---|---|\n";
    std::string csv =
        "variante,reglages,objets,points,sauts,coupes,changements_de_fil,fil_m,largeur_mm,"
        "hauteur_mm,tissu_visible_pct,details_perdus_pct,surepaisseur_mm2,epaisseurs_max,"
        "liaisons_pct,erreurs,avertissements,non_dominee,erreur\n";
    json rows = json::array();
    for (std::size_t i = 0; i < report.rows.size(); ++i) {
        const SweepRow& r = report.rows[i];
        if (r.ok) {
            md += "| " + std::to_string(i + 1) + " | " + r.label + " | " +
                  std::to_string(r.objects) + " | " + std::to_string(r.stitches) + " | " +
                  std::to_string(r.trims) + " | " + std::to_string(r.color_changes + 1) + " | " +
                  fixed(r.hole_percent, 2) + " | " + fixed(r.lost_percent, 2) + " | " +
                  fixed(r.hotspot_mm2, 0) + " | " + fixed(r.connector_percent, 2) + " | " +
                  std::to_string(r.errors) + "E " + std::to_string(r.warnings) + "A | " +
                  (r.pareto ? "oui" : "") + " |\n";
        } else {
            md += "| " + std::to_string(i + 1) + " | " + r.label + " | échec : " + r.error +
                  " | | | | | | | | | |\n";
        }
        csv += std::to_string(i + 1) + ",\"" + r.label + "\"," + std::to_string(r.objects) + "," +
               std::to_string(r.stitches) + "," + std::to_string(r.jumps) + "," +
               std::to_string(r.trims) + "," + std::to_string(r.color_changes) + "," +
               fixed(r.thread_m, 1) + "," + fixed(r.width_mm, 1) + "," + fixed(r.height_mm, 1) +
               "," + fixed(r.hole_percent, 3) + "," + fixed(r.lost_percent, 3) + "," +
               fixed(r.hotspot_mm2, 1) + "," + fixed(r.layers_max, 2) + "," +
               fixed(r.connector_percent, 3) + "," + std::to_string(r.errors) + "," +
               std::to_string(r.warnings) + "," + (r.pareto ? "1" : "0") + ",\"" + r.error + "\"\n";
        json settings = json::object();
        for (const auto& [k, v] : r.settings) {
            settings[k] = v;
        }
        rows.push_back({{"variant", i + 1},
                        {"label", r.label},
                        {"settings", settings},
                        {"ok", r.ok},
                        {"error", r.error},
                        {"objects", r.objects},
                        {"stitches", r.stitches},
                        {"jumps", r.jumps},
                        {"trims", r.trims},
                        {"color_changes", r.color_changes},
                        {"thread_m", round_to(r.thread_m, 1)},
                        {"width_mm", round_to(r.width_mm, 1)},
                        {"height_mm", round_to(r.height_mm, 1)},
                        {"interior_hole_percent", round_to(r.hole_percent, 3)},
                        {"lost_detail_percent", round_to(r.lost_percent, 3)},
                        {"hotspot_mm2", round_to(r.hotspot_mm2, 1)},
                        {"layers_max", round_to(r.layers_max, 2)},
                        {"connector_percent", round_to(r.connector_percent, 3)},
                        {"errors", r.errors},
                        {"warnings", r.warnings},
                        {"pareto", r.pareto}});
    }
    report.markdown = md;
    report.csv = csv;
    report.json = json{{"schema", "openstitch.sweep/1"}, {"variants", rows}}.dump(2);

    // Planche : vignettes en grille, ordre des variantes (aucun texte : le numéro se lit dans le
    // tableau, de gauche à droite puis de haut en bas).
    if (options.sheet) {
        std::vector<image::Image> decoded;
        int tileW = 0, tileH = 0;
        for (const auto& png : tiles) {
            if (png.empty()) {
                decoded.emplace_back();
                continue;
            }
            auto img = image::decode_image(png);
            if (!img) {
                decoded.emplace_back();
                continue;
            }
            tileW = std::max(tileW, img->width);
            tileH = std::max(tileH, img->height);
            decoded.push_back(std::move(*img));
        }
        if (tileW > 0 && tileH > 0) {
            const int cols =
                std::max(1, std::min<int>(options.sheet_columns, static_cast<int>(decoded.size())));
            const int rowsN = (static_cast<int>(decoded.size()) + cols - 1) / cols;
            const int gap = 6;
            image::Image sheet;
            sheet.width = cols * tileW + (cols - 1) * gap;
            sheet.height = rowsN * tileH + (rowsN - 1) * gap;
            sheet.rgba.assign(static_cast<std::size_t>(sheet.width) * sheet.height * 4, 255);
            for (std::size_t i = 0; i < decoded.size(); ++i) {
                const auto& t = decoded[i];
                if (t.empty()) {
                    continue;
                }
                const int ox = static_cast<int>(i % static_cast<std::size_t>(cols)) * (tileW + gap);
                const int oy = static_cast<int>(i / static_cast<std::size_t>(cols)) * (tileH + gap);
                for (int y = 0; y < t.height; ++y) {
                    std::copy_n(t.rgba.data() + static_cast<std::size_t>(y) * t.width * 4,
                                static_cast<std::size_t>(t.width) * 4,
                                sheet.rgba.data() +
                                    (static_cast<std::size_t>(oy + y) * sheet.width + ox) * 4);
                }
            }
            if (auto png = image::encode_png(sheet)) {
                report.sheet_png = std::move(*png);
            }
        }
    }
    return report;
}

} // namespace openstitch::project_ops
