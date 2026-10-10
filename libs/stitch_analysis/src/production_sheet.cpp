// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_analysis/production_sheet.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>

#include "openstitch/stitch_analysis/color_blocks.hpp"
#include "openstitch/thread_palette/catalog.hpp"

namespace openstitch::stitch_analysis {

namespace {

std::string thread_label_of(const stitch::ColorBlock& block) {
    if (!block.thread_key) {
        return {};
    }
    const auto& key = *block.thread_key;
    if (const auto thread = thread_palette::find_by_code(key.chart_id, key.code)) {
        std::string label = thread->brand;
        if (!thread->range.empty()) {
            label += (label.empty() ? "" : " ") + thread->range;
        }
        label += (label.empty() ? "" : " ") + thread->key.code;
        if (!thread->name.empty()) {
            label += " - " + thread->name;
        }
        return label;
    }
    return {}; // nuancier inconnu : pas de référence inventée
}

std::string hex_of(const std::array<std::uint8_t, 3>& rgb) {
    return fmt::format("#{:02x}{:02x}{:02x}", rgb[0], rgb[1], rgb[2]);
}

std::string json_escape(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (const char ch : in) {
        const auto c = static_cast<unsigned char>(ch);
        switch (ch) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20) {
                out += fmt::format("\\u{:04x}", c);
            } else {
                out += ch;
            }
        }
    }
    return out;
}

std::string jstr(const std::string& s) {
    return "\"" + json_escape(s) + "\"";
}

std::string html_escape(const std::string& in, bool keepNewlines = false) {
    std::string out;
    out.reserve(in.size());
    for (const char ch : in) {
        switch (ch) {
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
        case '\r':
            break;
        case '\n':
            out += keepNewlines ? "<br/>" : " ";
            break;
        default:
            out += ch;
        }
    }
    return out;
}

const char* severity_word(Severity s) {
    switch (s) {
    case Severity::Error:
        return "Erreur";
    case Severity::Warning:
        return "Avertissement";
    case Severity::Info:
        return "Information";
    }
    return "Information";
}

const char* severity_slug(Severity s) {
    switch (s) {
    case Severity::Error:
        return "error";
    case Severity::Warning:
        return "warning";
    case Severity::Info:
        return "info";
    }
    return "info";
}

std::string fmt_fr(double v, int decimals) {
    std::string s = fmt::format("{:.{}f}", v, decimals);
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

} // namespace

