// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include "openstitch/core/ids.hpp"

namespace openstitch::document {

// Calque du document (HP-OBJ-024) : un niveau de classement au-dessus des couches d'une forme.
// Un calque regroupe des formes (`VectorObject::calque`) ; chaque forme garde ses couches de
// couture (plusieurs `EmbroideryObject` de même `source_vector`). `Project::calques` est ordonné
// du fond (cousu en premier) vers le dessus : le rang du calque est la clé principale de l'ordre
// de couture, l'ordre de `Project::embroidery_objects` ne départage que les objets d'un même
// calque.
//
// Liste vide = un seul calque implicite (documents antérieurs, aucune migration). Une forme dont
// `calque` est invalide ou inconnu appartient au calque du fond.
struct Calque {
    ObjectId id;
    std::string name;
    bool visible{true}; // masqué : ses formes ne sont ni dessinées ni cousues
    bool locked{false}; // verrouillé : ses formes ne se sélectionnent ni ne s'éditent au canevas
};

} // namespace openstitch::document
