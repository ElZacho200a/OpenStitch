// SPDX-License-Identifier: Apache-2.0
// Interne à autodigitize : anneaux sombres (cercles / ellipses épaissis : hublots, ventouses,
// bulles, yeux) reconnus sur le masque des pixels sombres de l'image.
#pragma once

#include <vector>

namespace openstitch::autodigitize::detail {

// Ellipse INTÉRIEURE de l'anneau (le trou qu'il entoure) et épaisseur, en pixels. Repère image :
// pixel (x, y) a son centre en (x + 0,5 ; y + 0,5), Y vers le bas.
struct RingSpec {
    double cx{0}, cy{0};
    double a{0}, b{0}; // demi-axes intérieurs (a >= b)
    double angle{0};   // direction du grand axe (rad, Y vers le bas)
    double width{0};   // épaisseur du trait sombre
};

struct RingDetectOptions {
    double min_inner_diameter_px{4.0};
    double max_inner_diameter_px{200.0};
    double min_width_px{2.0};
    double max_width_px{40.0};
};

// `dark` : 1 = pixel sombre (taille width x height). Un anneau = un trou enclos (composante
// non sombre ne touchant pas le bord) quasi elliptique, entouré sur l'essentiel de son pourtour
// d'une bande sombre d'épaisseur régulière. Déterministe (balayage ligne par ligne).
[[nodiscard]] std::vector<RingSpec> detect_rings(const std::vector<char>& dark, int width,
                                                 int height, const RingDetectOptions& options);

// Pixels sombres de la bande de l'anneau (entre l'ellipse intérieure et l'extérieure).
void paint_ring_band(const RingSpec& ring, const std::vector<char>& dark, int width, int height,
                     std::vector<char>& out);

} // namespace openstitch::autodigitize::detail
