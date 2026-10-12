// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <string_view>

#include "openstitch/document/canvas.hpp"

namespace openstitch::document {

// Cadres courants des machines domestiques (dimensions de la zone de broderie
// annoncée par les fabricants, largeur x hauteur, en mm). Liste indicative :
// l'utilisateur garde la main sur la taille et la marge, et peut enregistrer
// ses propres cadres.
struct HoopPreset {
    std::string_view name;
    int width_mm;
    int height_mm;
};

inline constexpr std::array<HoopPreset, 8> kHoopPresets{{
    {"Petit 50 x 50 mm", 50, 50},
    {"Carré 100 x 100 mm", 100, 100},
    {"Standard 130 x 180 mm", 130, 180},
    {"Moyen 160 x 260 mm", 160, 260},
    {"Carré 200 x 200 mm", 200, 200},
    {"Grand 200 x 300 mm", 200, 300},
    {"Casquette 150 x 60 mm", 150, 60},
    {"Très grand 300 x 400 mm", 300, 400},
}};

// Canevas d'un modèle, avec la marge donnée.
[[nodiscard]] inline Canvas canvas_from_preset(const HoopPreset& preset, Micrometers margin = {}) {
    Canvas c;
    c.width = Micrometers{preset.width_mm * 1000};
    c.height = Micrometers{preset.height_mm * 1000};
    c.margin = margin;
    return c;
}

} // namespace openstitch::document
