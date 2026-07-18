// SPDX-License-Identifier: Apache-2.0
#include <CLI/CLI.hpp>
#include <fmt/core.h>

#include <filesystem>
#include <string>

#include "openstitch/core/app_info.hpp"
#include "openstitch/core/log.hpp"
#include "openstitch/formats/dst.hpp"
#include "openstitch/formats/svg.hpp"
#include "openstitch/image/image.hpp"

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
    return 0;
}
