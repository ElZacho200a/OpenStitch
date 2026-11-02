// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "openstitch/commands/vector_ops.hpp"

namespace openstitch::commands {

// Nettoyage répétitif de formes (compagnon IA, mais utilisable par tout appelant) : chaque
// fabrique calcule la nouvelle géométrie MAINTENANT et renvoie UNE commande annulable (un seul
// pas d'historique pour toutes les formes touchées). Une forme verrouillée (elle-même ou par son
// calque) est ignorée. Refusé (`error` montrable) si rien ne change.

enum class ReshapeKind {
    Smooth,   // `geometry::smooth_path_set` : moins de nœuds, parties arrondies en Béziers
    Simplify, // `geometry::simplify` (Douglas-Peucker) : polygones à moins de sommets
};

// Lisse ou simplifie les contours (extérieur et trous) des objets `ids` avec l'écart maximal
// `tolerance` (µm, > 0). `resulting_objects` = nombre de formes modifiées.
[[nodiscard]] VectorOpResult make_reshape_paths_command(const document::Project& project,
                                                        const std::vector<ObjectId>& ids,
                                                        ReshapeKind kind, Micrometers tolerance);

// Retire les petits morceaux disjoints (artefacts, miettes) des objets `ids` : tout morceau dont
// l'aire est strictement inférieure à `min_area_um2` est supprimé. Un objet dont TOUS les morceaux
// seraient sous le seuil est laissé intact (on ne vide jamais une forme). `resulting_objects` =
// nombre de morceaux retirés.
[[nodiscard]] VectorOpResult make_remove_small_pieces_command(const document::Project& project,
                                                              const std::vector<ObjectId>& ids,
                                                              double min_area_um2);

} // namespace openstitch::commands
