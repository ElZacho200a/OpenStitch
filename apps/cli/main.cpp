// SPDX-License-Identifier: Apache-2.0
#include <CLI/CLI.hpp>
#include <fmt/core.h>

#include <filesystem>
#include <string>

#include "openstitch/core/app_info.hpp"
#include "openstitch/core/log.hpp"
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

    CLI11_PARSE(app, argc, argv);

    if (info_cmd->parsed()) {
        return run_info(image_path, dpi);
    }
    return 0;
}
