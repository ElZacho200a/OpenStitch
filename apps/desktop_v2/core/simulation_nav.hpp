// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- navigation de la simulation de couture : sauter au début de l'objet ou de la
// couleur suivante / précédente, avancer d'un point, aller à la fin. Les positions sont des nombres
// de commandes de la séquence EFFECTIVE (celle que la simulation dessine). Sans Qt, lecture seule.

#include <cstddef>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::desktop_v2 {

struct SimulationMarks {
    std::vector<std::size_t>
        objects; // positions où un nouvel objet commence (0 exclu), croissantes
    std::vector<std::size_t> colours; // positions juste APRÈS un changement de fil, croissantes
    std::vector<std::size_t> jumps;   // positions des sauts (Jump commands)
    std::vector<std::size_t> trims;   // positions des coupes (Trim commands)
    std::size_t total{0};             // nombre total de commandes
};

[[nodiscard]] Result<SimulationMarks> simulationMarks(const document::Project& project);
// Use the already-generated effective stitch sequence for Studio simulation navigation.
[[nodiscard]] SimulationMarks simulationMarks(const stitch::StitchSequence& sequence);

// Première marque STRICTEMENT après `position`, ou `total` s'il n'y en a plus.
[[nodiscard]] std::size_t nextMark(const std::vector<std::size_t>& marks, std::size_t position,
                                   std::size_t total);
// Dernière marque STRICTEMENT avant `position`, ou 0 s'il n'y en a plus.
[[nodiscard]] std::size_t previousMark(const std::vector<std::size_t>& marks, std::size_t position);

} // namespace openstitch::desktop_v2
