// SPDX-License-Identifier: Apache-2.0
#pragma once

// Requêtes de sélection (ergonomie sélection/filtres) : fonctions PURES sur le projet, sans Qt ni
// MainWindow, qui calculent une liste de formes à sélectionner. MainWindow n'ajoute que le câblage
// (action de menu, `setSelection`, message d'état). `selectable` est le prédicat de sélection
// de l'interface (visible, filtres d'affichage, verrous) : une requête ne renvoie jamais une forme
// que le clic n'aurait pas pu saisir. Le DERNIER identifiant renvoyé est la forme principale.

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::desktop {

using ShapePredicate = std::function<bool(const document::VectorObject&)>;

// Boîte englobante des nœuds d'une forme (µm) ; `valid` faux pour une forme sans nœud.
struct ShapeBox {
    bool valid{false};
    Vec2um min{};
    Vec2um max{};
};
[[nodiscard]] ShapeBox shape_box(const document::VectorObject& shape);

// Broderie de la forme (première couche), ou nullptr si elle n'est pas brodée.
[[nodiscard]] const document::EmbroideryObject*
embroidery_of_shape(const document::Project& project, ObjectId shape);

// Formes sélectionnables NON sélectionnées, dans l'ordre du document.
[[nodiscard]] std::vector<ObjectId> invert_selection(const document::Project& project,
                                                     const std::vector<ObjectId>& selected,
                                                     const ShapePredicate& selectable);

// Formes de même famille de point que `active` (tatami, satin, contour…) ; `active` en dernier.
[[nodiscard]] std::vector<ObjectId> select_same_stitch_type(const document::Project& project,
                                                            ObjectId active,
                                                            const ShapePredicate& selectable);

// Formes de même fil : même référence de nuancier si `active` en a une, sinon même couleur libre.
[[nodiscard]] std::vector<ObjectId> select_same_thread(const document::Project& project,
                                                       ObjectId active,
                                                       const ShapePredicate& selectable);

// Formes du même calque que `active`.
[[nodiscard]] std::vector<ObjectId> select_same_calque(const document::Project& project,
                                                       ObjectId active,
                                                       const ShapePredicate& selectable);

// Formes dont une broderie est « verrouillée d'ordre » ou masquée, pour les retrouver dans la
// liste.
enum class StateKind { OrderLocked, HiddenEmbroidery, EditLocked, NoEmbroidery };
[[nodiscard]] std::vector<ObjectId> select_by_state(const document::Project& project,
                                                    StateKind kind);

// Forme voisine dans l'ordre de couture (`step` = +1 suivante, -1 précédente), en boucle. Part de
// `current` (une broderie de la forme) ; sans courante, +1 donne la première et -1 la dernière.
[[nodiscard]] std::optional<ObjectId> step_in_sewing_order(const document::Project& project,
                                                           std::optional<ObjectId> current,
                                                           int step,
                                                           const ShapePredicate& selectable);

// Rang de couture (à partir de 1) d'une forme brodée ; 0 si elle n'est pas brodée.
[[nodiscard]] std::size_t sewing_rank_of_shape(const document::Project& project, ObjectId shape);

// Formes brodées dont le rang de couture est dans [first, last] (à partir de 1, ordre libre).
[[nodiscard]] std::vector<ObjectId> select_sewing_range(const document::Project& project,
                                                        std::size_t first, std::size_t last,
                                                        const ShapePredicate& selectable);

// Formes (hors `selected`) dont la boîte englobante touche celle de la sélection, élargie de
// `marginUm` : « ce qui se chevauche ».
[[nodiscard]] std::vector<ObjectId> select_touching(const document::Project& project,
                                                    const std::vector<ObjectId>& selected,
                                                    Micrometers margin,
                                                    const ShapePredicate& selectable);

// Lasso : formes dont le centre de la boîte englobante est dans le polygone (règle pair-impair).
[[nodiscard]] std::vector<ObjectId> select_in_lasso(const document::Project& project,
                                                    const std::vector<Vec2um>& polygon,
                                                    const ShapePredicate& selectable);

} // namespace openstitch::desktop
