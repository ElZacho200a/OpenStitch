// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "openstitch/commands/command.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/segmentation/region_growing.hpp"

namespace openstitch::commands {

// Clic-pour-broder (HP-AUTO-003) : transforme la région obtenue par croissance
// (`segmentation::grow_region`) en objet vectoriel + objet de broderie, en UNE commande annulable.
enum class ClickToStitchKind : std::uint8_t {
    Auto,    // même classification que l'auto-numérisation : tatami, contour si trop petit,
             // satin pour une bande étroite
    Fill,    // remplissage tatami, toujours
    Satin,   // auto-satin (squelette)
    Contour, // contour seul (point droit le long du bord de la zone)
};

struct ClickToStitchOptions {
    Millimeters mm_per_px{25.4 / 96.0}; // résolution de l'image dont vient la région
    Micrometers simplify_tolerance{200};
    ClickToStitchKind kind{ClickToStitchKind::Auto};
};

struct ClickToStitchResult {
    // Nulle si la zone ne donne aucun objet ; `error` dit pourquoi (montrable tel quel).
    std::unique_ptr<ICommand> command;
    std::string error;
    ObjectId vector_id;     // objet vectoriel créé (invalide si échec)
    ObjectId embroidery_id; // objet de broderie créé (invalide si échec)
};

// Calcule la géométrie MAINTENANT (les identifiants sont tirés de `project.object_ids`, comme les
// autres fabriques de commandes) et renvoie la commande ; ne modifie pas le contenu du document.
// La région doit provenir de l'image du projet (même grille de pixels que celle dont `mm_per_px`
// est la résolution). L'objet prend la couleur moyenne de la zone. Annuler retire exactement les
// deux objets ; refaire les remet avec les mêmes identifiants.
[[nodiscard]] ClickToStitchResult
make_click_to_stitch_command(document::Project& project, const segmentation::GrownRegion& region,
                             const ClickToStitchOptions& options);

} // namespace openstitch::commands
