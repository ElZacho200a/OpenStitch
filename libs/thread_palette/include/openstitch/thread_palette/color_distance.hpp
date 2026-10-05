// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "openstitch/thread_palette/thread.hpp"

namespace openstitch::thread_palette {

// Couleur CIELAB (illuminant D65, observateur 2°).
struct CieLab {
    double l;
    double a;
    double b;
};

// Conversion sRGB [0,255] -> CIELAB, écrite depuis les formules standard
// (sRGB -> linéaire -> XYZ D65 -> CIELAB). Indépendante de
// cv::cvtColor (libs/segmentation) : approximation interne à OpenCV non
// spécifiée bit à bit, et thread_palette ne peut lier aucune bibliothèque
// tierce (C-S1-01).
[[nodiscard]] CieLab to_cielab(std::array<std::uint8_t, 3> srgb) noexcept;

// Différence perceptuelle CIEDE2000 entre deux couleurs CIELAB.
[[nodiscard]] double ciede2000(const CieLab& a, const CieLab& b) noexcept;

// Un résultat de recherche de fil le plus proche : identité + distance.
struct ThreadMatch {
    ThreadKey key;
    double distance{};
};

// Fils d'un nuancier les plus proches perceptuellement de `rgb`, triés par
// distance CIEDE2000 croissante. Égalités départagées par l'ordre de
// déclaration du nuancier (déterminisme, AD-S1-2). Retourne exactement
// `top_n` résultats quand `chart.threads.size() >= top_n` (sinon un
// résultat par fil du nuancier).
[[nodiscard]] std::vector<ThreadMatch> nearest_threads(std::array<std::uint8_t, 3> rgb,
                                                       const ThreadChart& chart, std::size_t top_n);

} // namespace openstitch::thread_palette
