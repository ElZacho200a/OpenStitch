// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- déplacement et mise à l'échelle par lots `project_ops` (opérations `translate` et
// `scale`) : même chemin que Claude et la CLI, une seule étape d'annulation pour toute la sélection.
// Sans Qt. Repère : mm « modèle » (origine au centre du cadre, Y vers le haut), comme `shape_batch`.

#include <cstdint>
#include <string>
#include <vector>

#include "shape_batch.hpp"

namespace openstitch::desktop_v2 {

// Limites du catalogue : |d| <= 2000 mm ; facteur 0,05..20 ; pivot |coord| <= 1000 mm. Hors limites,
// ou aucun objet, ou déplacement nul : chaîne vide (rien à faire), jamais de valeur « corrigée ».
[[nodiscard]] std::string batchTranslate(const std::vector<std::uint64_t>& ids, double dxMm,
                                         double dyMm);
[[nodiscard]] std::string batchScale(const std::vector<std::uint64_t>& ids, double factor,
                                     PointMm pivot);

} // namespace openstitch::desktop_v2
