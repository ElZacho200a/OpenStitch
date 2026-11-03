// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "openstitch/commands/command.hpp"
#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::commands {

// Transformation affine du plan appliquée à des objets : p' = pivot + M (p - pivot) + t.
// Repère du modèle (µm, Y vers le haut) : une rotation d'angle positif est anti-horaire.
struct AffineTransform {
    double m00{1.0}, m01{0.0}, m10{0.0}, m11{1.0}; // M
    Vec2um pivot{};
    Vec2um translation{};

    [[nodiscard]] static AffineTransform rotation(Vec2um pivot, Angle angle);
    // `horizontal` = retournement gauche/droite (axe vertical passant par le pivot) ;
    // sinon haut/bas (axe horizontal).
    [[nodiscard]] static AffineTransform mirror(Vec2um pivot, bool horizontal);
    [[nodiscard]] static AffineTransform scale(Vec2um pivot, double factor);
    // Échelle anisotrope (HP-OBJ-005) : facteur non fini ou <= 0 remplacé par 1.
    [[nodiscard]] static AffineTransform scale_xy(Vec2um pivot, double factor_x, double factor_y);
    [[nodiscard]] static AffineTransform translate(Vec2um delta);

    [[nodiscard]] Vec2um apply(Vec2um p) const;
    // Vecteur (tangente de Bézier) : partie linéaire seule.
    [[nodiscard]] Vec2um apply_vector(Vec2um v) const;
    // Direction modulo pi (orientation de rangées, guide absolu) dans [0 ; pi).
    [[nodiscard]] Angle apply_direction(Angle a) const;
    [[nodiscard]] bool flips() const { return m00 * m11 - m01 * m10 < 0.0; }
};

// Boîte englobante (nœuds des contours) des objets vectoriels `ids` ; nullopt si aucun
// n'existe ou n'a de nœud.
struct BoundsUm {
    Vec2um min{};
    Vec2um max{};
    [[nodiscard]] Vec2um center() const {
        return Vec2um{Micrometers{static_cast<std::int32_t>(
                          (static_cast<std::int64_t>(min.x.value) + max.x.value) / 2)},
                      Micrometers{static_cast<std::int32_t>(
                          (static_cast<std::int64_t>(min.y.value) + max.y.value) / 2)}};
    }
};
[[nodiscard]] std::optional<BoundsUm> objects_bounds(const document::Project& project,
                                                     const std::vector<ObjectId>& ids);

// Identifiants de TOUS les objets vectoriels du document (design entier), dans l'ordre.
[[nodiscard]] std::vector<ObjectId> all_object_ids(const document::Project& project);

// Transforme les objets vectoriels `ids` et tout ce qui en dépend, en UNE commande :
//  - contours (nœuds et tangentes de Bézier) ;
//  - tatami : orientation des rangées ;
//  - remplissage directionnel : guides, lignes de rupture, axe du dégradé ;
//  - satin (bordure ou manuel) : rails, barreaux, points d'entrée/sortie ;
//  - auto-satin : ancres et angles des guides, points d'entrée/sortie.
// Annulation EXACTE par instantané (contours et paramètres d'origine restaurés tels
// quels, pas une transformation inverse qui dériverait de quelques µm par l'arrondi).
// Les retouches manuelles de points ne sont pas transformées. Nulle si aucun objet de
// `ids` n'existe.
[[nodiscard]] std::unique_ptr<ICommand> make_transform_command(const document::Project& project,
                                                               const std::vector<ObjectId>& ids,
                                                               const AffineTransform& transform,
                                                               std::string label);

} // namespace openstitch::commands
