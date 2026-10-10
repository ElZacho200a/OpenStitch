// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "openstitch/commands/command.hpp"
#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::commands {

// Opérations booléennes et découpes sur les objets vectoriels (façon « Pathfinder »). Chaque
// fabrique calcule la géométrie MAINTENANT (les identifiants des nouveaux objets sont tirés de
// `project.object_ids`) et renvoie UNE commande annulable : un seul pas d'historique par geste.
// Aucune de ces fonctions ne modifie le contenu du document, seulement son compteur d'ids.
enum class BooleanOp {
    Union,     // somme des formes
    Subtract,  // la forme la plus basse (ordre du document) perd toutes les autres
    Intersect, // partie commune à toutes les formes
};

struct VectorOpResult {
    // Nulle si l'opération est impossible ; `error` dit pourquoi (montrable tel quel).
    std::unique_ptr<ICommand> command;
    std::string error;
    // Nombre d'objets vectoriels présents après l'opération parmi ceux qu'elle concerne.
    std::size_t resulting_objects{0};
};

// Union / soustraction / intersection d'au moins deux objets. Le résultat prend l'identité
// (et donc les objets de broderie) de l'objet RETENU : le dernier de `ids` (le principal) pour
// l'union et l'intersection, la forme la plus basse du document pour la soustraction. Les autres
// objets, et leurs broderies, sont supprimés. Résultat vide : refusé.
[[nodiscard]] VectorOpResult make_boolean_command(document::Project& project, BooleanOp op,
                                                  const std::vector<ObjectId>& ids);

// Découpe chaque objet de `ids` le long de la droite (a, b) (prolongée au-delà des formes).
// Chaque morceau devient un objet : le plus grand garde l'identité d'origine, les autres sont
// de nouveaux objets, et leurs broderies de remplissage sont clonées (réglages identiques,
// retouches manuelles non copiées). Refusé si aucune forme n'est réellement traversée.
[[nodiscard]] VectorOpResult make_split_command(document::Project& project,
                                                const std::vector<ObjectId>& ids, Vec2um a,
                                                Vec2um b);

// Sépare les morceaux disjoints d'un objet en objets distincts (mêmes règles de clonage).
// Refusé si l'objet n'a qu'un morceau.
[[nodiscard]] VectorOpResult make_break_apart_command(document::Project& project, ObjectId id);

} // namespace openstitch::commands
