// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <vector>

#include "openstitch/core/units.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::geometry {

// Partie cœur de l'outil de mesure (HP-VEC-010) : fonctions pures sur des
// points en micromètres. Les résultats sont en µm (ou µm²) ; l'affichage en mm
// passe par les accesseurs `*_mm`. Aucun arrondi n'est appliqué au calcul.

// Mesure d'un glisser « règle » entre deux points.
struct SegmentMeasure {
    Micrometers dx{};    // b.x - a.x
    Micrometers dy{};    // b.y - a.y (Y vers le haut)
    double length_um{0}; // distance euclidienne
    // Direction de a vers b, dans (-pi, pi], 0 = est, sens antihoraire.
    // Valeur 0 si a == b (voir `is_degenerate`).
    Angle direction{};

    [[nodiscard]] bool is_degenerate() const { return dx.value == 0 && dy.value == 0; }
    [[nodiscard]] Millimeters length_mm() const { return {length_um / 1000.0}; }
    [[nodiscard]] Millimeters dx_mm() const { return to_millimeters(dx); }
    [[nodiscard]] Millimeters dy_mm() const { return to_millimeters(dy); }
};

[[nodiscard]] SegmentMeasure measure_segment(Vec2um a, Vec2um b);

// Distance euclidienne en µm.
[[nodiscard]] double distance_um(Vec2um a, Vec2um b);

// Longueur d'un chemin en µm : les courbes sont aplaties à `tolerance`
// (cf. `flatten`), un chemin fermé inclut le segment de fermeture.
[[nodiscard]] double path_length_um(const Path& path, Micrometers tolerance);

// Longueur d'une polyligne de points (ouverte, ou fermée si `closed`).
[[nodiscard]] double polyline_length_um(const std::vector<Vec2um>& points, bool closed);

// Angle géométrique en `vertex` entre les rayons vers `a` et `b`, dans
// [0, pi]. nullopt si `a` ou `b` est confondu avec `vertex`.
[[nodiscard]] std::optional<Angle> angle_between(Vec2um vertex, Vec2um a, Vec2um b);

// Aire (µm², positive) du polygone fermé défini par les points « entre
// points ». Moins de 3 points -> 0. Un polygone auto-sécant donne l'aire
// algébrique (formule des lacets) en valeur absolue.
[[nodiscard]] double polygon_area_um2(const std::vector<Vec2um>& points);

// Aire (µm², positive) d'un chemin fermé, courbes aplaties à `tolerance`.
// Chemin ouvert ou de moins de 3 nœuds -> 0.
[[nodiscard]] double path_area_um2(const Path& path, Micrometers tolerance);

[[nodiscard]] constexpr Millimeters area_to_mm2(double area_um2) {
    return {area_um2 / 1.0e6};
}

} // namespace openstitch::geometry
