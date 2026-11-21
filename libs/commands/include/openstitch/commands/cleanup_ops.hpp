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

// --- Nettoyage de scene : miettes et interstices (sans creer de trou) ---------------------------

// Voisin qui recoit une miette, parmi les formes de surface qui la touchent (a `reach` pres).
enum class AbsorbPriority {
    SameThread,    // meme couleur de fil d'abord (plus longue frontiere), sinon `LongestBorder`
    LongestBorder, // la forme qui partage la plus longue frontiere avec la miette
    Largest,       // la plus grande forme voisine
};

// Fusionne chaque forme `crumbs` (formes vectorielles) dans une forme voisine au lieu de la
// supprimer : la miette, dilatee de `reach` pour combler la fente qui l'en separe (sans mordre sur
// les autres formes), est ajoutee au voisin choisi, puis la miette et ses broderies sont
// supprimees. Une miette sans surface (contour ouvert) est seulement supprimee ; une miette sans
// voisin a portee, verrouillee ou de lettrage reste telle quelle. Voisins : formes de surface
// visibles, non verrouillees, hors lettrage, hors trait et hors miettes. Une seule commande ;
// refusee si rien ne change. `resulting_objects` = nombre de miettes traitees.
[[nodiscard]] VectorOpResult
make_absorb_small_objects_command(const document::Project& project,
                                  const std::vector<ObjectId>& crumbs, AbsorbPriority priority,
                                  Micrometers reach = Micrometers{200});

// Rebouche les interstices ENTRE formes : vides fermes de l'union des formes de surface visibles,
// d'aire < `max_area_um2`, qui touchent (a `reach` pres) au moins DEUX formes. Un trou borde par
// une seule forme (l'interieur d'un « O ») est voulu : il reste. Chaque interstice rejoint la
// forme voisine editable qui en partage la plus longue frontiere. Une seule commande ; refusee si
// aucun interstice. `resulting_objects` = nombre d'interstices rebouches.
[[nodiscard]] VectorOpResult make_fill_gaps_between_command(const document::Project& project,
                                                            double max_area_um2,
                                                            Micrometers reach = Micrometers{200});

} // namespace openstitch::commands
