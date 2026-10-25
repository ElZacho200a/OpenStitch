// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "openstitch/document/project.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::stitch_generation {

// HP-ENG-008 -- passe commune de bornes de longueur de point, pour TOUS les générateurs.
// Elle s'applique à la séquence effective (première étape des finitions, avant les verrous) :
//  - fusion des points trop courts (`filter_short_stitches`, `min_stitch_length`) : un point
//    plus court que le minimum est retiré et le segment relie son voisin, si la nouvelle corde
//    reste dans la région de l'objet. Le dernier point d'un tracé est conservé ; si lui-même
//    est trop proche de son prédécesseur, c'est le prédécesseur qui est retiré. Jamais une
//    passe de verrou ni un point retouché à la main (passe Manual) ;
//  - découpage des points trop longs (`split_long_stitches`, `max_stitch_length`) en
//    ceil(L / max) points égaux sur la MÊME droite (morceaux >= max / 2).
// Fonction pure, déterministe, coordonnées entières (µm).
struct LengthBoundsReport {
    std::size_t merged{0};     // points retirés par la fusion
    std::size_t split{0};      // points longs découpés
    std::size_t added{0};      // pénétrations ajoutées par le découpage
    std::size_t short_left{0}; // segments encore < min en sortie (non fusionnables)
    std::size_t long_left{0};  // segments encore > max en sortie
};

struct LengthBoundsResult {
    std::vector<stitch::StitchCommand> commands;
    LengthBoundsReport report;
};

[[nodiscard]] LengthBoundsResult
enforce_length_bounds(const std::vector<stitch::StitchCommand>& cmds,
                      const document::Project& project,
                      const document::SequenceFinishing& finishing);

// Mesure : segments cousus (deux Stitch consécutifs, hors passe Lock à l'arrivée) plus courts
// que `min_um` (si > 0) ou plus longs que `max_um` (si > 0). Sert aux tests, à l'analyse et au
// rapport de `enforce_length_bounds`.
[[nodiscard]] LengthBoundsReport
measure_length_bounds(const std::vector<stitch::StitchCommand>& cmds, std::int32_t min_um,
                      std::int32_t max_um);

} // namespace openstitch::stitch_generation
