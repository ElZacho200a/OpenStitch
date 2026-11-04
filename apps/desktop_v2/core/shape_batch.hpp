// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- création de formes par lots `project_ops` (opération `add_shape`) : le même chemin
// que Claude et la CLI. Sans Qt. Repères : l'écran / l'aperçu est en mm « écran » (origine en haut
// à gauche de l'image, Y vers le bas) ; le document est en mm « modèle » (origine au centre du
// cadre, Y vers le haut). `screenToModel` est la seule conversion.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "openstitch/project_ops/operations.hpp"

namespace openstitch::desktop_v2 {

struct PointMm {
    double x{0};
    double y{0};
};

// Les mm « écran » du rendu sont ceux du modèle avec l'axe Y retourné.
[[nodiscard]] inline PointMm screenToModel(double xScreenMm, double yScreenMm) {
    return {xScreenMm, -yScreenMm};
}

// Limites de l'opération `add_shape` (catalogue) : |coord| <= 1000 mm, taille 0,1..2000 mm,
// 3..2000 sommets. Hors limites : chaîne vide (aucun lot), jamais de valeur « corrigée » en
// silence.
[[nodiscard]] std::string batchAddRect(PointMm centre, double widthMm, double heightMm,
                                       std::string_view name = {});
[[nodiscard]] std::string batchAddEllipse(PointMm centre, double widthMm, double heightMm,
                                          std::string_view name = {});
[[nodiscard]] std::string batchAddPolygon(const std::vector<PointMm>& points,
                                          std::string_view name = {});

// Identifiant de l'objet créé par la première opération du rapport (`{"created":[id,…]}`).
[[nodiscard]] std::optional<std::uint64_t> createdObjectId(const project_ops::BatchReport& report);

} // namespace openstitch::desktop_v2
