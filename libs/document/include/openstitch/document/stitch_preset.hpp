// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include "openstitch/document/embroidery_object.hpp"

namespace openstitch::document {

// Préréglage de points (HP-ENG-013) : un jeu de paramètres de couture nommé, réutilisable.
// L'application à un objet remplace ses `StitchParams` (type de point compris) par ceux du
// préréglage ; le nom est unique dans `Project::stitch_presets`.
struct StitchPreset {
    std::string name;
    StitchParams params{RunningStitchParams{}};

    bool operator==(const StitchPreset&) const = default;
};

} // namespace openstitch::document
