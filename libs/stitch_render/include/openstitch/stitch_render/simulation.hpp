// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <vector>

#include "openstitch/stitch/sequence.hpp"

namespace openstitch::stitch_render {

// HP-SIM-001..004 : état de la simulation de couture à un instant donné (cœur, sans Qt,
// déterministe). Un « pas » est l'indice d'une commande de la séquence EFFECTIVE.

// Indices de début des blocs de couleur (0 toujours présent) et des objets (première
// commande de couture de chaque suite d'un même objet source).
struct SimulationIndex {
    std::vector<int> color_starts;
    std::vector<int> object_starts;
};

[[nodiscard]] SimulationIndex build_simulation_index(const stitch::StitchSequence& sequence);

enum class SimulationUnit { Stitch, Object, Color };

// Pas cible en partant de `current` : `direction` > 0 vers l'avant, < 0 vers l'arrière.
// Résultat borné à [0, dernier indice] ; Stitch = +/-1, Object/Color = début du suivant
// (ou fin) / début de l'actuel puis du précédent.
[[nodiscard]] int simulation_step_target(const SimulationIndex& index, std::size_t command_count,
                                         int current, SimulationUnit unit, int direction);

// Hypothèses de durée (mêmes valeurs par défaut que la fiche de production).
struct SimulationTiming {
    double stitches_per_minute{700.0};
    double color_change_seconds{30.0};
    double trim_seconds{2.0};
};

// Minutes de couture écoulées jusqu'au pas `step` inclus (clampé à la séquence).
[[nodiscard]] double simulation_elapsed_minutes(const stitch::StitchSequence& sequence, int step,
                                                const SimulationTiming& timing);

// Rang (1..m) du bloc de couleur contenant `step`.
[[nodiscard]] int simulation_color_rank(const SimulationIndex& index, int step);

// Nombre de pas à avancer par seconde de lecture « temps réel machine ».
[[nodiscard]] double simulation_machine_steps_per_second(const SimulationTiming& timing);

} // namespace openstitch::stitch_render
