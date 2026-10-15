// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "openstitch/autodigitize/contour_objects.hpp"
#include "openstitch/autodigitize/stipple_objects.hpp"
#include "openstitch/core/error.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/segmentation/segmentation.hpp"

namespace openstitch::project_ops {

// Convention d'erreur de cette lib : `Error::message` = phrase montrable, `Error::detail` =
// piste de correction (vide = aucune), comme le « Piste : » de la CLI.

// --- Création de projets -------------------------------------------------------
struct NewProjectOptions {
    double width_mm{100.0};
    double height_mm{100.0};
    double margin_mm{0.0};
    double trim_threshold_mm{3.0};
    std::string lock{"backforth"}; // none | backforth | triangle | zigzag
};

// Projet vide (aucune image, aucun objet) : cadre et finitions des nouveaux projets.
// Refuse un cadre non positif ou une marge qui dépasse la moitié du cadre.
[[nodiscard]] Result<document::Project> create_empty_project(const NewProjectOptions& options);

// Applique `lock` / `trim_threshold_mm` aux finitions ; erreur nommée si `lock` est inconnu.
[[nodiscard]] Result<void> apply_basic_finishing(document::Project& project,
                                                 double trim_threshold_mm, std::string_view lock);

// « Niveaux » d'optimisation des trajets (`between,within,underlay,regions|all`).
[[nodiscard]] Result<void> parse_route_levels(std::string_view list, document::HiddenTravel& out);

// --- Numérisation automatique image -> objets du document ----------------------
// Mêmes réglages et mêmes valeurs par défaut que `openstitch-cli digitize` et que le
// dialogue du bureau. Aucun algorithme ici : appel de `segmentation`, `autodigitize`.
struct DigitizeSettings {
    double dpi{96.0};
    int max_colors{8};
    int min_region_px{16};
    int smoothing_px{3};
    int skip_background{-1}; // -1 auto (segmentation::background_candidate), 0 non, 1 oui
    std::optional<double> min_feature_mm;
    std::optional<double> min_region_mm2;
    double trim_threshold_mm{3.0};
    std::string lock{"backforth"};
    bool hidden_travel{false};
    bool hidden_same_color{false};
    std::string hidden_levels{"all"};
    double hidden_max_mm{15.0};
    std::string mode{"shapes"};       // shapes | contours | stipple
    double detail{0.5};               // contours
    std::string technique{"auto"};    // contours : auto | running | legacy-satin
    double curve_tolerance_mm{0.0};   // shapes
    bool satin_bands{false};          // shapes
    bool single_path{false};          // contours
    bool coherent_lines{false};       // contours : FDoG avant segmentation
    int stipple_points{2000};
    int stipple_seed{1};
    bool stipple_invert{false};
};

struct DigitizeReport {
    std::size_t image_width_px{0};
    std::size_t image_height_px{0};
    bool image_had_alpha{false};
    std::size_t regions{0};
    bool background_skipped{false};
    bool background_auto{true}; // décision automatique (sinon option explicite)
    std::optional<segmentation::BackgroundCandidate> background_candidate;
    std::size_t vector_objects{0};
    std::size_t embroidery_objects{0};
    std::size_t satin{0};
    std::size_t tatami{0};
    std::size_t running{0};
    std::optional<autodigitize::ContourMetrics> contour; // mode contours
    std::optional<autodigitize::StippleMetrics> stipple; // mode stipple
    std::vector<std::string> warnings; // avertissements non bloquants du moteur
};

// Journal d'état optionnel (une ligne lisible par étape) : la CLI le branche sur stderr/stdout.
using StatusLog = std::function<void(std::string_view)>;

// Charge l'image, la place dans `project` (original, résolution), segmente et numérise :
// `project` reçoit image, segmentation, objets vectoriels et de broderie dans l'ordre de
// couture. `project` doit être vide d'objets (sinon E_REFUSED). Les objets sont ajoutés
// directement (création du projet, pas une édition : pas d'historique d'annulation).
[[nodiscard]] Result<DigitizeReport> digitize_image_into(document::Project& project,
                                                         const std::filesystem::path& image,
                                                         const DigitizeSettings& settings,
                                                         const StatusLog& log = {});

} // namespace openstitch::project_ops
