// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "openstitch/document/project.hpp"
#include "openstitch/stitch_analysis/analyze.hpp"

namespace openstitch::stitch_analysis {

// Écart CIEDE2000 en dessous duquel un fil se confond avec le tissu (HP-THR-008). Seuil indicatif :
// ~10 = deux teintes que l'oeil distingue à peine sur un aplat brodé.
inline constexpr double kMinThreadFabricContrast = 10.0;

// Écart perceptuel (CIEDE2000) entre un fil et le tissu.
[[nodiscard]] double thread_fabric_distance(std::array<std::uint8_t, 3> thread,
                                            std::array<std::uint8_t, 3> fabric);

// HP-THR-008 : un constat par objet de broderie visible dont la couleur est trop proche du tissu
// du projet (catégorie « fil-couleur-tissu », avertissement, objet renseigné, ordre du document).
// Tissu laissé au défaut (blanc, sans nom) : aucun contrôle (les documents existants ne changent
// pas). Lit l'intention (`Project::embroidery_objects`, `Project::fabric`), pas la séquence.
[[nodiscard]] std::vector<Finding> check_thread_vs_fabric(const document::Project& project);

} // namespace openstitch::stitch_analysis
