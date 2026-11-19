// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- sélection au lasso. Calcul pur (sans Qt) : quels objets, parmi leurs boîtes, un
// contour tracé à main levée désigne. Le polygone et les boîtes doivent être dans le MÊME repère
// (mm scène). Un lasso est toujours fermé implicitement (dernier point relié au premier).

#include <cstdint>
#include <functional>
#include <map>
#include <vector>

#include "openstitch/stitch_render/segments.hpp"
#include "shape_batch.hpp"

namespace openstitch::desktop_v2 {

enum class LassoMode : std::uint8_t {
    Enclosed, // objets ENTIÈREMENT dans le lasso (comme le cadre glissé vers la droite)
    Touching, // tout objet touché par le lasso ou qui le contient (cadre glissé vers la gauche)
};

// Point dans un polygone (règle pair-impair ; un polygone auto-sécant reste défini).
[[nodiscard]] bool pointInPolygon(PointMm p, const std::vector<PointMm>& polygon);

// Vrai si le segment [a, b] a au moins un point dans le rectangle (bords compris).
[[nodiscard]] bool segmentTouchesRect(PointMm a, PointMm b, const stitch_render::RectMm& box);

// Identifiants (ordre croissant, déterministe) des objets désignés. `accept` écarte les objets
// masqués ou verrouillés ; vide = tous acceptés. Moins de 3 points : aucun objet.
[[nodiscard]] std::vector<std::uint64_t>
lassoSelect(const std::vector<PointMm>& polygon,
            const std::map<std::uint64_t, stitch_render::RectMm>& boxes, LassoMode mode,
            const std::function<bool(std::uint64_t)>& accept = {});

} // namespace openstitch::desktop_v2
