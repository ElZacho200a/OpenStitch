// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "openstitch/core/units.hpp"

namespace openstitch::document {

// Forme du cadre (HP-HOOP-001). Le rectangle est le cas historique.
enum class HoopShape : std::uint8_t {
    Rectangle,        // cadre rectangulaire
    Ellipse,          // rond (largeur = hauteur) ou ovale ; inscrit dans width x height
    RoundedRectangle, // rectangle aux coins arrondis (`corner_radius`)
};

// Canevas de travail : zone physique dans laquelle le motif doit tenir.
// Représente pour l'instant un cadre rectangulaire simple ; les profils de
// machines (formes) viendront en Phase 16 du cahier des charges.
// Repère : origine au centre, X vers la droite, Y vers le haut.
struct Canvas {
    Micrometers width{100'000}; // 100 mm par défaut (cadre courant 10x10 cm)
    Micrometers height{100'000};
    // Marge de sécurité entre le bord du cadre et la zone réellement brodable
    // (pied presseur, collision avec le cadre) : 0 = tout le cadre est brodable
    // (projets existants). La zone brodable est le cadre réduit de cette marge
    // sur chacun de ses quatre côtés.
    Micrometers margin{0};
    // Forme du cadre dans le rectangle width x height (HP-HOOP-001). Absente des projets
    // anciens : rectangle.
    HoopShape shape{HoopShape::Rectangle};
    // Rayon des coins du cadre (RoundedRectangle uniquement), borné à la moitié du plus petit
    // côté de la zone brodable.
    Micrometers corner_radius{0};

    // Dimensions de la zone brodable, jamais négatives.
    [[nodiscard]] Micrometers usable_width() const {
        return Micrometers{std::max(0, width.value - 2 * margin.value)};
    }
    [[nodiscard]] Micrometers usable_height() const {
        return Micrometers{std::max(0, height.value - 2 * margin.value)};
    }

    // Le point (repère centre, Y vers le haut) est-il dans la zone brodable, forme comprise ?
    // Bord inclus. Le rectangle reste exactement le test des bornes (arithmétique entière).
    [[nodiscard]] bool contains_usable(Vec2um p) const {
        const std::int64_t hw = usable_width().value / 2;
        const std::int64_t hh = usable_height().value / 2;
        const std::int64_t ax = std::abs(static_cast<std::int64_t>(p.x.value));
        const std::int64_t ay = std::abs(static_cast<std::int64_t>(p.y.value));
        if (ax > hw || ay > hh) {
            return false;
        }
        switch (shape) {
        case HoopShape::Rectangle:
            return true;
        case HoopShape::Ellipse: {
            if (hw == 0 || hh == 0) {
                return ax == 0 && ay == 0;
            }
            const double nx = static_cast<double>(ax) / static_cast<double>(hw);
            const double ny = static_cast<double>(ay) / static_cast<double>(hh);
            return nx * nx + ny * ny <= 1.0 + 1e-9;
        }
        case HoopShape::RoundedRectangle: {
            const std::int64_t r = std::min<std::int64_t>(
                std::max(0, corner_radius.value - margin.value), std::min(hw, hh));
            const std::int64_t cx = ax - (hw - r);
            const std::int64_t cy = ay - (hh - r);
            if (cx <= 0 || cy <= 0) {
                return true; // hors des quatre quarts de coin
            }
            return cx * cx + cy * cy <= r * r;
        }
        }
        return true;
    }

    bool operator==(const Canvas&) const = default;
};

} // namespace openstitch::document
