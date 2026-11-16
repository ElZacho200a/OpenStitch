// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- outils d'affichage purs (règle, grille) et export d'image de l'aperçu. Sans Qt :
// les calculs sont testables ; le canevas ne fait que les dessiner.

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/core/task_control.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/stitch_analysis/placement_template.hpp"
#include "preview_service.hpp"
#include "shape_batch.hpp"

namespace openstitch::desktop_v2 {

// Règle : mesure entre deux points en mm « modèle » (Y vers le haut) ; l'angle est antihoraire
// depuis l'axe X, dans ]-180 ; 180].
struct Measurement {
    double dxMm{0};
    double dyMm{0};
    double distanceMm{0};
    double angleDeg{0};
};
[[nodiscard]] Measurement measure(PointMm from, PointMm to);

// Grille : positions (pixels de l'image d'aperçu) des traits verticaux et horizontaux, alignés sur
// l'origine du modèle (centre du cadre). Vide si l'écart est invalide ou donnerait plus de
// `kMaxGridLines` traits (grille illisible, mieux vaut ne rien tracer).
inline constexpr std::size_t kMaxGridLines = 400;
struct GridLines {
    std::vector<double> verticalPx;
    std::vector<double> horizontalPx;
    [[nodiscard]] bool empty() const { return verticalPx.empty() && horizontalPx.empty(); }
};
[[nodiscard]] GridLines gridLines(const PreviewImage& frame, double spacingMm);

// Exporte l'aperçu réaliste du document en PNG (écriture atomique), à `pxPerMm` pixels par mm,
// dans la plage du rendu [kMinPxPerMm ; kMaxPxPerMm]. Instantané immuable, annulable.
[[nodiscard]] Result<void> export_preview_png(std::shared_ptr<const document::Project> project,
                                              const std::filesystem::path& path, double pxPerMm,
                                              const TaskContext& task);

// --- Plans d'export (CX07) : ce que l'export produira, calculé AVANT d'écrire ------------------
// Image : taille réelle en pixels (même fenêtre et même plafond de pixels que le rendu exporté).
// `reduced` : la résolution a été abaissée pour rester sous le plafond.
struct ImageExportPlan {
    int width{0};
    int height{0};
    double pxPerMm{0};
    bool reduced{false};
    double widthMm{0}; // fenêtre rendue : cadre ∪ points
    double heightMm{0};
};
[[nodiscard]] Result<ImageExportPlan>
plan_image_export(std::shared_ptr<const document::Project> project, double pxPerMm,
                  const TaskContext& task);

// Papier du gabarit de placement : « A4 », « A3 » ou « Letter », portrait ou paysage. nullopt si
// le papier est inconnu.
[[nodiscard]] std::optional<stitch_analysis::PlacementTemplateOptions>
placement_template_options(const std::string& paper, bool landscape, const std::string& title);

// Gabarit : même pavage que l'export PDF (pages, colonnes, lignes, emprise des points).
struct TemplateExportPlan {
    std::size_t pages{0};
    std::size_t columns{0};
    std::size_t rows{0};
    double designWidthMm{0};
    double designHeightMm{0};
    double pageWidthMm{0}; // orientation appliquée
    double pageHeightMm{0};
};
[[nodiscard]] Result<TemplateExportPlan>
plan_placement_template(const document::Project& project,
                        const stitch_analysis::PlacementTemplateOptions& options,
                        const TaskContext& task);

} // namespace openstitch::desktop_v2
