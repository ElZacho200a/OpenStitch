// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstitch/document/project.hpp"
#include "openstitch/stitch_render/params.hpp"

namespace openstitch::desktop {

// HP-THR-008 : le tissu du PROJET l'emporte sur la couleur de tissu des préférences de rendu
// réaliste, dès qu'il a été défini (différent du défaut). Un projet qui n'a pas de tissu garde le
// rendu historique.
[[nodiscard]] inline stitch_render::RenderParams
withProjectFabric(stitch_render::RenderParams params, const document::Project& project) {
    if (!(project.fabric == document::FabricSettings{})) {
        params.fabric_rgb = project.fabric.color;
    }
    return params;
}

} // namespace openstitch::desktop
