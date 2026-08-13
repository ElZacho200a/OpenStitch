// SPDX-License-Identifier: Apache-2.0
#include <CLI/CLI.hpp>
#include <fmt/core.h>

#include <filesystem>
#include <string>

#include <cmath>
#include <numbers>

#include "openstitch/core/app_info.hpp"
#include "openstitch/core/log.hpp"
#include "openstitch/formats/dst.hpp"
#include "openstitch/formats/svg.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/auto_satin/auto_satin.hpp"
#include "openstitch/auto_satin/debug_export.hpp"
#include "openstitch/auto_satin/satin_column.hpp"
#include "openstitch/auto_satin/shapes.hpp"
#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/satin_coverage/coverage.hpp"
#include "openstitch/stitch/sequence.hpp"
#include "openstitch/stitch_generation/generate.hpp"
#include "openstitch/stitch_generation/lock.hpp"
#include "openstitch/stitch_generation/running_stitch.hpp"
#include "openstitch/stitch_generation/satin.hpp"
#include "openstitch/stitch_generation/tatami.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

int run_info(const std::string& path, double dpi) {
    const auto info = openstitch::image::read_image_info(std::filesystem::path(path));
    if (!info) {
        fmt::print(stderr, "Erreur : {}\n", info.error().message);
        return 1;
    }
    const double mm_per_px = 25.4 / dpi;
    fmt::print("Fichier        : {}\n", path);
    fmt::print("Format         : {}\n", info->format);
    fmt::print("Dimensions     : {} x {} px\n", info->width_px, info->height_px);
    fmt::print("Canaux         : {}\n", info->channels);
    fmt::print("Canal alpha    : {}\n", info->has_alpha ? "oui" : "non");
    fmt::print("Taille estimée : {:.1f} x {:.1f} mm (à {:g} dpi)\n",
               info->width_px * mm_per_px, info->height_px * mm_per_px, dpi);
    return 0;
}

int run_stats(const std::string& path) {
    const auto seq = openstitch::formats::read_dst_file(std::filesystem::path(path));
    if (!seq) {
        fmt::print(stderr, "Erreur : {}\n", seq.error().message);
        return 1;
    }
    const auto stats = openstitch::stitch::compute_stats(*seq);
    const double wMm = (stats.bounds.max.x.value - stats.bounds.min.x.value) / 1000.0;
    const double hMm = (stats.bounds.max.y.value - stats.bounds.min.y.value) / 1000.0;
    fmt::print("Fichier            : {}\n", path);
    fmt::print("Points             : {}\n", stats.stitches);
    fmt::print("Sauts              : {}\n", stats.jumps);
    fmt::print("Coupes             : {}\n", stats.trims);
    fmt::print("Changements de fil : {}\n", stats.color_changes);
    fmt::print("Dimensions         : {:.1f} x {:.1f} mm\n", wMm, hMm);
    fmt::print("Fil cousu estimé   : {:.2f} m\n", stats.thread_length_um / 1e6);
    return 0;
}

int run_dst2svg(const std::string& input, const std::string& output) {
    const auto seq = openstitch::formats::read_dst_file(std::filesystem::path(input));
    if (!seq) {
        fmt::print(stderr, "Erreur : {}\n", seq.error().message);
        return 1;
    }
    const auto written =
        openstitch::formats::write_svg_file(std::filesystem::path(output), *seq);
    if (!written) {
        fmt::print(stderr, "Erreur : {}\n", written.error().message);
        return 1;
    }
    fmt::print("SVG écrit : {}\n", output);
    return 0;
}

