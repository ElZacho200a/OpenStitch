// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "openstitch/core/units.hpp"
#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::document {

// Polygone d'un satin à rails : rail A puis rail B parcouru à l'envers (courbes aplaties).
[[nodiscard]] geometry::PathSet satin_polygon(const SatinParams& params, Micrometers tolerance);

// Emprise nominale d'un objet de broderie : le polygone des rails pour un satin à rails, le
// contour vectoriel source (trous compris, courbes aplaties) pour les autres surfaces. Vide
// pour un contour (running), un objet invisible ou sans vecteur source. C'est la zone que
// l'objet est CENSÉ couvrir, pas celle que ses points balaient (cf. stitch_analysis).
[[nodiscard]] std::vector<geometry::PathSet>
embroidery_footprint(const Project& project, const EmbroideryObject& object, Micrometers tolerance);

} // namespace openstitch::document
