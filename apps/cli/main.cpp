// SPDX-License-Identifier: Apache-2.0
#include <CLI/CLI.hpp>
#include <fmt/core.h>

#include <filesystem>
#include <functional>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <numbers>
#include <vector>

#include "openstitch/auto_satin/auto_satin.hpp"
#include "openstitch/auto_satin/shapes.hpp"
#include "openstitch/auto_satin/skeleton_satin.hpp"
#include "openstitch/autodigitize/autodigitize.hpp"
#include "openstitch/autodigitize/contour_objects.hpp"
#include "openstitch/core/app_info.hpp"
#include "openstitch/core/log.hpp"
#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/formats/dst.hpp"
#include "openstitch/formats/svg.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/segmentation/segmentation.hpp"
#include "openstitch/stitch/sequence.hpp"
#include "openstitch/stitch_analysis/metrics.hpp"
#include "openstitch/stitch_analysis/project_metrics.hpp"
#include "openstitch/stitch_generation/generate.hpp"
#include "openstitch/stitch_generation/lock.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "openstitch/stitch_generation/running_stitch.hpp"
#include "openstitch/stitch_generation/satin.hpp"
#include "openstitch/stitch_generation/tatami.hpp"
#include "openstitch/vectorization/vectorize.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

// Mesures de qualité d'une séquence (Lot G) : communes à `stats` (DST relu,
// verrous reconnus par leur forme) et `digitize` (séquence effective, passes
// connues).
void print_sequence_metrics(const openstitch::stitch::StitchSequence& seq, bool fromDst) {
    using namespace openstitch;
    stitch_analysis::SequenceMetricsOptions opts;
    opts.infer_locks = fromDst;
    if (fromDst) {
        opts.length_tolerance = Micrometers{100}; // résolution DST : 0,1 mm
    }
    const auto m = stitch_analysis::sequence_metrics(seq, opts);
    fmt::print("Déplacements        : {}\n", m.moves);
    fmt::print("  > {:.1f} mm sans coupe : {}\n", opts.trim_threshold.value / 1000.0,
               m.long_moves_without_trim);
    fmt::print("Points < {:.1f} mm    : {} ({:.2f} %) hors points d'arrêt ; {} dans les points "
               "d'arrêt{}\n",
               opts.short_stitch.value / 1000.0, m.short_stitches,
               m.stitches
                   ? 100.0 * static_cast<double>(m.short_stitches) / static_cast<double>(m.stitches)
                   : 0.0,
               m.short_lock_stitches, fromDst ? " (reconnus par leur forme)" : "");
    // Histogramme des directions : les 6 tranches de 5° les plus fréquentes.
    std::vector<std::pair<std::size_t, int>> bins;
    std::size_t total = 0;
    for (int b = 0; b < 36; ++b) {
        bins.emplace_back(m.direction_histogram[static_cast<std::size_t>(b)], b * 5);
        total += m.direction_histogram[static_cast<std::size_t>(b)];
    }
    std::stable_sort(bins.begin(), bins.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });
    fmt::print("Directions (points >= 1 mm, modulo 180°) :");
    for (int k = 0; k < 6 && bins[static_cast<std::size_t>(k)].first > 0; ++k) {
        fmt::print(" {}° {:.0f} %", bins[static_cast<std::size_t>(k)].second,
                   100.0 * static_cast<double>(bins[static_cast<std::size_t>(k)].first) /
                       static_cast<double>(std::max<std::size_t>(1, total)));
    }
    fmt::print("\n");
}

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
    fmt::print("Taille estimée : {:.1f} x {:.1f} mm (à {:g} dpi)\n", info->width_px * mm_per_px,
               info->height_px * mm_per_px, dpi);
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
    print_sequence_metrics(*seq, true);
    return 0;
}