// Formes de référence procédurales pour inspecter le moteur (§34-35).
openstitch::geometry::Path debug_shape(const std::string& name) {
    using namespace openstitch;
    using geometry::NodeType;
    const auto corner = [](std::int32_t x, std::int32_t y) {
        return geometry::PathNode{Vec2um{Micrometers{x}, Micrometers{y}}, NodeType::Corner,
                                  std::nullopt, std::nullopt};
    };
    geometry::Path p;
    if (name == "line") {
        p.closed = false;
        p.nodes = {corner(0, 0), corner(40'000, 0)};
    } else if (name == "corner") {
        p.closed = false;
        p.nodes = {corner(0, 0), corner(30'000, 0), corner(30'000, 30'000)};
    } else if (name == "circle") {
        p.closed = true;
        const int sides = 64;
        for (int i = 0; i < sides; ++i) {
            const double a = 2.0 * std::numbers::pi * i / sides;
            p.nodes.push_back(corner(static_cast<std::int32_t>(std::lround(20'000 * std::cos(a))),
                                     static_cast<std::int32_t>(std::lround(20'000 * std::sin(a)))));
        }
    } else if (name == "bezier") {
        p.closed = false;
        geometry::PathNode a = corner(0, 0);
        a.tan_out = Vec2um{Micrometers{15'000}, Micrometers{30'000}};
        geometry::PathNode b = corner(40'000, 0);
        b.tan_in = Vec2um{Micrometers{-15'000}, Micrometers{30'000}};
        p.nodes = {a, b};
    } else {  // "star" : coins vifs
        p.closed = true;
        const int pts = 5;
        for (int i = 0; i < pts * 2; ++i) {
            const double a = std::numbers::pi * i / pts - std::numbers::pi / 2.0;
            const double r = (i % 2 == 0) ? 22'000.0 : 9'000.0;
            p.nodes.push_back(corner(static_cast<std::int32_t>(std::lround(r * std::cos(a))),
                                     static_cast<std::int32_t>(std::lround(r * std::sin(a)))));
        }
    }
    return p;
}

// Remplissage tatami d'un anneau (extérieur 40 mm, trou central 16 mm) pour
// inspecter le routage autour d'un trou, les sous-couches et l'underpath (Lot 7).
// Passe par generate_sequence (passes taguées). Compte les coutures qui
// traverseraient le trou (doit être 0). `underlayMask` : 1 contour, 2 parallèle.
int run_filldebug(double lengthMm, const std::string& outSvg, int underlayMask, bool underpath) {
    using namespace openstitch;
    using geometry::NodeType;
    const auto corner = [](std::int32_t x, std::int32_t y) {
        return geometry::PathNode{Vec2um{Micrometers{x}, Micrometers{y}}, NodeType::Corner,
                                  std::nullopt, std::nullopt};
    };
    geometry::PathSet ring;
    ring.outer.closed = true;
    ring.outer.nodes = {corner(0, 0), corner(20'000, 0), corner(20'000, 20'000),
                        corner(0, 20'000)};
    geometry::Path hole;
    hole.closed = true;
    hole.nodes = {corner(6'000, 6'000), corner(14'000, 6'000), corner(14'000, 14'000),
                  corner(6'000, 14'000)};
    ring.holes.push_back(hole);

    document::Project project;
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.paths.push_back(geometry::PathSet{ring.outer, ring.holes});
    project.vector_objects.push_back(vec);
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.source_vector = vec.id;
    document::TatamiParams tp;
    tp.row_spacing = Micrometers{2'000};
    tp.stitch_length = to_micrometers(Millimeters{lengthMm});
    tp.inset = Micrometers{0};
    tp.underlay_edge = (underlayMask & 1) != 0;
    tp.underlay_parallel = (underlayMask & 2) != 0;
    tp.hidden_underpath = underpath;
    emb.params = tp;
    project.embroidery_objects.push_back(emb);

    // Projet synthetique construit ici meme, jamais charge/sauvegarde, aucune
    // retouche manuelle possible. raw-sequence-ok: generateur de debug.
    const auto seq = stitch_generation::generate_sequence(project);
    if (!seq) {
        fmt::print(stderr, "Erreur : {}\n", seq.error().message);
        return 1;
    }
    const auto stats = stitch::compute_stats(*seq);

    int sewnCrossingHole = 0;
    for (std::size_t i = 1; i < seq->commands.size(); ++i) {
        const auto& c = seq->commands[i];
        if (c.type != stitch::CommandType::Stitch) continue;
        const Vec2um a = seq->commands[i - 1].pos;
        const Vec2um mid{Micrometers{(a.x.value + c.pos.x.value) / 2},
                         Micrometers{(a.y.value + c.pos.y.value) / 2}};
        if (mid.x.value > 6'500 && mid.x.value < 13'500 && mid.y.value > 6'500 &&
            mid.y.value < 13'500) {
            ++sewnCrossingHole;
        }
    }

    int under = 0, travel = 0;
    for (const auto& c : seq->commands) {
        if (c.type == stitch::CommandType::Stitch && c.pass == stitch::StitchPass::Underlay) ++under;
        if (c.type == stitch::CommandType::Stitch && c.pass == stitch::StitchPass::Travel) ++travel;
    }
    fmt::print("Anneau tatami (trou central)\n");
    fmt::print("Points cousus : {}  |  déplacements : {}\n", stats.stitches, stats.jumps);
    fmt::print("Sous-couche : {}  |  underpath (Travel) : {}\n", under, travel);
    fmt::print("Coutures traversant le trou : {}  (doit être 0)\n", sewnCrossingHole);

    if (!outSvg.empty()) {
        // SVG maison : chaque liaison colorée par passe (contour vert, couche sup.
        // gris, underpath bleu, saut rouge pointillé).
        const auto pt = [](Vec2um p) {
            return fmt::format("{:.3f} {:.3f}", p.x.value / 1000.0, -p.y.value / 1000.0);
        };
        std::string body;
        for (std::size_t i = 1; i < seq->commands.size(); ++i) {
            const auto& prev = seq->commands[i - 1];
            const auto& cur = seq->commands[i];
            if (cur.type == stitch::CommandType::End) continue;
            const char* stroke = nullptr;
            const char* dash = "";
            if (cur.type == stitch::CommandType::Jump) {
                stroke = "#e00";
                dash = " stroke-dasharray=\"0.6 0.4\"";
            } else if (cur.pass == stitch::StitchPass::Underlay) {
                stroke = "#0a9";
            } else if (cur.pass == stitch::StitchPass::Travel) {
                stroke = "#06c";
            } else {
                stroke = "#333";
            }
            body += fmt::format(
                "<path d=\"M{} L{}\" fill=\"none\" stroke=\"{}\" stroke-width=\"0.18\"{}/>\n",
                pt(prev.pos), pt(cur.pos), stroke, dash);
        }
        std::string svg =
            "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"-2 -22 24 24\" width=\"800\">\n"
            "<rect x=\"-2\" y=\"-22\" width=\"24\" height=\"24\" fill=\"#fff\"/>\n" +
            body + "</svg>\n";
        std::ofstream f(std::filesystem::path(outSvg), std::ios::binary | std::ios::trunc);
        if (!f) {
            fmt::print(stderr, "Impossible d'écrire {}\n", outSvg);
            return 1;
        }
        f << svg;
        fmt::print("SVG écrit : {}\n", outSvg);
    }
    return sewnCrossingHole == 0 ? 0 : 2;
}

int run_stitchdebug(const std::string& shape, double lengthMm, int repeats,
                    const std::string& outSvg, int underlayMask, bool underpath) {
    using namespace openstitch;
    if (shape == "ring") {
        return run_filldebug(lengthMm, outSvg, underlayMask, underpath);
    }
    const auto path = debug_shape(shape);

    stitch_generation::RunningConfig cfg;
    cfg.target_length = to_micrometers(Millimeters{lengthMm});
    const auto result = stitch_generation::run_stitch(path, cfg);
    const auto points = stitch_generation::apply_repeats(result.points, repeats);

    stitch::StitchSequence seq;
    if (!points.empty()) {
        seq.commands.push_back({points.front(), stitch::CommandType::Jump, ObjectId{}});
        for (const Vec2um& p : points) {
            seq.commands.push_back({p, stitch::CommandType::Stitch, ObjectId{}});
        }
        seq.commands.push_back({points.back(), stitch::CommandType::End, ObjectId{}});
    }
    const auto stats = stitch::compute_stats(seq);

    fmt::print("Forme       : {}\n", shape);
    fmt::print("Longueur    : {:g} mm  |  répétitions : {}\n", lengthMm, repeats);
    fmt::print("Points      : {}\n", stats.stitches);
    fmt::print("Longueur fil : {:.1f} mm\n", stats.thread_length_um / 1000.0);
    if (result.stats.stitches >= 2) {
        fmt::print("Segment min/max : {:.2f} / {:.2f} mm\n", result.stats.min_segment_um / 1000.0,
                   result.stats.max_segment_um / 1000.0);
    }
    for (const auto& w : result.warnings) {
        fmt::print("  ! {}\n", w.message);
    }

    if (!outSvg.empty()) {
        const auto written = formats::write_svg_file(std::filesystem::path(outSvg), seq);
        if (!written) {
            fmt::print(stderr, "Erreur : {}\n", written.error().message);
            return 1;
        }
        fmt::print("SVG écrit : {}\n", outSvg);
    }
    return 0;
}

openstitch::satin_coverage::SatinColumnInput to_coverage_input(
    const openstitch::geometry::Path& railA, const openstitch::geometry::Path& railB,
    const std::vector<openstitch::auto_satin::SatinRung>& rungs) {
    openstitch::satin_coverage::SatinColumnInput in;
    in.rail_a = railA;
    in.rail_b = railB;
    in.rungs.reserve(rungs.size());
    for (const auto& r : rungs) {
        in.rungs.emplace_back(r.a, r.b);
    }
    in.density = openstitch::Micrometers{400};
    return in;
}

// Calcule et écrit le SVG de couverture géométrique (§ satin_coverage) pour
// les colonnes déjà construites, en plus du diagnostic texte affiché sur la
// sortie standard -- ne modifie ni `region` ni `columns`.
void write_coverage_svg(const openstitch::geometry::PathSet& region,
                        const std::vector<openstitch::satin_coverage::SatinColumnInput>& columns,
                        const std::string& path) {
    using namespace openstitch;
    const auto report = satin_coverage::analyze_satin_coverage(region, columns);
    if (!report) {
        fmt::print(stderr, "Erreur de couverture : {}\n", report.error().message);
        return;
    }
    fmt::print("\n{}\n", report->diagnostic);
    const std::string svg = satin_coverage::coverage_to_svg(region, columns, *report);
    std::ofstream f(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    if (!f) {
        fmt::print(stderr, "Impossible d'écrire {}\n", path);
        return;
    }
    f << svg;
    fmt::print("SVG de couverture écrit : {}\n", path);
}

int run_auto_satin_debug(const std::string& shape, double pixelMm, const std::string& outSvg,
                         int capEnd, int shortMode, int splitMode, int underlayMask, int lockMode,
                         bool route, const std::string& geometryMode, const std::string& coverageSvg) {
    using namespace openstitch;
    const auto region = auto_satin::make_shape(shape);
    if (!region) {
        fmt::print(stderr, "Forme inconnue : {}\n", shape);
        return 1;
    }
    const bool parametric = geometryMode == "parametric";
    auto_satin::SatinColumnsParameters params;
    params.analysis.raster.pixel_size = to_micrometers(Millimeters{pixelMm});
    params.geometry_mode =
        parametric ? auto_satin::SatinGeometryMode::Parametric : auto_satin::SatinGeometryMode::Legacy;
    const auto result = auto_satin::build_satin_columns(*region, params);
    const auto& r = result.report;
    fmt::print("Forme            : {}\n", shape);
    fmt::print("Geometrie        : {}\n", geometryMode);
    fmt::print("Satinabilité     : {} (confiance {:.2f})\n", auto_satin::to_string(r.status),
               r.confidence);
    fmt::print("Aire / périmètre : {:.1f} mm² / {:.1f} mm\n", r.area_mm2, r.perimeter_mm);
    fmt::print("Largeur moy/min/max : {:.2f} / {:.2f} / {:.2f} mm\n", r.mean_width_mm,
               r.minimum_width_mm, r.maximum_width_mm);
    fmt::print("Longueur d'axe   : {:.1f} mm  (allongée : {})\n", r.estimated_length_mm,
               r.is_elongated ? "oui" : "non");
    fmt::print("Squelette (élagué) : {} arêtes, {} extrémités, {} jonctions\n", r.branch_count,
               r.endpoint_count, r.junction_count);
    fmt::print("Trous            : {}\n", r.hole_count);
    if (parametric) {
        fmt::print("Objets satin     : {}\n", result.parametric_columns.size());
        for (std::size_t i = 0; i < result.parametric_columns.size(); ++i) {
            const auto& c = result.parametric_columns[i];
            fmt::print("  objet {} : {} stations brutes -> {} paires structurantes, {} segments "
                      "bezier, erreur max {:.3f} mm, largeur moy {:.2f} mm, long {:.1f} mm\n",
                      i, c.raw_station_count, c.control_pairs.size(),
                      c.rail_a.nodes.empty() ? 0 : c.rail_a.nodes.size() - 1,
                      c.max_fit_error_um / 1000.0, c.mean_width_um / 1000.0, c.length_um / 1000.0);
        }
        for (const auto& plan : result.junction_plans) {
            fmt::print("  jonction {} : ordre de couture =", plan.junction_id);
            for (auto idx : plan.stitch_order) {
                fmt::print(" {}", idx);
            }
            fmt::print("\n");
        }
    } else {
        fmt::print("Colonnes satin   : {}\n", result.columns.size());
        for (std::size_t i = 0; i < result.columns.size(); ++i) {
            const auto& c = result.columns[i];
            fmt::print(
                "  colonne {} : {} stations, {} barreaux, largeur moy {:.2f} mm, long {:.1f} mm\n",
                i, c.rail_a.nodes.size(), c.rungs.size(), c.mean_width_um / 1000.0,
                c.length_um / 1000.0);
        }
    }
    if (!result.refusal.empty()) {
        fmt::print("Refus            : {}\n", result.refusal);
    }
    for (const auto& w : result.warnings) {
        fmt::print("  ! {}\n", w);
    }
    if (!coverageSvg.empty()) {
        std::vector<satin_coverage::SatinColumnInput> coverageColumns;
        if (parametric) {
            for (const auto& obj : result.parametric_columns) {
                coverageColumns.push_back(to_coverage_input(obj.rail_a, obj.rail_b, obj.rungs));
            }
        } else {
            for (const auto& col : result.columns) {
                coverageColumns.push_back(to_coverage_input(col.rail_a, col.rail_b, col.rungs));
            }
        }
        write_coverage_svg(*region, coverageColumns, coverageSvg);
    }
    if (!outSvg.empty() && parametric) {
        std::string svg = auto_satin::parametric_to_svg(*region, result);
        std::string overlay;
        for (const auto& obj : result.parametric_columns) {
            std::vector<stitch_generation::SatinRungSeg> rungs;
            for (const auto& rr : obj.rungs) {
                rungs.emplace_back(rr.a, rr.b);
            }
            stitch_generation::SatinConfig scfg;
            scfg.cap_end = static_cast<stitch_generation::SatinCapType>(capEnd);
            scfg.short_stitch = static_cast<stitch_generation::ShortStitchMode>(shortMode);
            scfg.split_stitch = static_cast<stitch_generation::SplitStitchMode>(splitMode);
            scfg.center_underlay = (underlayMask & 1) != 0;
            scfg.underlay_edge = (underlayMask & 2) != 0;
            scfg.underlay_zigzag = (underlayMask & 4) != 0;
            const auto sat =
                stitch_generation::fill_satin_columns(obj.rail_a, obj.rail_b, rungs, scfg);
            const auto polylineSvg = [&](const std::vector<Vec2um>& pts, const char* stroke,
                                        double w) {
                if (pts.size() < 2) return;
                overlay += "<path d=\"M";
                for (std::size_t i = 0; i < pts.size(); ++i) {
                    overlay += fmt::format("{}{:.3f} {:.3f} ", i ? "L" : "", pts[i].x.value / 1000.0,
                                           -pts[i].y.value / 1000.0);
                }
                overlay += fmt::format("\" fill=\"none\" stroke=\"{}\" stroke-width=\"{}\"/>\n",
                                       stroke, w);
            };
            for (const auto& u : sat.underlays) {
                polylineSvg(u.points, "#0a9", 0.05);
            }
            polylineSvg(sat.satin, "#333", 0.06);
        }
        if (const auto pos = svg.rfind("</svg>"); pos != std::string::npos) {
            svg.insert(pos, overlay);
        }
        std::ofstream f(std::filesystem::path(outSvg), std::ios::binary | std::ios::trunc);
        if (!f) {
            fmt::print(stderr, "Impossible d'écrire {}\n", outSvg);
            return 1;
        }
        f << svg;
        fmt::print("SVG écrit : {}\n", outSvg);
        return 0;
    }
    if (!outSvg.empty()) {
        std::string svg = auto_satin::columns_to_svg(*region, result);
        // Superpose le zigzag satin généré (fil réel) pour inspection visuelle.
        std::string overlay;
        for (const auto& col : result.columns) {
            std::vector<stitch_generation::SatinRungSeg> rungs;
            for (const auto& rung : col.rungs) {
                rungs.emplace_back(rung.a, rung.b);
            }
            stitch_generation::SatinConfig scfg;
            scfg.cap_end = static_cast<stitch_generation::SatinCapType>(capEnd);
            scfg.short_stitch = static_cast<stitch_generation::ShortStitchMode>(shortMode);
            scfg.split_stitch = static_cast<stitch_generation::SplitStitchMode>(splitMode);
            scfg.center_underlay = (underlayMask & 1) != 0;
            scfg.underlay_edge = (underlayMask & 2) != 0;
            scfg.underlay_zigzag = (underlayMask & 4) != 0;
            const auto sat =
                stitch_generation::fill_satin_columns(col.rail_a, col.rail_b, rungs, scfg);
            const auto polylineSvg = [&](const std::vector<Vec2um>& pts, const char* stroke,
                                         double w) {
                if (pts.size() < 2) return;
                overlay += "<path d=\"M";
                for (std::size_t i = 0; i < pts.size(); ++i) {
                    overlay += fmt::format("{}{:.3f} {:.3f} ", i ? "L" : "", pts[i].x.value / 1000.0,
                                           -pts[i].y.value / 1000.0);
                }
                overlay += fmt::format("\" fill=\"none\" stroke=\"{}\" stroke-width=\"{}\"/>\n",
                                       stroke, w);
            };
            for (const auto& u : sat.underlays) {
                polylineSvg(u.points, "#0a9", 0.05);  // sous-couches (vert)
            }
            polylineSvg(sat.satin, "#333", 0.06);  // couche supérieure
            // Points de fixation (Lot 5) : ancrés aux extrémités du satin, rouge.
            if (lockMode > 0 && sat.satin.size() >= 2) {
                const auto type = static_cast<stitch_generation::LockType>(lockMode);
                const std::size_t n = sat.satin.size();
                polylineSvg(stitch_generation::lock_stitches(sat.satin.front(), sat.satin[1], type,
                                                             Micrometers{800}, 2),
                            "#c00", 0.06);
                polylineSvg(stitch_generation::lock_stitches(sat.satin[n - 1], sat.satin[n - 2],
                                                             type, Micrometers{800}, 2),
                            "#c00", 0.06);
            }
        }
        // Routage multi-colonnes (Lot 6) : construit un projet à partir des
        // colonnes (même couleur/source), génère la séquence et superpose les
        // liaisons — trajets cachés (bleu) vs sauts (rouge pointillé).
        if (route && result.columns.size() >= 2) {
            document::Project project;
            document::VectorObject vec;
            vec.id = project.object_ids.next();
            project.vector_objects.push_back(vec);
            for (const auto& col : result.columns) {
                document::SatinParams sp;
                sp.rail_a = col.rail_a;
                sp.rail_b = col.rail_b;
                sp.center_underlay = false;  // lisibilité : une passe par colonne
                for (const auto& rr : col.rungs) {
                    sp.rungs.push_back(document::SatinRung{rr.a, rr.b});
                }
                document::EmbroideryObject emb;
                emb.id = project.object_ids.next();
                emb.source_vector = vec.id;
                emb.rgb = {10, 20, 30};
                emb.params = sp;
                project.embroidery_objects.push_back(emb);
            }
            // Projet synthetique construit ici meme, jamais charge/sauvegarde,
            // aucune retouche manuelle possible. raw-sequence-ok: generateur.
            if (const auto seq = stitch_generation::generate_sequence(project)) {
                const auto pt = [](Vec2um p) {
                    return fmt::format("{:.3f} {:.3f}", p.x.value / 1000.0, -p.y.value / 1000.0);
                };
                // Une liaison = changement de colonne source. Saut (coupe) en
                // rouge pointillé, trajet caché (cousu) en bleu.
                for (std::size_t i = 1; i < seq->commands.size(); ++i) {
                    const auto& prev = seq->commands[i - 1];
                    const auto& cur = seq->commands[i];
                    if (cur.source == prev.source || prev.source.value == 0 ||
                        cur.source.value == 0 || cur.type == stitch::CommandType::End) {
                        continue;
                    }
                    const bool jump = cur.type == stitch::CommandType::Jump;
                    overlay += fmt::format(
                        "<path d=\"M{} L{}\" fill=\"none\" stroke=\"{}\" stroke-width=\"0.15\"{}/>\n",
                        pt(prev.pos), pt(cur.pos), jump ? "#e00" : "#06c",
                        jump ? " stroke-dasharray=\"0.4 0.3\"" : "");
                }
            }
        }
        if (const auto pos = svg.rfind("</svg>"); pos != std::string::npos) {
            svg.insert(pos, overlay);
        }
        std::ofstream f(std::filesystem::path(outSvg), std::ios::binary | std::ios::trunc);
        if (!f) {
            fmt::print(stderr, "Impossible d'écrire {}\n", outSvg);
            return 1;
        }
        f << svg;
        fmt::print("SVG écrit : {}\n", outSvg);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    openstitch::init_logging();

    CLI::App app{fmt::format("{} — outils en ligne de commande", openstitch::kAppName)};
    app.set_version_flag("--version", openstitch::kAppVersion);
    app.require_subcommand(1);

    std::string image_path;
    double dpi = 96.0;
    auto* info_cmd = app.add_subcommand("info", "Affiche les métadonnées d'une image");
    info_cmd->add_option("image", image_path, "Chemin de l'image (PNG, JPEG, BMP, TIFF)")
        ->required();
    info_cmd->add_option("--dpi", dpi, "Résolution supposée pour l'estimation en mm (défaut : 96)")
        ->check(CLI::PositiveNumber);

    std::string dst_path;
    auto* stats_cmd = app.add_subcommand("stats", "Statistiques d'un fichier de broderie DST");
    stats_cmd->add_option("fichier", dst_path, "Chemin du fichier .dst")->required();

    std::string svg_in;
    std::string svg_out;
    auto* svg_cmd = app.add_subcommand("dst2svg", "Convertit un DST en SVG de diagnostic");
    svg_cmd->add_option("entree", svg_in, "Fichier .dst source")->required();
    svg_cmd->add_option("sortie", svg_out, "Fichier .svg à produire")->required();

    std::string sd_shape = "circle";
    double sd_length = 3.0;
    int sd_repeats = 1;
    std::string sd_out;
    auto* sd_cmd = app.add_subcommand(
        "stitchdebug", "Inspecte le moteur de points sur une forme de référence");
    sd_cmd->add_option("--shape", sd_shape, "line|corner|circle|bezier|star|ring")
        ->check(CLI::IsMember({"line", "corner", "circle", "bezier", "star", "ring"}));
    sd_cmd->add_option("--length", sd_length, "Longueur de point en mm")
        ->check(CLI::PositiveNumber);
    sd_cmd->add_option("--repeats", sd_repeats, "1 simple, 2 aller-retour, 3 bean");
    sd_cmd->add_option("--output-svg", sd_out, "Fichier SVG de diagnostic à produire");
    int sd_underlay = 0;
    bool sd_underpath = false;
    sd_cmd->add_option("--underlay", sd_underlay,
                       "Tatami (ring) : sous-couches (masque : 1 contour, 2 parallèle)");
    sd_cmd->add_flag("--underpath", sd_underpath, "Tatami (ring) : liaisons cousues cachées");

    std::string as_shape = "rectangle";
    double as_pixel = 0.05;
    std::string as_out;
    int as_cap = 0, as_short = 0, as_split = 0;
    auto* as_cmd = app.add_subcommand(
        "auto-satin-debug", "Analyse de satinabilité et squelette d'une forme de référence");
    as_cmd->add_option("--shape", as_shape,
                       "rectangle|capsule|ribbon|s|y|t|cross|h|circle|ring|wide|tiny|notch|pinch|"
                       "trident");
    as_cmd->add_option("--pixel-size", as_pixel, "Taille de pixel de calcul en mm")
        ->check(CLI::PositiveNumber);
    as_cmd->add_option("--output-svg", as_out, "SVG de diagnostic à produire");
    as_cmd->add_option("--cap-end", as_cap, "Terminaison fin : 0 plat, 1 arrondi, 2 effilé");
    as_cmd->add_option("--short", as_short, "Points courts : 0 off, 2 inset, 3 multi-niveaux");
    as_cmd->add_option("--split", as_split, "Split : 0 off, 1 simple, 2 décalé, 3 jitter");
    int as_underlay = 0;
    as_cmd->add_option("--underlay", as_underlay, "Sous-couches (masque : 1 center, 2 edge, 4 zigzag)");
    int as_lock = 0;
    as_cmd->add_option("--lock", as_lock,
                       "Fixation : 0 off, 1 aller-retour, 2 triangle, 3 micro-zigzag");
    bool as_route = false;
    as_cmd->add_flag("--route", as_route,
                     "Superpose le routage multi-colonnes (liaisons cachées bleu / sauts rouge)");
    std::string as_geometry = "legacy";
    as_cmd->add_option("--satin-geometry", as_geometry,
                       "legacy (rails polyligne denses) | parametric (objets satin "
                       "parametriques, rails Bezier epars)")
        ->check(CLI::IsMember({"legacy", "parametric"}));
    std::string as_coverage_svg;
    as_cmd->add_option("--coverage-svg", as_coverage_svg,
                       "SVG de couverture geometrique a produire (satin_coverage : cible grise, "
                       "couverture verte, zones manquantes rouges, hors-forme orange)");

    CLI11_PARSE(app, argc, argv);

    if (info_cmd->parsed()) {
        return run_info(image_path, dpi);
    }
    if (stats_cmd->parsed()) {
        return run_stats(dst_path);
    }
    if (svg_cmd->parsed()) {
        return run_dst2svg(svg_in, svg_out);
    }
    if (sd_cmd->parsed()) {
        return run_stitchdebug(sd_shape, sd_length, sd_repeats, sd_out, sd_underlay, sd_underpath);
    }
    if (as_cmd->parsed()) {
        return run_auto_satin_debug(as_shape, as_pixel, as_out, as_cap, as_short, as_split,
                                    as_underlay, as_lock, as_route, as_geometry, as_coverage_svg);
    }
    return 0;
}
