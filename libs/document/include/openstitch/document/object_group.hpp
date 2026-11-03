// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include "openstitch/core/ids.hpp"

namespace openstitch::document {

// Groupe d'objets de l'utilisateur (HP-OBJ-009). Un groupe réunit des formes
// (`VectorObject::group`) et/ou d'autres groupes (`parent`) : la sélection et les
// transformations le traitent comme un seul objet. Grouper ne change JAMAIS l'ordre de couture
// (les objets de broderie gardent leur rang dans `Project::embroidery_objects`).
//
// `parent` invalide = groupe de premier niveau. Un identifiant de groupe inconnu (forme dont le
// groupe a été supprimé) est lu comme « aucun groupe » : aucune migration nécessaire.
struct ObjectGroup {
    ObjectId id;
    std::string name;
    ObjectId parent;
};

} // namespace openstitch::document
