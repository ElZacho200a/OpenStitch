// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "openstitch/stitch_render/params.hpp"
#include "openstitch/stitch_render/segments.hpp"

namespace openstitch::stitch_render {

// Niveau de détail : sous ~1,6 pixel d'épaisseur de fil, l'ombrage n'est plus
// lisible et le rendu retombe sur de simples lignes épaisses colorées (le
// dessin en lignes est assuré par l'appelant).
enum class Detail : std::uint8_t {
    Lines,
    Realistic,
};

inline constexpr double kMinThreadPixels = 1.6;

[[nodiscard]] Detail choose_detail(double px_per_mm, const RenderParams& params);

// Fenêtre de rendu : rectangle du repère « écran » (mm, Y vers le bas) et
// résolution. L'image produite fait exactement width x height pixels.
struct RasterView {
    RectMm rect{};
    double px_per_mm{1.0};
    int width{0};
    int height{0};

    bool operator==(const RasterView&) const = default;
};

// Fenêtre couvrant `region` à la résolution `px_per_mm` demandée ; la
// résolution est abaissée si l'image dépasserait `max_pixels` (bornage de la
// mémoire). `region` vide -> fenêtre vide (0 x 0).
[[nodiscard]] RasterView plan_view(const RectMm& region, double px_per_mm, std::size_t max_pixels);

// Image RGBA 8 bits PRÉMULTIPLIÉE, lignes de haut en bas (compatible
// QImage::Format_RGBA8888_Premultiplied).
struct RasterImage {
    int width{0};
    int height{0};
    std::vector<std::uint8_t> rgba;
};

// Rend les brins en fils texturés dans `view` : fond de tissu (opaque ou
// transparent), ombre portée, puis chaque fil (section cylindrique éclairée
// en haut à gauche, reflet, torsion, extrémités qui plongent dans le tissu),
// empilés dans l'ordre des brins. Pur et déterministe : mêmes entrées, mêmes
// octets (le parallélisme est découpé en bandes de lignes indépendantes).
[[nodiscard]] RasterImage render_threads(const std::vector<ThreadSegment>& segments,
                                         const RenderParams& params, const RasterView& view);

} // namespace openstitch::stitch_render
