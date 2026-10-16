// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::project_ops {

// Rendu réaliste (fils texturés, le MÊME moteur que l'aperçu de l'application : `stitch_render`)
// d'un projet, sans interface. Sert à regarder un résultat comme le verrait l'utilisateur, sur un
// fond de couleur franche (vert) qui révèle les trous et les interstices.
struct RenderOptions {
    double px_per_mm{5.0};
    // Fond de tissu (aplat). Vert vif par défaut : tout ce qui n'est pas brodé se voit.
    std::array<std::uint8_t, 3> fabric_rgb{0, 255, 0};
    double thread_width_mm{0.35};
    // Zone rendue, en fractions de la boîte englobante des points, repère image (origine en haut à
    // gauche) : x0, y0, x1, y1 dans [0, 1]. {0,0,1,1} = tout le motif.
    std::array<double, 4> crop{0.0, 0.0, 1.0, 1.0};
    // Borne mémoire : la résolution est abaissée si l'image dépasserait ce nombre de pixels.
    std::size_t max_pixels{40'000'000};
};

struct RenderResult {
    std::vector<std::uint8_t> png;
    int width{0};
    int height{0};
    double px_per_mm{0.0}; // résolution réellement utilisée (peut être abaissée)
};

// PNG du motif de la séquence EFFECTIVE (`effective_sequence`), objets masqués exclus.
// Déterministe : mêmes entrées, mêmes octets.
[[nodiscard]] Result<RenderResult> render_realistic_png(const document::Project& project,
                                                        const RenderOptions& options = {});

} // namespace openstitch::project_ops
