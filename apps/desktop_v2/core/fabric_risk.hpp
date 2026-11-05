// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- risque de plissement et déformation prévue du tissu : le solveur mécanique 2D du
// moteur (`project_ops::predict_fabric_deformation`) appliqué à la séquence EFFECTIVE, sur un
// instantané, hors fil d'interface. Le résultat est une carte RGBA (un pixel par élément), des
// flèches de déplacement prévu et un verdict chiffré.
//
// HONNÊTETÉ : le modèle n'est PAS calibré sur pièce-test (`calibrated == false` tant que ce n'est
// pas le cas). C'est une estimation relative, utile pour comparer deux versions d'un motif, pas
// une mesure physique ; le verdict le dit toujours. Sans Qt.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/core/task_control.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::desktop_v2 {

struct DisplacementArrow {
    double xMm{0.0}; // position nominale, repère modèle (Y vers le haut)
    double yMm{0.0};
    double dxMm{0.0}; // déplacement prévu
    double dyMm{0.0};
};

struct FabricRisk {
    int columns{0};
    int rows{0};
    std::vector<std::uint8_t> rgba; // ligne 0 = haut ; transparent là où il n'y a ni fil ni risque
    double minXmm{0.0};
    double minYmm{0.0};
    double widthMm{0.0};
    double heightMm{0.0};

    double maxWrinkleIndex{0.0}; // 1 = seuil critique atteint
    double p99WrinkleIndex{0.0};
    double wrinkleAreaMm2{0.0};
    double maxDisplacementMm{0.0};
    double maxWrinkleXmm{0.0};
    double maxWrinkleYmm{0.0};
    bool converged{false};
    bool calibrated{false};
    std::string material;
    std::string stabilizer;
    std::vector<DisplacementArrow> arrows; // échantillon régulier du champ de déplacement
    std::uint64_t revision{0};

    [[nodiscard]] bool empty() const { return rgba.empty(); }
    // Niveau de risque en mots : « faible », « à surveiller », « élevé ».
    [[nodiscard]] std::string level() const;
    // Verdict lisible, avec l'avertissement « estimation non calibrée » tant que c'est le cas.
    [[nodiscard]] std::string verdict() const;
};

[[nodiscard]] Result<FabricRisk> runFabricRisk(std::shared_ptr<const document::Project> project,
                                               std::uint64_t revision, const TaskContext& task);

} // namespace openstitch::desktop_v2