ProductionSheet make_production_sheet(const document::Project& project,
                                      const stitch::StitchSequence& sequence,
                                      const ProductionOptions& options) {
    ProductionSheet sheet;
    sheet.project_name = options.project_name;
    sheet.date = options.date;
    sheet.notes = options.notes;
    sheet.stitches_per_minute =
        options.stitches_per_minute > 0.0 ? options.stitches_per_minute : 700.0;
    sheet.color_change_seconds = std::max(0.0, options.color_change_seconds);
    sheet.trim_seconds = std::max(0.0, options.trim_seconds);

    const stitch::StitchStats stats = stitch::compute_stats(sequence);
    sheet.stitches = stats.stitches;
    sheet.jumps = stats.jumps;
    sheet.trims = stats.trims;
    sheet.color_changes = stats.color_changes;
    if (stats.stitches > 0) {
        sheet.width_mm = (stats.bounds.max.x.value - stats.bounds.min.x.value) / 1000.0;
        sheet.height_mm = (stats.bounds.max.y.value - stats.bounds.min.y.value) / 1000.0;
    }

    AnalysisOptions analysis = options.analysis;
    if (options.use_project_canvas) {
        const document::Canvas& canvas = project.canvas;
        sheet.frame_mm = std::make_pair(canvas.width.value / 1000.0, canvas.height.value / 1000.0);
        analysis.hoop = stitch::BoundsUm{
            Vec2um{Micrometers{-canvas.width.value / 2}, Micrometers{-canvas.height.value / 2}},
            Vec2um{Micrometers{canvas.width.value / 2}, Micrometers{canvas.height.value / 2}}};
        sheet.fits_frame =
            stats.stitches == 0 || (stats.bounds.min.x.value >= analysis.hoop->min.x.value &&
                                    stats.bounds.min.y.value >= analysis.hoop->min.y.value &&
                                    stats.bounds.max.x.value <= analysis.hoop->max.x.value &&
                                    stats.bounds.max.y.value <= analysis.hoop->max.y.value);
    }

    // Blocs : la table du design importé si elle couvre la séquence, sinon la
    // dérivation unique (color_blocks) -- jamais une seconde logique de couleurs.
    std::vector<stitch::ColorBlock> blocks;
    if (project.imported_design && !project.imported_design->color_blocks.empty() &&
        project.imported_design->color_blocks.back().end <= sequence.commands.size()) {
        blocks = project.imported_design->color_blocks;
    } else {
        blocks = color_blocks(project, sequence);
    }

    const auto& cmds = sequence.commands;
    for (std::size_t b = 0; b < blocks.size(); ++b) {
        const auto& cb = blocks[b];
        ProductionBlock out;
        out.number = b + 1;
        out.rgb = cb.rgb;
        out.thread_label = thread_label_of(cb);

        double lengthUm = 0.0;
        bool hasPrev = false;
        Vec2um prev{};
        ProductionStroke stroke;
        stroke.block = b;
        const auto flush = [&] {
            if (stroke.points.size() >= 2) {
                sheet.preview.push_back(stroke);
            }
            stroke.points.clear();
        };
        for (std::size_t i = cb.start; i < cb.end && i < cmds.size(); ++i) {
            const auto& c = cmds[i];
            switch (c.type) {
            case stitch::CommandType::Stitch: {
                ++out.stitches;
                if (hasPrev) {
                    lengthUm += length_um(c.pos - prev);
                }
                prev = c.pos;
                hasPrev = true;
                stroke.points.emplace_back(static_cast<float>(c.pos.x.value / 1000.0),
                                           static_cast<float>(-c.pos.y.value / 1000.0));
                const auto known = std::any_of(
                    out.objects.begin(), out.objects.end(),
                    [&](const ProductionObjectRef& o) { return o.id.value == c.source.value; });
                if (!known) {
                    ProductionObjectRef ref;
                    ref.id = c.source;
                    if (const auto* emb = project.findEmbroidery(c.source)) {
                        ref.name = emb->name;
                    }
                    out.objects.push_back(std::move(ref));
                }
                break;
            }
            case stitch::CommandType::Jump:
                ++out.jumps;
                prev = c.pos;
                hasPrev = true;
                flush();
                break;
            case stitch::CommandType::Trim:
                ++out.trims;
                flush();
                break;
            default:
                break;
            }
        }
        flush();
        out.thread_length_mm = lengthUm / 1000.0;
        out.minutes = static_cast<double>(out.stitches) / sheet.stitches_per_minute +
                      (static_cast<double>(out.trims) * sheet.trim_seconds +
                       (b > 0 ? sheet.color_change_seconds : 0.0)) /
                          60.0;
        sheet.thread_length_m += out.thread_length_mm / 1000.0;
        sheet.estimated_minutes += out.minutes;
        sheet.blocks.push_back(std::move(out));
    }

    const AnalysisReport report = analyze_detailed(sequence, analysis);
    for (const auto& f : report.findings) {
        ProductionFinding pf;
        pf.severity = f.severity;
        pf.category = f.category;
        pf.message = f.message;
        pf.hint = f.hint;
        pf.object = f.object;
        if (const auto* emb = project.findEmbroidery(f.object)) {
            pf.object_name = emb->name;
        }
        sheet.findings.push_back(std::move(pf));
    }
    sheet.suppressed = report.suppressed;
    return sheet;
}

std::string format_duration_fr(double minutes) {
    if (!(minutes > 0.0)) {
        return "< 1 min";
    }
    const long total = std::lround(minutes);
    if (total < 1) {
        return "< 1 min";
    }
    const long h = total / 60;
    const long m = total % 60;
    if (h == 0) {
        return fmt::format("{} min", m);
    }
    return fmt::format("{} h {:02} min", h, m);
}

