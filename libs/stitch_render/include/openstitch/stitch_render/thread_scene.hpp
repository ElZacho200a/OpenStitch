// SPDX-License-Identifier: Apache-2.0
#pragma once

// Scène de brins pour un moteur de rendu par lancer de rayons hors écran (RENDER-PBR-01, lot D).
// Format texte neutre « openstitch-thread-scene » v1 (JSON) : brins dans l'ordre de couture,
// repère écran en mm (Y vers le bas), couleur sRGB et largeur de fil. Lu par
// tools/raytrace/render_mitsuba.py ; aucun moteur n'est lié à OpenStitch. Déterministe.

#include <string>
#include <vector>

#include "openstitch/stitch_render/segments.hpp"

namespace openstitch::stitch_render {

[[nodiscard]] std::string thread_scene_json(const std::vector<ThreadSegment>& segments,
                                            double thread_width_mm);

} // namespace openstitch::stitch_render
