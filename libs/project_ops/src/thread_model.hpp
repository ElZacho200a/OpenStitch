// SPDX-License-Identifier: Apache-2.0
// Interne à project_ops : brins de fil de la séquence EFFECTIVE d'un projet, et leur rasterisation
// dans le repère EXACT de l'image source. Base commune de fidelity, density, locate et diagnose.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/image/ops.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::project_ops::detail {

// Un brin cousu entre deux pénétrations consécutives d'un même objet (repère modèle : mm, Y vers le
// haut, origine au centre de l'image source ; voir `vectorize.cpp::to_model`).
struct ThreadSeg {
    double ax{0}, ay{0}, bx{0}, by{0};
    std::uint32_t object{0};
    std::uint32_t order{0};  // rang du brin dans l'ordre de couture (0 = premier)
    std::uint16_t colour{0}; // indice dans `ThreadModel::palette`
    stitch::StitchPass pass{stitch::StitchPass::TopStitch};
};

struct ThreadModel {
    std::vector<ThreadSeg> segs;
    std::vector<std::array<std::uint8_t, 3>>
        palette; // couleurs de fil distinctes (ordre d'apparition)
    stitch::StitchStats stats;
    double min_x{0}, min_y{0}, max_x{0}, max_y{0}; // boîte des brins, mm
    // Objets du projet par identifiant -> indice dans `project.embroidery_objects`.
    std::vector<std::pair<std::uint32_t, std::size_t>> object_index; // trié par identifiant
    [[nodiscard]] const document::EmbroideryObject* object(const document::Project& project,
                                                           std::uint32_t id) const;
    [[nodiscard]] std::size_t rank_of(std::uint32_t id) const; // rang dans le document, ou SIZE_MAX
};

// Image de travail : `original` passée par `project.ops` (vide si l'étape échoue). Repère de tous
// les outils d'analyse, comme la segmentation.
[[nodiscard]] image::Image working_image(const document::Project& project);

[[nodiscard]] Result<ThreadModel> build_thread_model(const document::Project& project);

// Grille d'analyse à l'échelle de l'image source (1 pixel = 1 pixel source) : couverture, couleur
// de fil (indice de palette +1, 0 = rien) et objet visible dessus (dernier brin cousu).
struct ThreadRaster {
    int width{0};
    int height{0};
    double mm_per_px{0.2};
    std::vector<std::uint8_t> covered;
    std::vector<std::uint16_t> colour; // palette + 1 ; 0 = non couvert
    std::vector<std::uint32_t> top_object;
};

// `thread_mm` : diamètre apparent du fil. Les brins hors grille sont ignorés.
[[nodiscard]] ThreadRaster rasterize_threads(const ThreadModel& model, int width, int height,
                                             double mm_per_px, double thread_mm);

// Conversions repère modèle (mm, Y haut) <-> pixel source (centre de pixel en +0,5).
[[nodiscard]] inline double mm_to_px_x(double x_mm, int width, double mm_per_px) {
    return x_mm / mm_per_px + width / 2.0;
}
[[nodiscard]] inline double mm_to_px_y(double y_mm, int height, double mm_per_px) {
    return height / 2.0 - y_mm / mm_per_px;
}
[[nodiscard]] inline double px_to_mm_x(double px, int width, double mm_per_px) {
    return (px - width / 2.0) * mm_per_px;
}
[[nodiscard]] inline double px_to_mm_y(double py, int height, double mm_per_px) {
    return (height / 2.0 - py) * mm_per_px;
}

// Composantes 4-connexes d'un masque, triées par aire décroissante (au plus `keep` conservées ;
// `total` reçoit le nombre total de composantes).
struct Component {
    std::size_t area{0};
    double cx{0}, cy{0}; // centroïde en pixels
    int min_x{0}, min_y{0}, max_x{0}, max_y{0};
};
[[nodiscard]] std::vector<Component> components(const std::vector<char>& mask, int width,
                                                int height, std::size_t keep,
                                                std::size_t* total = nullptr);

// Couleur sRGB -> CIELAB (D65).
struct Lab {
    double l{0}, a{0}, b{0};
};
[[nodiscard]] Lab to_lab(std::uint8_t r, std::uint8_t g, std::uint8_t b);
[[nodiscard]] double delta_e(const Lab& x, const Lab& y);

} // namespace openstitch::project_ops::detail
