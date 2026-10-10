// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
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

// Contraste relatif WCAG 2.x entre deux couleurs sRGB : (L_clair + 0,05) /
// (L_sombre + 0,05) sur la luminance relative, dans [1 ; 21]. Symétrique.
[[nodiscard]] double wcag_contrast(std::array<std::uint8_t, 3> a,
                                   std::array<std::uint8_t, 3> b) noexcept;

// Meilleure paire de fils pour représenter DEUX couleurs (fond `first_target`,
// dessus `second_target`) d'un fondu à deux fils.
struct ThreadPairMatch {
    ThreadKey first;          // fil proche de `first_target`
    ThreadKey second;         // fil proche de `second_target`
    double cost{};            // valeur minimale du critère ci-dessous
    double distance_first{};  // CIEDE2000 first_target <-> first
    double distance_second{}; // CIEDE2000 second_target <-> second
    double contrast{};        // contraste WCAG entre les deux fils
};

// Minimise sur toutes les paires (t1, t2) du nuancier :
//   E = dE00(s1, t1) + dE00(s2, t2) + w * max(C(s1, s2) - C(t1, t2), 0)
// où C est le contraste WCAG : les deux fils choisis ne doivent pas être moins
// contrastés que les couleurs d'origine, sinon le fondu s'écrase (Liu et al.,
// CGF 2023, §4.3). `contrast_weight` (w) ramène le contraste, de 1 à 21, à
// l'ordre de grandeur des distances de couleur. Égalités départagées par
// l'ordre de déclaration du nuancier (premier fil, puis second) : déterministe.
// nullopt si le nuancier est vide.
[[nodiscard]] std::optional<ThreadPairMatch>
best_thread_pair(std::array<std::uint8_t, 3> first_target,
                 std::array<std::uint8_t, 3> second_target, const ThreadChart& chart,
                 double contrast_weight = 10.0);

} // namespace openstitch::thread_palette
