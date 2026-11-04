// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- aperçu réaliste du projet : mêmes moteurs que `project_ops::render_realistic_png`
// (fils texturés de `stitch_render`), exécuté sur un INSTANTANÉ immuable, hors fil d'interface,
// avec en plus le repère de l'image (pour relier pixels et mm) et l'index de sélection. Un aperçu
// n'est jamais une modification du document.

#include <cstdint>
#include <memory>
#include <vector>

#include "hit_index.hpp"
#include "openstitch/core/error.hpp"
#include "openstitch/core/task_control.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::desktop_v2 {

struct PreviewImage {
    std::vector<std::uint8_t> png;
    int width{0};
    int height{0};
    double px_per_mm{0.0};
    double origin_x_mm{0.0}; // coin haut-gauche de l'image, repère mm « écran » (Y vers le bas)
    double origin_y_mm{0.0};
    std::uint64_t revision{0}; // révision du document rendue : l'UI écarte un aperçu périmé
    std::shared_ptr<const HitIndex> hits; // objets VISIBLES cousus ; nul si un id dépasse 24 bits

    [[nodiscard]] double pxToMmX(double px) const { return origin_x_mm + px / px_per_mm; }
    [[nodiscard]] double pxToMmY(double py) const { return origin_y_mm + py / px_per_mm; }
    [[nodiscard]] double mmToPxX(double mm) const { return (mm - origin_x_mm) * px_per_mm; }
    [[nodiscard]] double mmToPxY(double mm) const { return (mm - origin_y_mm) * px_per_mm; }
};

[[nodiscard]] Result<PreviewImage> render_preview(std::shared_ptr<const document::Project> project,
                                                  std::uint64_t revision, double pxPerMm,
                                                  const TaskContext& task);

} // namespace openstitch::desktop_v2
