// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cmath>
#include <compare>
#include <cstdint>

namespace openstitch {

// Unité interne du modèle : le micromètre, en entier 32 bits (ADR-003).
// Plage ±2 147 m : très au-delà de tout cadre de broderie. Le pas DST
// (0,1 mm = 100 µm) se convertit par division entière exacte.
struct Micrometers {
    std::int32_t value{0};

    constexpr auto operator<=>(const Micrometers&) const = default;

    constexpr Micrometers operator-() const { return {-value}; }
    constexpr Micrometers& operator+=(Micrometers o) { value += o.value; return *this; }
    constexpr Micrometers& operator-=(Micrometers o) { value -= o.value; return *this; }
    friend constexpr Micrometers operator+(Micrometers a, Micrometers b) { return {a.value + b.value}; }
    friend constexpr Micrometers operator-(Micrometers a, Micrometers b) { return {a.value - b.value}; }
};

// Millimètres en double : réservé à l'affichage et aux paramètres saisis
// par l'utilisateur. Jamais stocké comme coordonnée du modèle.
struct Millimeters {
    double value{0.0};
    constexpr auto operator<=>(const Millimeters&) const = default;
};

// Pixels : réservé au domaine image. La conversion vers Micrometers exige
// une résolution explicite (mm/pixel) — jamais implicite.
struct Pixels {
    double value{0.0};
    constexpr auto operator<=>(const Pixels&) const = default;
};

struct Angle {
    double radians{0.0};
    constexpr auto operator<=>(const Angle&) const = default;
};

[[nodiscard]] constexpr Millimeters to_millimeters(Micrometers um) {
    return {static_cast<double>(um.value) / 1000.0};
}

// Arrondi au micromètre le plus proche.
[[nodiscard]] inline Micrometers to_micrometers(Millimeters mm) {
    return {static_cast<std::int32_t>(std::lround(mm.value * 1000.0))};
}

[[nodiscard]] inline Micrometers to_micrometers(Pixels px, Millimeters mm_per_pixel) {
    return {static_cast<std::int32_t>(std::lround(px.value * mm_per_pixel.value * 1000.0))};
}

// Vecteur 2D en micromètres. Repère du modèle : origine au centre du
// canevas, X vers la droite, Y vers le haut. L'inversion de Y est un
// détail de la couche de rendu.
struct Vec2um {
    Micrometers x{};
    Micrometers y{};

    constexpr auto operator<=>(const Vec2um&) const = default;
    friend constexpr Vec2um operator+(Vec2um a, Vec2um b) { return {a.x + b.x, a.y + b.y}; }
    friend constexpr Vec2um operator-(Vec2um a, Vec2um b) { return {a.x - b.x, a.y - b.y}; }
};

[[nodiscard]] inline double length_um(Vec2um v) {
    const double dx = static_cast<double>(v.x.value);
    const double dy = static_cast<double>(v.y.value);
    return std::sqrt(dx * dx + dy * dy);
}

namespace literals {
constexpr Micrometers operator""_um(unsigned long long v) { return {static_cast<std::int32_t>(v)}; }
constexpr Millimeters operator""_mm(long double v) { return {static_cast<double>(v)}; }
constexpr Millimeters operator""_mm(unsigned long long v) { return {static_cast<double>(v)}; }
}  // namespace literals

}  // namespace openstitch
