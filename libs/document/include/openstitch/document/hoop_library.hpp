// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <span>
#include <string_view>

#include "openstitch/document/canvas.hpp"

namespace openstitch::document {

// HP-HOOP-001 : bibliothèque de cadres GÉNÉRIQUES, classés par famille. Les dimensions sont
// des tailles courantes publiques (pouces usuels convertis en mm, diamètres ronds usuels) ;
// aucune base de données de marque n'est copiée. La zone indiquée est la zone de broderie
// annoncée (largeur x hauteur de la boîte englobante de la forme). Liste indicative :
// l'utilisateur garde la main sur la taille, la marge et peut enregistrer ses cadres.
struct HoopSpec {
    std::string_view id; // identifiant stable (référencé par les profils machine)
    std::string_view family; // « Domestique », « Rond », « Ovale », « Casquette », « Grand champ »
    std::string_view name; // libellé montrable
    HoopShape shape;
    int width_mm;
    int height_mm;
    int corner_radius_mm; // RoundedRectangle uniquement
};

inline constexpr std::array<HoopSpec, 16> kHoopLibrary{{
    {"rect-50x50", "Domestique", "Petit 50 x 50 mm", HoopShape::Rectangle, 50, 50, 0},
    {"rect-100x100", "Domestique", "4 x 4 po (100 x 100 mm)", HoopShape::Rectangle, 100, 100, 0},
    {"rect-130x180", "Domestique", "5 x 7 po (130 x 180 mm)", HoopShape::Rectangle, 130, 180, 0},
    {"rect-160x260", "Domestique", "6 x 10 po (160 x 260 mm)", HoopShape::Rectangle, 160, 260, 0},
    {"rect-200x300", "Grand champ", "8 x 12 po (200 x 300 mm)", HoopShape::Rectangle, 200, 300, 0},
    {"rect-240x360", "Grand champ", "9,5 x 14 po (240 x 360 mm)", HoopShape::Rectangle, 240, 360,
     0},
    {"rect-300x400", "Grand champ", "12 x 16 po (300 x 400 mm)", HoopShape::Rectangle, 300, 400, 0},
    {"rect-360x450", "Grand champ", "14 x 18 po (360 x 450 mm)", HoopShape::Rectangle, 360, 450, 0},
    {"round-60", "Rond", "Rond 60 mm", HoopShape::Ellipse, 60, 60, 0},
    {"round-100", "Rond", "Rond 100 mm", HoopShape::Ellipse, 100, 100, 0},
    {"round-150", "Rond", "Rond 150 mm", HoopShape::Ellipse, 150, 150, 0},
    {"round-200", "Rond", "Rond 200 mm", HoopShape::Ellipse, 200, 200, 0},
    {"oval-120x180", "Ovale", "Ovale 120 x 180 mm", HoopShape::Ellipse, 120, 180, 0},
    {"oval-200x260", "Ovale", "Ovale 200 x 260 mm", HoopShape::Ellipse, 200, 260, 0},
    {"rounded-130x180", "Domestique", "5 x 7 po coins arrondis", HoopShape::RoundedRectangle, 130,
     180, 20},
    {"cap-150x60", "Casquette", "Casquette 150 x 60 mm", HoopShape::RoundedRectangle, 150, 60, 10},
}};

[[nodiscard]] inline std::span<const HoopSpec> hoop_library() {
    return std::span<const HoopSpec>(kHoopLibrary);
}

// Cadre de la bibliothèque par identifiant ; nullptr si inconnu.
[[nodiscard]] inline const HoopSpec* find_hoop(std::string_view id) {
    for (const HoopSpec& h : kHoopLibrary) {
        if (h.id == id) {
            return &h;
        }
    }
    return nullptr;
}

// Canevas d'un cadre de la bibliothèque, avec la marge donnée.
[[nodiscard]] inline Canvas canvas_from_hoop(const HoopSpec& hoop, Micrometers margin = {}) {
    Canvas c;
    c.width = Micrometers{hoop.width_mm * 1000};
    c.height = Micrometers{hoop.height_mm * 1000};
    c.margin = margin;
    c.shape = hoop.shape;
    c.corner_radius = Micrometers{hoop.corner_radius_mm * 1000};
    return c;
}

} // namespace openstitch::document
