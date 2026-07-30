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
#include "openstitch/geometry/path.hpp"
#include "openstitch/stitch/sequence.hpp"
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
    fmt::print("Fil cousu estimé   : {:.2f} m\n", stats.thread_length_um / 1e9);
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
// inspecter le routage autour d'un trou. Retourne la séquence + un compte des
// coutures qui traverseraient le trou (doit être 0).
int run_filldebug(double lengthMm, const std::string& outSvg) {
    using namespace openstitch;
    using geometry::NodeType;
    const auto corner = [](std::int32_t x, std::int32_t y) {
        return geometry::PathNode{Vec2um{Micrometers{x}, Micrometers{y}}, NodeType::Corner,
                                  std::nullopt, std::nullopt};
    };
    geometry::PathSet ring;
    ring.outer.closed = true;
    ring.outer.nodes = {corner(0, 0), corner(40'000, 0), corner(40'000, 40'000),
                        corner(0, 40'000)};
    geometry::Path hole;
    hole.closed = true;
    hole.nodes = {corner(12'000, 12'000), corner(28'000, 12'000), corner(28'000, 28'000),
                  corner(12'000, 28'000)};
    ring.holes.push_back(hole);

    document::TatamiParams tp;
    tp.row_spacing = Micrometers{1'000};
    tp.stitch_length = to_micrometers(Millimeters{lengthMm});
    tp.inset = Micrometers{0};
    const auto fill = stitch_generation::fill_tatami(ring, tp);

    stitch::StitchSequence seq;
    bool started = false;
    int sewnCrossingHole = 0;
    Vec2um prev{};
    for (const auto& fs : fill) {
        const bool sew = started && !fs.jump;
        seq.commands.push_back(
            {fs.pos, sew ? stitch::CommandType::Stitch : stitch::CommandType::Jump, ObjectId{}});
        if (sew) {
            const Vec2um mid{Micrometers{(prev.x.value + fs.pos.x.value) / 2},
                             Micrometers{(prev.y.value + fs.pos.y.value) / 2}};
            if (mid.x.value > 12'500 && mid.x.value < 27'500 && mid.y.value > 12'500 &&
                mid.y.value < 27'500) {
                ++sewnCrossingHole;
            }
        }
        prev = fs.pos;
        started = true;
    }
    seq.commands.push_back({prev, stitch::CommandType::End, ObjectId{}});
    const auto stats = stitch::compute_stats(seq);

    fmt::print("Anneau tatami (trou central)\n");
    fmt::print("Points cousus : {}  |  déplacements : {}\n", stats.stitches, stats.jumps);
    fmt::print("Coutures traversant le trou : {}  (doit être 0)\n", sewnCrossingHole);
    if (!outSvg.empty()) {
        const auto w = formats::write_svg_file(std::filesystem::path(outSvg), seq);
        if (!w) {
            fmt::print(stderr, "Erreur : {}\n", w.error().message);
            return 1;
        }
        fmt::print("SVG écrit : {}\n", outSvg);
    }
    return sewnCrossingHole == 0 ? 0 : 2;
}

int run_stitchdebug(const std::string& shape, double lengthMm, int repeats,
                    const std::string& outSvg) {
    using namespace openstitch;
    if (shape == "ring") {
        return run_filldebug(lengthMm, outSvg);
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

int run_auto_satin_debug(const std::string& shape, double pixelMm, const std::string& outSvg,
                         int capEnd, int shortMode, int splitMode) {
    using namespace openstitch;
    const auto region = auto_satin::make_shape(shape);
    if (!region) {
        fmt::print(stderr, "Forme inconnue : {}\n", shape);
        return 1;
    }
    auto_satin::SatinColumnsParameters params;
    params.analysis.raster.pixel_size = to_micrometers(Millimeters{pixelMm});
    const auto result = auto_satin::build_satin_columns(*region, params);
    const auto& r = result.report;
    fmt::print("Forme            : {}\n", shape);
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
    fmt::print("Colonnes satin   : {}\n", result.columns.size());
    for (std::size_t i = 0; i < result.columns.size(); ++i) {
        const auto& c = result.columns[i];
        fmt::print("  colonne {} : {} stations, {} barreaux, largeur moy {:.2f} mm, long {:.1f} mm\n",
                   i, c.rail_a.nodes.size(), c.rungs.size(), c.mean_width_um / 1000.0,
                   c.length_um / 1000.0);
    }
    if (!result.refusal.empty()) {
        fmt::print("Refus            : {}\n", result.refusal);
    }
    for (const auto& w : result.warnings) {
        fmt::print("  ! {}\n", w);
    }
    if (!outSvg.empty()) {
        std::string svg = auto_satin::columns_to_svg(*region, result);
        // Superpose le zigzag satin généré (fil réel) pour inspection visuelle.
        std::string overlay;
        for (const auto& col : result.columns) {
            std::vector<stitch_generation::SatinRungSeg> rungs;
            for (const auto& r : col.rungs) {
                rungs.emplace_back(r.a, r.b);
            }
            stitch_generation::SatinConfig scfg;
            scfg.cap_end = static_cast<stitch_generation::SatinCapType>(capEnd);
            scfg.short_stitch = static_cast<stitch_generation::ShortStitchMode>(shortMode);
            scfg.split_stitch = static_cast<stitch_generation::SplitStitchMode>(splitMode);
            const auto sat =
                stitch_generation::fill_satin_columns(col.rail_a, col.rail_b, rungs, scfg);
            if (sat.satin.size() < 2) {
                continue;
            }
            overlay += "<path d=\"M";
            for (std::size_t i = 0; i < sat.satin.size(); ++i) {
                overlay += fmt::format("{}{:.3f} {:.3f} ", i ? "L" : "",
                                       sat.satin[i].x.value / 1000.0, -sat.satin[i].y.value / 1000.0);
            }
            overlay += "\" fill=\"none\" stroke=\"#333\" stroke-width=\"0.06\"/>\n";
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

    std::string as_shape = "rectangle";
    double as_pixel = 0.05;
    std::string as_out;
    int as_cap = 0, as_short = 0, as_split = 0;
    auto* as_cmd = app.add_subcommand(
        "auto-satin-debug", "Analyse de satinabilité et squelette d'une forme de référence");
    as_cmd->add_option("--shape", as_shape,
                       "rectangle|capsule|ribbon|s|y|t|cross|circle|ring|wide|tiny");
    as_cmd->add_option("--pixel-size", as_pixel, "Taille de pixel de calcul en mm")
        ->check(CLI::PositiveNumber);
    as_cmd->add_option("--output-svg", as_out, "SVG de diagnostic à produire");
    as_cmd->add_option("--cap-end", as_cap, "Terminaison fin : 0 plat, 1 arrondi, 2 effilé");
    as_cmd->add_option("--short", as_short, "Points courts : 0 off, 2 inset, 3 multi-niveaux");
    as_cmd->add_option("--split", as_split, "Split : 0 off, 1 simple, 2 décalé, 3 jitter");

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
        return run_stitchdebug(sd_shape, sd_length, sd_repeats, sd_out);
    }
    if (as_cmd->parsed()) {
        return run_auto_satin_debug(as_shape, as_pixel, as_out, as_cap, as_short, as_split);
    }
    return 0;
}
