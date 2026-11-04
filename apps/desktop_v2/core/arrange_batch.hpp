// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- aligner, répartir, grouper, dégrouper et placer numériquement la sélection, par
// lots `project_ops` (`place`, `group`, `ungroup`) : même chemin que Claude et la CLI, UNE étape
// d'annulation par action. Sans Qt. Repère : mm modèle, Y vers le haut. Une demande sans effet
// (moins d'objets que nécessaire, rien à bouger) renvoie une chaîne vide.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "openstitch/document/project.hpp"

namespace openstitch::desktop_v2 {

enum class AlignMode { Left, CenterH, Right, Top, CenterV, Bottom };
enum class DistributeAxis { Horizontal, Vertical };

// Aligne les boîtes englobantes sur celle de l'ensemble (au moins 2 objets).
[[nodiscard]] std::string batchAlign(const document::Project& project,
                                     const std::vector<std::uint64_t>& ids, AlignMode mode);
// Espaces égaux entre les boîtes, les deux extrêmes restant en place (au moins 3 objets).
[[nodiscard]] std::string batchDistribute(const document::Project& project,
                                          const std::vector<std::uint64_t>& ids,
                                          DistributeAxis axis);
[[nodiscard]] std::string batchGroup(const std::vector<std::uint64_t>& ids,
                                     const std::string& name);
// Dissout les groupes de premier niveau qui contiennent la sélection.
[[nodiscard]] std::string batchUngroup(const document::Project& project,
                                       const std::vector<std::uint64_t>& ids);

struct PlaceRequest {
    std::optional<double> xMm, yMm;          // centre de la boîte
    std::optional<double> widthMm, heightMm; // > 0
    double rotateDeg{0.0};                   // relative, anti-horaire
};
// Limites : |coord| <= 1000 mm, 0,1 <= dimension <= 2000 mm, |angle| <= 360°. Hors limites ou
// requête vide : chaîne vide.
[[nodiscard]] std::string batchPlace(const std::vector<std::uint64_t>& ids,
                                     const PlaceRequest& request);

} // namespace openstitch::desktop_v2