std::string production_to_json(const ProductionSheet& s) {
    std::string j;
    j += "{";
    j += fmt::format("\"schema\":1,\"project\":{},\"date\":{},\"notes\":{},", jstr(s.project_name),
                     jstr(s.date), jstr(s.notes));
    j +=
        fmt::format("\"size_mm\":{{\"width\":{:.2f},\"height\":{:.2f}}},", s.width_mm, s.height_mm);
    if (s.frame_mm) {
        j += fmt::format("\"frame_mm\":{{\"width\":{:.2f},\"height\":{:.2f}}},", s.frame_mm->first,
                         s.frame_mm->second);
    } else {
        j += "\"frame_mm\":null,";
    }
    j += fmt::format("\"fits_frame\":{},", s.fits_frame ? "true" : "false");
    j += fmt::format("\"totals\":{{\"stitches\":{},\"jumps\":{},\"trims\":{},\"color_changes\":{},"
                     "\"colors\":{},\"thread_length_m\":{:.3f}}},",
                     s.stitches, s.jumps, s.trims, s.color_changes, s.blocks.size(),
                     s.thread_length_m);
    j += fmt::format(
        "\"estimate\":{{\"stitches_per_minute\":{:.1f},\"color_change_seconds\":{:.1f},"
        "\"trim_seconds\":{:.1f},\"minutes\":{:.2f}}},",
        s.stitches_per_minute, s.color_change_seconds, s.trim_seconds, s.estimated_minutes);
    j += "\"blocks\":[";
    for (std::size_t i = 0; i < s.blocks.size(); ++i) {
        const auto& b = s.blocks[i];
        j += i ? "," : "";
        j += fmt::format("{{\"number\":{},\"color\":\"{}\",\"thread_label\":{},\"stitches\":{},"
                         "\"jumps\":{},\"trims\":{},\"thread_length_mm\":{:.1f},\"minutes\":{:.2f},"
                         "\"objects\":[",
                         b.number, hex_of(b.rgb), jstr(b.thread_label), b.stitches, b.jumps,
                         b.trims, b.thread_length_mm, b.minutes);
        for (std::size_t k = 0; k < b.objects.size(); ++k) {
            j += fmt::format("{}{{\"id\":{},\"name\":{}}}", k ? "," : "", b.objects[k].id.value,
                             jstr(b.objects[k].name));
        }
        j += "]}";
    }
    j += "],\"findings\":[";
    for (std::size_t i = 0; i < s.findings.size(); ++i) {
        const auto& f = s.findings[i];
        j += fmt::format("{}{{\"severity\":\"{}\",\"category\":{},\"message\":{},\"hint\":{},"
                         "\"object\":{},\"object_name\":{}}}",
                         i ? "," : "", severity_slug(f.severity), jstr(f.category), jstr(f.message),
                         jstr(f.hint), f.object.value, jstr(f.object_name));
    }
    j += "],\"suppressed\":{";
    bool first = true;
    for (const auto& [cat, n] : s.suppressed) {
        j += fmt::format("{}{}:{}", first ? "" : ",", jstr(cat), n);
        first = false;
    }
    j += "}}\n";
    return j;
}

std::string production_preview_svg(const ProductionSheet& s) {
    constexpr double kMargin = 2.0;
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool any = false;
    for (const auto& st : s.preview) {
        for (const auto& p : st.points) {
            if (!any) {
                x0 = x1 = p.first;
                y0 = y1 = p.second;
                any = true;
            }
            x0 = std::min<double>(x0, p.first);
            x1 = std::max<double>(x1, p.first);
            y0 = std::min<double>(y0, p.second);
            y1 = std::max<double>(y1, p.second);
        }
    }
    if (!any) {
        x0 = y0 = 0.0;
        x1 = y1 = 10.0;
    }
    x0 -= kMargin;
    y0 -= kMargin;
    x1 += kMargin;
    y1 += kMargin;
    std::string svg = fmt::format(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" role=\"img\" "
        "aria-label=\"Aperçu du motif, {} par {} mm\" viewBox=\"{:.2f} {:.2f} {:.2f} {:.2f}\">\n"
        "<title>Aperçu du motif</title>\n"
        "<rect x=\"{:.2f}\" y=\"{:.2f}\" width=\"{:.2f}\" height=\"{:.2f}\" fill=\"#f4f1ea\"/>\n",
        fmt_fr(s.width_mm, 1), fmt_fr(s.height_mm, 1), x0, y0, x1 - x0, y1 - y0, x0, y0, x1 - x0,
        y1 - y0);
    for (const auto& st : s.preview) {
        svg += fmt::format("<polyline fill=\"none\" stroke=\"{}\" stroke-width=\"0.25\" "
                           "stroke-linejoin=\"round\" stroke-linecap=\"round\" points=\"",
                           hex_of(s.blocks[st.block].rgb));
        for (std::size_t i = 0; i < st.points.size(); ++i) {
            svg += fmt::format("{}{:.2f},{:.2f}", i ? " " : "", st.points[i].first,
                               st.points[i].second);
        }
        svg += "\"/>\n";
    }
    svg += "</svg>\n";
    return svg;
}

