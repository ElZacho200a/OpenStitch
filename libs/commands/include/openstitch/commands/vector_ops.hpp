// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "openstitch/commands/command.hpp"
#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/document/object_fragment.hpp"
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
    Exclude,   // ce qui appartient à une seule forme (différence symétrique)
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
// `union_gap` (Union seulement) : écart maximal soudé entre deux formes juxtaposées ; 0 = union
// stricte. Les trous plus étroits que cet écart sont comblés.
[[nodiscard]] VectorOpResult make_boolean_command(document::Project& project, BooleanOp op,
                                                  const std::vector<ObjectId>& ids,
                                                  Micrometers union_gap = Micrometers{50});

// Découpe chaque objet de `ids` le long du segment (a, b) : seules les formes que le segment
// traverse de part en part sont coupées. `cut_width` : largeur de la bande retirée.
// Chaque morceau devient un objet : le plus grand garde l'identité d'origine, les autres sont
// de nouveaux objets, et leurs broderies de remplissage sont clonées (réglages identiques,
// retouches manuelles non copiées). Refusé si aucune forme n'est réellement traversée.
[[nodiscard]] VectorOpResult make_split_command(document::Project& project,
                                                const std::vector<ObjectId>& ids, Vec2um a,
                                                Vec2um b, Micrometers cut_width = Micrometers{20});

// Retire les trous des objets `ids` (HP-VEC-005) : tous si `max_area_um2` <= 0, sinon
// seulement ceux dont l'aire ne dépasse pas ce seuil (« combler les petits trous »). Une seule
// commande ; refusé si aucun trou n'est concerné. `resulting_objects` = nombre de trous retirés.
[[nodiscard]] VectorOpResult make_remove_holes_command(const document::Project& project,
                                                       const std::vector<ObjectId>& ids,
                                                       double max_area_um2 = 0.0);

// Comble les poches sans fil d'un bloc (`stitch_analysis::find_uncovered_pockets`) : chaque poche
// est rattachée au plus grand remplissage (tatami ou directionnel) de `ids` qui la touche, à
// défaut au plus grand objet voisin, tout type confondu. Un remplissage voisin qui est seul sur
// son vecteur, sans retouche manuelle ni lettrage, voit ce vecteur agrandi des poches qu'il
// absorbe (une seule zone de remplissage) ; sinon -- vecteur partagé, retouches à préserver, voisin
// non remplissant, poche déjà dans son vecteur -- les poches deviennent un NOUVEL objet vectoriel
// suivi d'un tatami (réglages du voisin si c'est un remplissage, tatami par défaut sinon), de la
// couleur du voisin. Une seule commande ; refusée si aucune poche n'est rattachable.
// `resulting_objects` = nombre de poches comblées.
[[nodiscard]] VectorOpResult
make_fill_pockets_command(document::Project& project, const std::vector<ObjectId>& ids,
                          const std::vector<geometry::PathSet>& pockets);

// Pot de peinture : crée un NOUVEL objet vectoriel qui remplit le vide fermé contenant `seed`,
// délimité par les objets vectoriels visibles du document (`geometry::fill_gap_at`). Les îlots
// enfermés restent des trous. Le nouvel objet (couleur neutre, nom « Vide rempli ») n'a pas
// encore de couture : on y applique ensuite satin, tatami, etc. Une seule commande annulable ;
// refusée (`error` montrable) si le point est sur une forme ou si le vide n'est pas fermé.
// `seal` : largeur de fente refermée, cf. `fill_gap_at` (`grow`).
[[nodiscard]] VectorOpResult make_fill_gap_command(document::Project& project, Vec2um seed,
                                                   Micrometers seal = Micrometers{0});

// Sépare les morceaux disjoints d'un objet en objets distincts (mêmes règles de clonage).
// Refusé si l'objet n'a qu'un morceau.
[[nodiscard]] VectorOpResult make_break_apart_command(document::Project& project, ObjectId id);

// --- Copier / coller (HP-OBJ-002) ---------------------------------------------
// Fragment à copier : les objets vectoriels `ids` (ordre du document, doublons et
// identifiants inconnus ignorés) et TOUTES les broderies qui en dépendent, dans leur
// ordre de couture. Ne modifie pas le document.
[[nodiscard]] document::ObjectFragment collect_fragment(const document::Project& project,
                                                        const std::vector<ObjectId>& ids);

struct PasteResult {
    std::unique_ptr<ICommand> command; // nulle si le fragment est vide
    std::vector<ObjectId> pasted;      // nouveaux objets vectoriels, dans l'ordre du fragment
};

// Colle `fragment` dans `project` en UNE commande annulable : nouveaux identifiants
// (tirés de `project.object_ids`), `source_vector` des broderies réaligné, décalage
// `delta` appliqué aux copies (formes, guides, rails de bordure, dégradés : même
// règle que `TranslateVectorObjectCommand`). Les copies ne sont plus des lettres
// d'un texte (`text_owner` retiré) ; `source_region` n'est gardé que si la région
// existe dans le projet cible. Les retouches manuelles de points sont conservées
// pour un collage sur place et retirées pour un collage décalé (leurs positions
// seraient fausses).
[[nodiscard]] PasteResult make_paste_command(document::Project& project,
                                             const document::ObjectFragment& fragment, Vec2um delta,
                                             const std::string& label = "Coller");

} // namespace openstitch::commands
