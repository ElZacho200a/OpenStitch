// SPDX-License-Identifier: Apache-2.0
#pragma once

// Position, taille et rotation numériques d'une sélection (HP-OBJ-015, barre de propriétés).
// Tout passe par `make_transform_command` : paramètres dépendants transformés, annulation exacte,
// UNE commande par validation. Repère du modèle : µm, Y vers le haut ; la position est celle du
// CENTRE de la boîte englobante de la sélection.

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include "openstitch/commands/transform_ops.hpp"

namespace openstitch::commands {

// Géométrie courante de la sélection : centre et dimensions de la boîte englobante, en µm.
struct SelectionGeometry {
    Vec2um center{};
    Micrometers width{};
    Micrometers height{};
};

[[nodiscard]] inline std::optional<SelectionGeometry>
selection_geometry(const document::Project& project, const std::vector<ObjectId>& ids) {
    const auto box = objects_bounds(project, ids);
    if (!box) {
        return std::nullopt;
    }
    return SelectionGeometry{box->center(),
                             Micrometers{static_cast<std::int32_t>(
                                 static_cast<std::int64_t>(box->max.x.value) - box->min.x.value)},
                             Micrometers{static_cast<std::int32_t>(
                                 static_cast<std::int64_t>(box->max.y.value) - box->min.y.value)}};
}

// Amène la boîte englobante de la sélection au centre / à la taille demandés. Un champ vide laisse
// la valeur actuelle ; une dimension actuelle nulle (ligne) ne peut pas être mise à l'échelle et
// reste inchangée. Dimensions bornées à >= 1 µm. Nulle si rien ne change ou si la sélection est
// vide.
[[nodiscard]] inline std::unique_ptr<ICommand>
make_place_command(const document::Project& project, const std::vector<ObjectId>& ids,
                   std::optional<Micrometers> centerX, std::optional<Micrometers> centerY,
                   std::optional<Micrometers> width, std::optional<Micrometers> height,
                   std::string label) {
    const auto box = objects_bounds(project, ids);
    if (!box) {
        return nullptr;
    }
    const double curW = static_cast<double>(box->max.x.value) - box->min.x.value;
    const double curH = static_cast<double>(box->max.y.value) - box->min.y.value;
    const double newW = width && curW > 0.0 ? std::max(1.0, double(width->value)) : curW;
    const double newH = height && curH > 0.0 ? std::max(1.0, double(height->value)) : curH;
    const Vec2um center = box->center();
    const double newCx = centerX ? double(centerX->value) : double(center.x.value);
    const double newCy = centerY ? double(centerY->value) : double(center.y.value);
    AffineTransform t;
    t.m00 = curW > 0.0 ? newW / curW : 1.0;
    t.m11 = curH > 0.0 ? newH / curH : 1.0;
    // p' = pivot + M (p - pivot) + translation, pivot = centre actuel : le centre y reste fixe,
    // la translation l'amène à la position demandée.
    t.pivot = center;
    t.translation =
        Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(newCx - center.x.value))},
               Micrometers{static_cast<std::int32_t>(std::lround(newCy - center.y.value))}};
    if (t.m00 == 1.0 && t.m11 == 1.0 && t.translation.x.value == 0 && t.translation.y.value == 0) {
        return nullptr;
    }
    return make_transform_command(project, ids, t, std::move(label));
}

// Fait pivoter la sélection de `degrees` (positif : sens anti-horaire) autour du centre de sa
// boîte. Nulle si l'angle est nul ou la sélection vide.
[[nodiscard]] inline std::unique_ptr<ICommand>
make_rotate_about_center_command(const document::Project& project, const std::vector<ObjectId>& ids,
                                 double degrees, std::string label) {
    const auto box = objects_bounds(project, ids);
    if (!box || degrees == 0.0 || !std::isfinite(degrees)) {
        return nullptr;
    }
    return make_transform_command(
        project, ids,
        AffineTransform::rotation(box->center(), Angle{degrees * std::numbers::pi / 180.0}),
        std::move(label));
}

} // namespace openstitch::commands
