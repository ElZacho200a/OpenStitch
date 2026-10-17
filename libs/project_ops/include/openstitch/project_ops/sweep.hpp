// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/project_ops/factory.hpp"

// Balayage d'options de numérisation : une grille de réglages en entrée, un tableau comparatif
// OBJECTIF en sortie (taille, coupes, tissu visible, détails perdus, surépaisseurs, défauts
// connus), avec les variantes non dominées marquées, et une planche de zooms réalistes.
namespace openstitch::project_ops {

// Un axe du balayage : une option (nom de l'option du CLI sans « -- ») et ses valeurs.
struct SweepAxis {
    std::string key;
    std::vector<std::string> values;
};

struct SweepOptions {
    DigitizeSettings base;       // réglages communs à toutes les variantes
    std::vector<SweepAxis> axes; // produit cartésien des axes (aucun axe : une seule variante)
    bool sheet{false};           // planche de zooms réalistes
    std::array<double, 4> sheet_crop{0.0, 0.0, 1.0, 1.0}; // zone de chaque vignette (fractions)
    int sheet_tile_px{480};                               // largeur d'une vignette
    int sheet_columns{3};
    std::filesystem::path keep_dir; // non vide : écrit chaque variante en .osp dans ce dossier
};

struct SweepRow {
    std::string label; // « island-cleanup=0 curve-tolerance=0.4 »
    std::vector<std::pair<std::string, std::string>> settings;
    bool ok{false};
    std::string error;
    std::size_t objects{0}, satin{0}, tatami{0}, running{0};
    std::size_t stitches{0}, jumps{0}, trims{0}, color_changes{0};
    double thread_m{0.0}, width_mm{0.0}, height_mm{0.0};
    double hole_percent{0.0}, lost_percent{0.0}, hotspot_mm2{0.0}, layers_max{0.0},
        connector_percent{0.0};
    std::size_t errors{0}, warnings{0};
    bool pareto{false}; // non dominée sur (coupes, tissu visible, détails perdus, surépaisseurs)
};

struct SweepReport {
    std::vector<SweepRow> rows;
    std::string markdown;
    std::string csv;
    std::string json; // schéma `openstitch.sweep/1`
    std::vector<std::uint8_t> sheet_png;
};

// Applique « clé = valeur » aux réglages (mêmes noms que les options de `digitize`).
// Erreur nommée si la clé ou la valeur est inconnue.
[[nodiscard]] Result<void> apply_digitize_setting(DigitizeSettings& settings,
                                                  const std::string& key, const std::string& value);
// Liste des clés acceptées, séparées par des virgules.
[[nodiscard]] std::string digitize_setting_keys();

[[nodiscard]] Result<SweepReport> run_sweep(const std::filesystem::path& image,
                                            const SweepOptions& options, const StatusLog& log = {});

} // namespace openstitch::project_ops
