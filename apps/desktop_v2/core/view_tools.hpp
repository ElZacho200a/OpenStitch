// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- outils d'affichage purs (règle, grille) et export d'image de l'aperçu. Sans Qt :
// les calculs sont testables ; le canevas ne fait que les dessiner.

#include <filesystem>
#include <memory>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/core/task_control.hpp"
#include "openstitch/document/project.hpp"
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

// Exporte l'aperçu réaliste du document en PNG (écriture atomique), à `pxPerMm` (1..20) pixels par
// mm. Instantané immuable, annulable.
[[nodiscard]] Result<void> export_preview_png(std::shared_ptr<const document::Project> project,
                                              const std::filesystem::path& path, double pxPerMm,
                                              const TaskContext& task);

} // namespace openstitch::desktop_v2
