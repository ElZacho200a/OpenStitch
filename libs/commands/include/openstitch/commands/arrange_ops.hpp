// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "openstitch/commands/command.hpp"
#include "openstitch/commands/transform_ops.hpp"
#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::commands {

// Alignement et répartition des formes (HP-OBJ-008). Fonctions pures qui calculent le
// décalage de chaque forme ; `make_shift_command` les applique en UNE commande.

enum class AlignMode { Left, CenterHorizontal, Right, Top, CenterVertical, Bottom };

// Boîte de référence de l'alignement : celle de la sélection, celle de la première forme de
// `ids` (elle ne bouge pas), ou une boîte fournie (cadre de broderie).
enum class AlignReference { Selection, FirstObject, Frame };

struct ObjectShift {
    ObjectId id;
    Vec2um delta{};
};

// Décalages (non nuls seulement, ordre de `ids`) qui alignent chaque forme sur la référence.
// `frame` n'est lu que pour `AlignReference::Frame` (sans lui, retombe sur la sélection).
// Les formes sans nœud sont ignorées. Y vers le haut : « Top » = Y maximal.
[[nodiscard]] std::vector<ObjectShift> align_shifts(const document::Project& project,
                                                    const std::vector<ObjectId>& ids,
                                                    AlignMode mode, AlignReference reference,
                                                    const std::optional<BoundsUm>& frame = {});

// Répartit au moins trois formes pour que les ESPACES entre boîtes voisines soient égaux
// (les deux extrêmes ne bougent pas). Tri par centre sur l'axe, départage par identifiant ;
// reste entier : le reliquat d'arrondi va aux premiers espaces. Vide si < 3 formes.
[[nodiscard]] std::vector<ObjectShift> distribute_shifts(const document::Project& project,
                                                         const std::vector<ObjectId>& ids,
                                                         bool horizontal);

// Une seule commande (un pas d'annulation) qui translate chaque forme (broderie dépendante
// comprise, comme `TranslateVectorObjectCommand`). Nulle si `shifts` est vide.
[[nodiscard]] std::unique_ptr<ICommand> make_shift_command(const std::vector<ObjectShift>& shifts,
                                                           std::string label);

// Verrou d'édition des formes `ids` (HP-OBJ-010) : une seule commande, l'état précédent de
// chaque forme est restitué exactement à l'annulation. Nulle si aucune forme n'existe ou si
// toutes sont déjà dans l'état demandé.
[[nodiscard]] std::unique_ptr<ICommand> make_shape_lock_command(const document::Project& project,
                                                                const std::vector<ObjectId>& ids,
                                                                bool locked);

} // namespace openstitch::commands