int run_dst2svg(const std::string& input, const std::string& output) {
    const auto seq = openstitch::formats::read_dst_file(std::filesystem::path(input));
    if (!seq) {
        fmt::print(stderr, "Erreur : {}\n", seq.error().message);
        return 1;
    }
    const auto written = openstitch::formats::write_svg_file(std::filesystem::path(output), *seq);
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
    } else { // "star" : coins vifs
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
    ring.outer.nodes = {corner(0, 0), corner(20'000, 0), corner(20'000, 20'000), corner(0, 20'000)};
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
        if (c.type != stitch::CommandType::Stitch)
            continue;
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
        if (c.type == stitch::CommandType::Stitch && c.pass == stitch::StitchPass::Underlay)
            ++under;
        if (c.type == stitch::CommandType::Stitch && c.pass == stitch::StitchPass::Travel)
            ++travel;
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
            if (cur.type == stitch::CommandType::End)
                continue;
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

// Pipeline complet image -> DST (segmentation -> numerisation automatique ->
// generation des points -> export), en ligne de commande, pour comparer le
// resultat d'OpenStitch a une reference externe sans passer par l'IHM. Memes
// valeurs par defaut que le dialogue "Numerisation automatique" du desktop
// (main_window.cpp : max_colors=8, min_region_px=16, smoothing_radius_px=3,
// skip_largest_region coche par defaut seulement si la plus grande region est
// un fond quasi blanc qui encadre le motif -- segmentation::background_candidate,
// Lot A ; une valeur explicite de --skip-background reste prioritaire).
int run_digitize(const std::string& imagePath, const std::string& dstPath, double dpi,
                 int maxColors, int minRegionPx, int smoothingPx, int skipBg,
                 const std::string& outSvg, double trimThresholdMm, const std::string& lockName,
                 const std::string& mode, double detail, const std::string& technique) {
    using namespace openstitch;

    const auto loaded = image::load_image(std::filesystem::path(imagePath));
    if (!loaded) {
        fmt::print(stderr, "Erreur de chargement : {}\n", loaded.error().message);
        return 1;
    }
    fmt::print("Image : {} x {} px (alpha source : {})\n", loaded->width, loaded->height,
               loaded->source_had_alpha ? "oui" : "non");

    document::Project project;
    project.mm_per_px = Millimeters{25.4 / dpi};
    project.original = *loaded;
    // Finitions (Lots E/F) : nouveau projet -> activées ; réglables ici.
    project.finishing.trim_threshold =
        Micrometers{static_cast<std::int32_t>(std::lround(trimThresholdMm * 1000.0))};
    project.finishing.lock_type = lockName == "none"       ? document::LockStitch::None
                                  : lockName == "triangle" ? document::LockStitch::Triangle
                                  : lockName == "zigzag"   ? document::LockStitch::MicroZigzag
                                                           : document::LockStitch::BackAndForth;

    auto seg = segmentation::segment(project.original, {.max_colors = maxColors,
                                                        .min_region_px = minRegionPx,
                                                        .smoothing_radius_px = smoothingPx});
    if (!seg) {
        fmt::print(stderr, "Erreur de segmentation : {}\n", seg.error().message);
        return 1;
    }
    fmt::print("Régions segmentées : {}\n", seg->region_count());
    project.segmentation = std::move(*seg);

    // Même règle que le dialogue desktop (Lot A) : jamais sur le seul
    // critère « pas d'alpha ».
    const auto candidate = segmentation::background_candidate(*project.segmentation);
    if (candidate) {
        fmt::print(
            "Région candidate au fond : #{:02X}{:02X}{:02X}, {:.1f} % de l'image, L* {:.0f}, "
            "{} bord(s) touché(s)\n",
            candidate->rgb[0], candidate->rgb[1], candidate->rgb[2], candidate->area_ratio * 100.0,
            candidate->lightness, candidate->sides_touched);
    }
    const bool skipLargest = skipBg < 0 ? (candidate && candidate->recommended) : (skipBg != 0);
    autodigitize::AutoOptions opts;
    opts.mm_per_px = project.mm_per_px;
    opts.skip_largest_region = skipLargest;
    fmt::print("Ignorer la plus grande région (fond) : {}{}\n", skipLargest ? "oui" : "non",
               skipBg < 0 ? " (automatique)" : " (option explicite)");

    // Strategie « contours / dessin au trait » : lignes medianes cousues en
    // point droit (satin legacy degrade en point droit).
    const bool contours = mode == "contours";
    autodigitize::ContourMetrics cm;
    autodigitize::ContourOptions co;
    co.mm_per_px = project.mm_per_px;
    co.skip_largest_region = skipLargest;
    co.detail = detail;
    co.technique = technique == "running" ? autodigitize::ContourTechnique::Running
                   : technique == "satin" ? autodigitize::ContourTechnique::Satin
                                          : autodigitize::ContourTechnique::Automatic;
    if (contours) {
        fmt::print("Mode : contours (detail {:.2f}, technique {})\n", detail, technique);
    }
    auto result =
        contours ? autodigitize::auto_digitize_contours(*project.segmentation, project.object_ids,
                                                        co, &cm)
                 : autodigitize::auto_digitize(*project.segmentation, project.object_ids, opts);
    if (!result) {
        fmt::print(stderr, "Erreur de numérisation : {}\n", result.error().message);
        return 1;
    }
    if (contours) {
        fmt::print("Contours : composantes={} segments={} jonctions={} extremites={}\n",
                   cm.components, cm.segments, cm.junctions, cm.endpoints);
        fmt::print("  elagage : branches courtes={} elements petits={}\n",
                   cm.removed_short_branches, cm.removed_small_elements);
        fmt::print("  longueur point droit {:.1f} mm | largeur min/moy/max "
                   "{:.2f}/{:.2f}/{:.2f} mm\n",
                   cm.running_length_mm, cm.min_width_mm, cm.mean_width_mm, cm.max_width_mm);
        fmt::print("  replis legacy->point droit={} rejets={}\n", cm.fallbacks, cm.rejected);
    }
    for (const auto& w : result->warnings) {
        fmt::print(stderr, "  ! {}\n", w);
    }
    for (auto& v : result->vectors) {
        project.vector_objects.push_back(std::move(v));
    }
    for (auto& e : result->embroideries) {
        project.embroidery_objects.push_back(std::move(e));
    }

    int nSatin = 0, nTatami = 0, nRunning = 0;
    for (const auto& e : project.embroidery_objects) {
        if (e.is_satin() || e.is_auto_satin())
            ++nSatin;
        else if (e.is_tatami())
            ++nTatami;
        else if (std::holds_alternative<document::RunningStitchParams>(e.params))
            ++nRunning;
    }
    fmt::print("Objets brodés : {} (satin={} tatami={} running={})\n",
               project.embroidery_objects.size(), nSatin, nTatami, nRunning);

    const auto sequence = stitch_generation::effective_sequence(project);
    if (!sequence) {
        fmt::print(stderr, "Erreur de génération des points : {}\n", sequence.error().message);
        return 1;
    }
    const auto stats = stitch::compute_stats(*sequence);
    const double wMm = (stats.bounds.max.x.value - stats.bounds.min.x.value) / 1000.0;
    const double hMm = (stats.bounds.max.y.value - stats.bounds.min.y.value) / 1000.0;
    fmt::print("Points : {}  |  sauts : {}  |  coupes : {}  |  changements de fil : {}\n",
               stats.stitches, stats.jumps, stats.trims, stats.color_changes);
    fmt::print("Dimensions : {:.1f} x {:.1f} mm  |  fil : {:.2f} m\n", wMm, hMm,
               stats.thread_length_um / 1e6);

    // Mesures de qualité (Lot G).
    print_sequence_metrics(*sequence, false);
    stitch_analysis::ProjectMetricsOptions pm;
    if (skipLargest && candidate) {
        pm.excluded_rgb = candidate->rgb;
    }
    const auto quality = stitch_analysis::project_metrics(project, *sequence, pm);
    fmt::print("Objets brodés < {:.0f} mm² : {}\n", pm.small_object_mm2, quality.small_objects);
    if (quality.uncovered_ratio) {
        fmt::print("Surface non couverte (hors fond ignoré) : {:.2f} %\n",
                   *quality.uncovered_ratio * 100.0);
    }
    fmt::print("Angles de remplissage ({} distincts) :", quality.fill_angles_deg.size());
    for (const auto& [deg, n] : quality.fill_angles_deg) {
        fmt::print(" {}°x{}", deg, n);
    }
    fmt::print("\n");
    fmt::print("Déplacements par source :");
    for (const auto& [kind, n] : quality.moves_by_kind) {
        fmt::print(" {}={}", kind, n);
    }
    fmt::print("\nPoints courts par source :");
    for (const auto& [kind, n] : quality.short_stitches_by_kind) {
        fmt::print(" {}={}", kind, n);
    }
    fmt::print("\n");

    const auto written = formats::write_dst_file(std::filesystem::path(dstPath), *sequence);
    if (!written) {
        fmt::print(stderr, "Erreur d'écriture DST : {}\n", written.error().message);
        return 1;
    }
    fmt::print("DST écrit : {}\n", dstPath);

    if (!outSvg.empty()) {
        const auto svgWritten = formats::write_svg_file(std::filesystem::path(outSvg), *sequence);
        if (!svgWritten) {
            fmt::print(stderr, "Erreur d'écriture SVG : {}\n", svgWritten.error().message);
            return 1;
        }
        fmt::print("SVG écrit : {}\n", outSvg);
    }
    return 0;
}

// Séquence effective d'un projet .osp (la même que l'aperçu, l'export et l'analyse) en SVG de
// diagnostic ; `--outlines` superpose le contour des vecteurs sources (en gris) pour voir
// d'un coup d'œil les points qui débordent de leur forme. `--only` limite à un objet brodé.
int run_osp2svg(const std::string& ospPath, const std::string& outSvg, bool outlines,
                std::uint64_t onlyObject) {
    using namespace openstitch;
    auto project = project_io::load_project(std::filesystem::path(ospPath));
    if (!project) {
        fmt::print(stderr, "Erreur : {}\n", project.error().message);
        return 1;
    }
    if (onlyObject != 0) {
        std::erase_if(project->embroidery_objects,
                      [&](const auto& e) { return e.id.value != onlyObject; });
    }
    const auto seq = stitch_generation::effective_sequence(*project);
    if (!seq) {
        fmt::print(stderr, "Erreur : {}\n", seq.error().message);
        return 1;
    }
    const auto written = formats::write_svg_file(std::filesystem::path(outSvg), *seq);
    if (!written) {
        fmt::print(stderr, "Erreur : {}\n", written.error().message);
        return 1;
    }
    if (outlines) {
        std::ifstream in(outSvg, std::ios::binary);
        std::string svg((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        std::string extra;
        for (const auto& v : project->vector_objects) {
            bool used = onlyObject == 0;
            for (const auto& e : project->embroidery_objects) {
                used = used || e.source_vector == v.id;
            }
            if (!used) {
                continue;
            }
            for (const auto& set : v.paths) {
                std::vector<const geometry::Path*> rings{&set.outer};
                for (const auto& h : set.holes) {
                    rings.push_back(&h);
                }
                for (const auto* ring : rings) {
                    std::string d;
                    for (std::size_t i = 0; i < ring->nodes.size(); ++i) {
                        d += fmt::format("{}{:.3f},{:.3f}", i == 0 ? "M" : "L",
                                         ring->nodes[i].pos.x.value / 1000.0,
                                         -ring->nodes[i].pos.y.value / 1000.0);
                    }
                    extra += "<path d=\"" + d +
                             "Z\" fill=\"none\" stroke=\"#888\" stroke-width=\"0.08\"/>\n";
                }
            }
        }
        const auto close = svg.rfind("</svg>");
        if (close != std::string::npos) {
            svg.insert(close, extra);
        }
        std::ofstream out(outSvg, std::ios::binary | std::ios::trunc);
        out << svg;
    }
    fmt::print("Points : {}  |  SVG : {}\n", seq->commands.size(), outSvg);
    return 0;
}

// Auto-satin par squelette et traversées orientées (spec
// specs/plans/satin-squelette-traversees.md) sur une forme de référence : résume
// colonnes, longueurs de traversées et diagnostics, et écrit un SVG (contour, axes,
// traversées, zigzag) pour inspecter orientations et zones non couvertes.
int run_satin_auto_debug(const std::string& shape, double spacingMm,
                         const std::vector<std::string>& guides, const std::string& outSvg,
                         const std::string& ospPath, std::uint64_t vectorId, bool pristine) {
    using namespace openstitch;
    std::optional<geometry::PathSet> region;
    if (!ospPath.empty()) {
        const auto project = project_io::load_project(std::filesystem::path(ospPath));
        if (!project) {
            fmt::print(stderr, "Erreur : {}\n", project.error().message);
            return 1;
        }
        for (const auto& v : project->vector_objects) {
            if (v.id.value == vectorId && !v.paths.empty()) {
                region = v.paths.front();
                if (pristine && v.source_region && project->segmentation) {
                    // Contour brut de la région de segmentation (sans le recouvrement tatami).
                    vectorization::VectorizeOptions vo;
                    vo.mm_per_px = project->mm_per_px;
                    const auto raw = vectorization::vectorize_region(*project->segmentation,
                                                                     *v.source_region, vo);
                    if (raw && !raw->empty()) {
                        double cur = 0.0, base = 0.0;
                        for (const auto& st : v.paths) {
                            cur += geometry::path_set_area_um2(st);
                        }
                        for (const auto& st : *raw) {
                            base += geometry::path_set_area_um2(st);
                        }
                        fmt::print(
                            "Contour brut : aire {:.1f} mm2 (contour du projet : {:.1f} mm2, "
                            "{:+.1f} %)\n",
                            base / 1e6, cur / 1e6, 100.0 * (cur - base) / base);
                        region = raw->front();
                    }
                }
            }
        }
        if (!region) {
            fmt::print(stderr, "Vecteur {} introuvable dans {}\n", vectorId, ospPath);
            return 1;
        }
    } else {
        region = auto_satin::make_shape(shape);
    }
    if (!region) {
        fmt::print(stderr, "Forme inconnue : {}\n", shape);
        return 1;
    }
    auto_satin::SkeletonSatinParameters params;
    params.spacing = to_micrometers(Millimeters{spacingMm});
    params.measure_coverage = true;
    for (const auto& g : guides) {
        double xMm = 0.0, yMm = 0.0, deg = 0.0;
        int absolute = 0;
        // Portable (sscanf_s n'existe pas sous GCC) : champs séparés par des virgules.
        int n = 0;
        {
            std::istringstream in(g);
            std::string field;
            while (n < 4 && std::getline(in, field, ',')) {
                try {
                    std::size_t used = 0;
                    if (n == 0) {
                        xMm = std::stod(field, &used);
                    } else if (n == 1) {
                        yMm = std::stod(field, &used);
                    } else if (n == 2) {
                        deg = std::stod(field, &used);
                    } else {
                        absolute = std::stoi(field, &used);
                    }
                    if (used != field.size()) {
                        break;
                    }
                } catch (const std::exception&) {
                    break;
                }
                ++n;
            }
        }
        if (n < 3) {
            fmt::print(stderr, "Guide invalide « {} » (attendu : x_mm,y_mm,angle_deg[,1=absolu])\n",
                       g);
            return 1;
        }
        params.guides.push_back(
            {Vec2um{to_micrometers(Millimeters{xMm}), to_micrometers(Millimeters{yMm})},
             deg * std::numbers::pi / 180.0, absolute != 0});
    }
    const auto result = auto_satin::generate_skeleton_satin(*region, params);
    if (!result) {
        fmt::print(stderr, "Erreur : {}\n", result.error().message);
        return 1;
    }
    std::size_t total = 0;
    fmt::print("Forme : {}  |  colonnes : {}\n", shape, result->columns.size());
    for (std::size_t c = 0; c < result->columns.size(); ++c) {
        const auto& cr = result->columns[c].crossings;
        double lo = 1e18, hi = 0.0, sum = 0.0;
        for (const auto& x : cr) {
            const double len = length_um(x.b - x.a) / 1000.0;
            lo = (std::min)(lo, len);
            hi = (std::max)(hi, len);
            sum += len;
        }
        total += cr.size();
        fmt::print(
            "  colonne {} : {} traversées, longueur {:.2f} / {:.2f} / {:.2f} mm (min/moy/max)\n",
            c + 1, cr.size(), cr.empty() ? 0.0 : lo,
            cr.empty() ? 0.0 : sum / static_cast<double>(cr.size()), hi);
    }
    const auto& d = result->diagnostics;
    fmt::print("Traversées : {}  |  morceaux : {}\n", total, d.pieces);
    fmt::print("Eventail : {} cordes  |  traversees raccourcies : {}\n", d.fan_chords,
               d.trimmed_crossings);
    fmt::print("Diagnostics : hors région {}  |  trop courtes {}  |  angle ramené {}  |  garde de "
               "rayon {}  |  guides orphelins {}\n",
               d.outside_samples, d.too_short, d.clamped_angle, d.radius_guard_hits,
               d.orphan_guides);
    if (d.coverage_measured) {
        fmt::print("Couverture estimée : {:.1f} %  |  fil en double : x{:.2f}  |  non couvert : "
                   "{:.1f} mm²\n",
                   d.coverage_ratio * 100.0, d.overlap_ratio, d.uncovered_area_mm2);
    }
    for (const auto& m : d.messages) {
        fmt::print("  ! {}\n", m);
    }
    if (!outSvg.empty()) {
        std::ofstream out(outSvg, std::ios::binary);
        out << auto_satin::skeleton_satin_to_svg(*region, *result);
        fmt::print("SVG : {}\n", outSvg);
    }
    return 0;
}

} // namespace

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

    std::string dz_image;
    std::string dz_dst;
    double dz_dpi = 96.0;
    int dz_max_colors = 8;
    int dz_min_region_px = 16;
    int dz_smoothing_px = 3;
    int dz_skip_bg = -1; // -1 = auto (segmentation::background_candidate), 0 = non, 1 = oui
    std::string dz_out_svg;
    double dz_trim_mm = 3.0;
    std::string dz_lock = "backforth";
    auto* dz_cmd = app.add_subcommand(
        "digitize", "Pipeline complet image -> DST (segmentation, numérisation automatique, "
                    "génération des points), sans IHM");
    dz_cmd->add_option("image", dz_image, "Image source (PNG, JPEG, BMP, TIFF)")->required();
    dz_cmd->add_option("sortie", dz_dst, "Fichier .dst à produire")->required();
    dz_cmd->add_option("--dpi", dz_dpi, "Résolution supposée pour l'échelle mm/px (défaut : 96)")
        ->check(CLI::PositiveNumber);
    dz_cmd->add_option("--max-colors", dz_max_colors, "Nombre maximal de couleurs (défaut : 8)")
        ->check(CLI::Range(2, 64));
    dz_cmd
        ->add_option("--min-region-px", dz_min_region_px,
                     "Taille minimale de région en px (défaut : 16)")
        ->check(CLI::PositiveNumber);
    dz_cmd->add_option("--smoothing-px", dz_smoothing_px, "Lissage des formes en px (défaut : 3)")
        ->check(CLI::NonNegativeNumber);
    dz_cmd->add_option(
        "--skip-background", dz_skip_bg,
        "Ignorer la plus grande région : -1 auto (défaut : fond quasi blanc touchant "
        "au moins 3 bords), 0 non, 1 oui");
    dz_cmd->add_option("--output-svg", dz_out_svg, "SVG de diagnostic à produire en plus du DST");
    dz_cmd
        ->add_option("--trim-threshold", dz_trim_mm,
                     "Coupe automatique au-delà de ce déplacement, en mm (défaut : 3)")
        ->check(CLI::PositiveNumber);
    std::string dz_mode = "shapes";
    double dz_detail = 0.5;
    std::string dz_technique = "auto";
    dz_cmd
        ->add_option("--mode", dz_mode,
                     "Stratégie : shapes (formes pleines, défaut) | contours (dessin au trait)")
        ->check(CLI::IsMember({"shapes", "contours"}));
    dz_cmd
        ->add_option("--detail", dz_detail, "Mode contours : niveau de détail 0..1 (défaut : 0.5)")
        ->check(CLI::Range(0.0, 1.0));
    dz_cmd
        ->add_option("--technique", dz_technique,
                     "Mode contours : auto (défaut) | running | satin (legacy, point droit)")
        ->check(CLI::IsMember({"auto", "running", "satin"}));
    dz_cmd->add_option("--lock", dz_lock, "Point d'arrêt : none|backforth|triangle|zigzag")
        ->check(CLI::IsMember({"none", "backforth", "triangle", "zigzag"}));

    std::string sd_shape = "circle";
    double sd_length = 3.0;
    int sd_repeats = 1;
    std::string sd_out;
    auto* sd_cmd = app.add_subcommand("stitchdebug",
                                      "Inspecte le moteur de points sur une forme de référence");
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

    std::string sa_shape = "rectangle";
    double sa_spacing = 0.4;
    std::vector<std::string> sa_guides;
    std::string sa_out;
    std::string sa_osp;
    std::string os_in, os_out;
    bool os_outlines = false;
    std::uint64_t os_only = 0;
    auto* os_cmd =
        app.add_subcommand("osp2svg", "Séquence effective d'un projet .osp en SVG de diagnostic");
    os_cmd->add_option("--osp", os_in, "Projet .osp")->required();
    os_cmd->add_option("--output", os_out, "SVG à produire")->required();
    os_cmd->add_flag("--outlines", os_outlines, "Superpose le contour des vecteurs");
    os_cmd->add_option("--only", os_only, "Id d'un objet brodé (les autres sont ignorés)");
    std::uint64_t sa_vector = 0;
    bool sa_pristine = false;
    auto* sa_cmd = app.add_subcommand(
        "satin-auto-debug",
        "Auto-satin par squelette et traversées orientées sur une forme de référence");
    sa_cmd->add_option("--shape", sa_shape,
                       "rectangle|capsule|ribbon|s|y|t|cross|h|circle|ring|wide|tiny|notch|pinch|"
                       "trident|star5|comb|E|e_trunk_isolated|multi_neck|two_holes|... "
                       "(corpus de auto_satin::make_shape)");
    sa_cmd->add_option("--spacing", sa_spacing, "Espacement des traversées en mm (défaut 0,4)")
        ->check(CLI::PositiveNumber);
    sa_cmd->add_option("--guide", sa_guides,
                       "Guide d'orientation x_mm,y_mm,angle_deg[,1=absolu] (répétable)");
    sa_cmd->add_option("--output-svg", sa_out, "SVG de diagnostic à produire");
    sa_cmd->add_option("--osp", sa_osp,
                       "Projet .osp dont on prend un vecteur (au lieu de --shape)");
    sa_cmd->add_option("--vector", sa_vector, "Id du vecteur dans le projet .osp");
    sa_cmd->add_flag("--pristine", sa_pristine,
                     "Avec --osp : utilise le contour brut de la région de segmentation");

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
    if (dz_cmd->parsed()) {
        return run_digitize(dz_image, dz_dst, dz_dpi, dz_max_colors, dz_min_region_px,
                            dz_smoothing_px, dz_skip_bg, dz_out_svg, dz_trim_mm, dz_lock, dz_mode,
                            dz_detail, dz_technique);
    }
    if (sd_cmd->parsed()) {
        return run_stitchdebug(sd_shape, sd_length, sd_repeats, sd_out, sd_underlay, sd_underpath);
    }
    if (os_cmd->parsed()) {
        return run_osp2svg(os_in, os_out, os_outlines, os_only);
    }
    if (sa_cmd->parsed()) {
        return run_satin_auto_debug(sa_shape, sa_spacing, sa_guides, sa_out, sa_osp, sa_vector,
                                    sa_pristine);
    }
    return 0;
}