std::string production_to_html(const ProductionSheet& s, const ProductionHtmlOptions& options) {
    std::string h;
    const std::string title = s.project_name.empty() ? "Sans titre" : s.project_name;
    h += "<!DOCTYPE html>\n<html lang=\"fr\"><head><meta charset=\"utf-8\"/>";
    h += "<title>Fiche de production - " + html_escape(title) + "</title>\n";
    h += "<style>\n"
         "body { font-family: 'Segoe UI', Arial, sans-serif; font-size: 10pt; color: #111111; }\n"
         "h1 { font-size: 20pt; margin-bottom: 0px; }\n"
         "h2 { font-size: 13pt; margin-top: 14px; margin-bottom: 4px; }\n"
         "table { border-collapse: collapse; }\n"
         "th { background-color: #e6e6e6; text-align: left; }\n"
         "td, th { padding: 4px; }\n"
         ".small { font-size: 8.5pt; color: #444444; }\n"
         "</style></head><body>\n";

    h += "<h1>Fiche de production</h1>\n";
    h += "<p><b>" + html_escape(title) + "</b>";
    if (!s.date.empty()) {
        h += " &mdash; " + html_escape(s.date);
    }
    h += "</p>\n";

    // Aperçu : texte alternatif complet pour les lecteurs d'écran.
    const std::string alt = "Aperçu du motif : " + fmt_fr(s.width_mm, 1) + " mm x " +
                            fmt_fr(s.height_mm, 1) + " mm, " + std::to_string(s.stitches) +
                            " points, " + std::to_string(s.blocks.size()) + " couleurs.";
    h += "<h2>Aperçu du motif</h2>\n<p align=\"center\">";
    if (!options.preview_src.empty()) {
        h += "<img src=\"" + html_escape(options.preview_src) + "\" alt=\"" + html_escape(alt) +
             "\" width=\"" + std::to_string(options.preview_width_px) + "\"/>";
    } else if (options.embed_svg_fallback) {
        h += production_preview_svg(s);
    }
    h += "</p>\n";

    // Chiffres clés : tableau à deux colonnes (libellé / valeur), ordre logique.
    h += "<h2>Caractéristiques</h2>\n<table border=\"1\" cellspacing=\"0\" width=\"100%\">\n";
    const auto row = [&](const std::string& k, const std::string& v) {
        h += "<tr><th width=\"40%\">" + html_escape(k) + "</th><td>" + html_escape(v) +
             "</td></tr>\n";
    };
    row("Dimensions réelles",
        fmt_fr(s.width_mm, 1) + " mm (largeur) x " + fmt_fr(s.height_mm, 1) + " mm (hauteur)");
    if (s.frame_mm) {
        row("Cadre", fmt_fr(s.frame_mm->first, 1) + " x " + fmt_fr(s.frame_mm->second, 1) + " mm" +
                         (s.fits_frame ? " (le motif tient dans le cadre)"
                                       : " (ATTENTION : le motif dépasse du cadre)"));
    }
    row("Points", std::to_string(s.stitches));
    row("Sauts", std::to_string(s.jumps));
    row("Coupes", std::to_string(s.trims));
    row("Changements de couleur", std::to_string(s.color_changes));
    row("Couleurs (blocs)", std::to_string(s.blocks.size()));
    row("Longueur de fil estimée", fmt_fr(s.thread_length_m, 2) + " m (fil du dessus)");
    row("Temps de broderie estimé", format_duration_fr(s.estimated_minutes));
    h += "</table>\n";
    h += "<p class=\"small\">Hypothèses du temps estimé : " + fmt_fr(s.stitches_per_minute, 0) +
         " points par minute, " + fmt_fr(s.color_change_seconds, 0) + " s par changement de fil, " +
         fmt_fr(s.trim_seconds, 0) +
         " s par coupe. La vitesse réelle dépend de la machine et du motif ; la longueur de fil "
         "ne compte ni le fil de canette ni les points de saut.</p>\n";

    // Tableau des blocs dans l'ordre de couture.
    h += "<h2>Blocs de couleur, dans l'ordre de couture</h2>\n";
    if (s.blocks.empty()) {
        h += "<p>Aucun bloc : le motif ne contient aucun point.</p>\n";
    } else {
        h += "<table border=\"1\" cellspacing=\"0\" width=\"100%\">\n<tr>"
             "<th>N°</th><th>Couleur</th><th>Fil</th><th>Points</th><th>Sauts</th>"
             "<th>Longueur</th><th>Objets</th></tr>\n";
        for (const auto& b : s.blocks) {
            std::string objects;
            for (const auto& o : b.objects) {
                objects += (objects.empty() ? "" : ", ");
                objects += o.name.empty()
                               ? (o.id.value == 0 ? std::string("design importé")
                                                  : "objet " + std::to_string(o.id.value))
                               : o.name;
            }
            const std::string hex = hex_of(b.rgb);
            h += "<tr><td>" + std::to_string(b.number) + "</td>";
            // Pastille (fond coloré) + code hexadécimal en toutes lettres : la
            // couleur n'est jamais portée par la seule teinte.
            h += "<td><table border=\"1\" cellspacing=\"0\"><tr><td bgcolor=\"" + hex +
                 "\" width=\"22\" height=\"14\">&nbsp;</td></tr></table> " + hex + "</td>";
            h += "<td>" +
                 (b.thread_label.empty() ? std::string("&mdash;") : html_escape(b.thread_label)) +
                 "</td>";
            h += "<td align=\"right\">" + std::to_string(b.stitches) + "</td>";
            h += "<td align=\"right\">" + std::to_string(b.jumps) + "</td>";
            h += "<td align=\"right\">" + fmt_fr(b.thread_length_mm / 1000.0, 2) + " m</td>";
            h += "<td>" + (objects.empty() ? std::string("&mdash;") : html_escape(objects)) +
                 "</td></tr>\n";
        }
        h += "</table>\n";
    }

    // Avertissements.
    h += "<h2>Avertissements</h2>\n";
    if (s.findings.empty()) {
        h += "<p>Aucun problème détecté par l'analyse.</p>\n";
    } else {
        h += "<ul>\n";
        for (const auto& f : s.findings) {
            h += std::string("<li><b>") + severity_word(f.severity) + "</b> : ";
            if (!f.object_name.empty()) {
                h += "&laquo; " + html_escape(f.object_name) + " &raquo; : ";
            }
            h += html_escape(f.message);
            if (!f.hint.empty()) {
                h += " <i>Piste : " + html_escape(f.hint) + "</i>";
            }
            h += "</li>\n";
        }
        for (const auto& [cat, n] : s.suppressed) {
            h += "<li>... et " + std::to_string(n) + " autre(s) problème(s) de catégorie « " +
                 html_escape(cat) + " » non listé(s).</li>\n";
        }
        h += "</ul>\n";
    }

    // Notes libres.
    h += "<h2>Notes</h2>\n";
    if (s.notes.empty()) {
        h += "<p class=\"small\">Aucune note.</p>\n";
    } else {
        h += "<p>" + html_escape(s.notes, true) + "</p>\n";
    }
    h += "</body></html>\n";
    return h;
}

} // namespace openstitch::stitch_analysis
