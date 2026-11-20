// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <type_traits>
#include <variant>

#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/document/vector_object.hpp"

namespace openstitch::document {

// Lignes ouvertes, citoyennes du moteur (docs/source/lignes-ouvertes.md).
//
// Une forme est une LIGNE OUVERTE quand tous ses morceaux sont des chemins non fermés, sans trou :
// un trait détecté sur l'image, un tracé à la main, un chemin SVG/DXF ouvert. Elle n'a pas
// d'intérieur : seuls les points qui suivent un chemin la cousent (point droit et variantes,
// boutonnière, motif le long du chemin, satin de bordure à rails ouverts). Un remplissage
// fermerait le chemin en silence (corde entre ses extrémités) : il est refusé à la conversion.
// Une forme mixte (morceaux ouverts et fermés) reste une surface : comportement historique.
[[nodiscard]] inline bool is_open_line(const VectorObject& shape) {
    if (shape.paths.empty()) {
        return false;
    }
    for (const auto& set : shape.paths) {
        if (set.outer.closed || !set.holes.empty() || set.outer.nodes.size() < 2) {
            return false;
        }
    }
    return true;
}

// Vrai si ce type de points a besoin d'une SURFACE (intérieur fermé) pour être cousu. Faux pour
// les points qui suivent un chemin, ouvert ou fermé, et pour le satin à rails (sa géométrie est
// portée par ses propres rails, qui peuvent être ouverts).
[[nodiscard]] inline bool stitch_needs_area(const StitchParams& params) {
    return std::visit(
        [](const auto& p) {
            using T = std::decay_t<decltype(p)>;
            return !(std::is_same_v<T, RunningStitchParams> ||
                     std::is_same_v<T, BlanketStitchParams> || std::is_same_v<T, MotifRunParams> ||
                     std::is_same_v<T, SatinParams>);
        },
        params);
}

} // namespace openstitch::document
