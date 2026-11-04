// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- édition d'objets par lots `project_ops` : supprimer, dupliquer, pivoter, miroir,
// ordre d'empilement. Même chemin que Claude et la CLI ; une seule étape d'annulation pour toute la
// sélection. Sans Qt. Repère : mm « modèle » (voir `shape_batch.hpp`).

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "shape_batch.hpp"

namespace openstitch::desktop_v2 {

enum class MirrorAxis { Horizontal, Vertical };

// Hors limites du catalogue ou aucun objet : chaîne vide (rien à faire), jamais de valeur corrigée.
[[nodiscard]] std::string batchRemove(const std::vector<std::uint64_t>& ids);
// Copie décalée de `offset` (|coord| <= 1000 mm) ; l'original est conservé.
[[nodiscard]] std::string batchDuplicate(const std::vector<std::uint64_t>& ids, PointMm offset);
// Angle en degrés, sens antihoraire dans le repère modèle (Y vers le haut), [-360 ; 360].
[[nodiscard]] std::string batchRotate(const std::vector<std::uint64_t>& ids, double angleDeg,
                                      PointMm pivot);
// Miroir par rapport à l'axe passant par `pivot` : Horizontal = gauche/droite échangés.
[[nodiscard]] std::string batchMirror(const std::vector<std::uint64_t>& ids, MirrorAxis axis,
                                      PointMm pivot);
// `move` : "front" | "back" | "forward" | "backward" (ordre de couture, donc de recouvrement).
[[nodiscard]] std::string batchStack(const std::vector<std::uint64_t>& ids, std::string_view move);

// Identifiants créés par chaque opération du rapport (un par opération qui en crée un).
[[nodiscard]] std::vector<std::uint64_t> createdObjectIds(const project_ops::BatchReport& report);

} // namespace openstitch::desktop_v2
