// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstitch/document/project.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::stitch_generation {

// Finitions de la séquence (Lots E et F, audit marine plein cadre
// 2026-09-22), réglées par `project.finishing` (`document::SequenceFinishing`).
// Troisième et dernière passe de `effective_sequence`, APRÈS les retouches
// manuelles : les index des retouches visent la vue brute (`raw_slice`), que
// cette passe ne modifie pas.
//
// Un « tracé » est une suite de points cousus consécutifs ; entre deux tracés
// se trouvent des déplacements (Jump), coupes ou changements de fil.
// - Déplacement plus long que `trim_threshold` (ou changement de fil si
//   `trim_before_color_change`) : point d'arrêt de sortie, `Trim`,
//   déplacement, point d'arrêt d'entrée.
// - Déplacement entre deux objets distincts : toujours une coupe, quelle que
//   soit sa longueur (le seuil ne protège que les sauts internes à un objet).
// - Déplacement plus court, dans le même objet : simple saut, sans coupe ni
//   verrou.
// - Changement d'objet : toujours un point d'arrêt de sortie et d'entrée
//   (sauf si l'objet porte déjà les siens, passe `Lock`).
// Les verrous (`lock_stitches`, passe `Lock`) sont posés le long du point
// voisin du tracé, donc sur la matière de l'objet. `enabled == false` :
// séquence rendue telle quelle. Fonction pure et déterministe.
[[nodiscard]] stitch::StitchSequence finish_sequence(const stitch::StitchSequence& sequence,
                                                     const document::Project& project);

} // namespace openstitch::stitch_generation
