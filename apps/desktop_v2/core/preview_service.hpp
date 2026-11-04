// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- aperçu réaliste du projet : même moteur que `project_ops::render_realistic_png`
// (fils texturés de `stitch_render`), exécuté sur un INSTANTANÉ immuable du projet, hors fil
// d'interface. Un aperçu n'est jamais une modification du document.

#include <cstdint>
#include <memory>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/core/task_control.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::desktop_v2 {

struct PreviewImage {
    std::vector<std::uint8_t> png;
    int width{0};
    int height{0};
    double px_per_mm{0.0};
    std::uint64_t revision{0}; // révision du document rendue : l'UI écarte un aperçu périmé
};

[[nodiscard]] Result<PreviewImage> render_preview(std::shared_ptr<const document::Project> project,
                                                  std::uint64_t revision, double pxPerMm,
                                                  const TaskContext& task);

} // namespace openstitch::desktop_v2
