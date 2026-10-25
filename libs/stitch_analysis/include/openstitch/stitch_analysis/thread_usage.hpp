// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "openstitch/core/ids.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/stitch/sequence.hpp"
#include "openstitch/thread_palette/thread.hpp"
#include "openstitch/thread_palette/thread_library.hpp"

namespace openstitch::stitch_analysis {

// =============================================================================
// API « fils du design » (HP-THR-004/005) -- Qt-free, déterministe.
//
// Consommée par le panneau Fils / film couleur de l'application ET par la
// future fiche de production imprimable (HP-PROD-*) : UNE seule dérivation des
// fils utilisés, jamais une seconde logique « couleurs du design ».
//
// Entrée : le projet et la séquence EFFECTIVE (`stitch_generation::
// effective_sequence(project)`), jamais la séquence brute. Aucune E/S.
// =============================================================================

// Hypothèses d'estimation du temps (machine mono-tête typique).
struct ThreadUsageOptions {
    double stitches_per_minute{700.0}; // vitesse de couture
    double color_change_seconds{30.0}; // changement de bobine / de fil (par bloc après le premier)
    double trim_seconds{3.0};          // une coupe de fil
};

// Identité d'un fil dans le design : le fil de nuancier s'il est assigné, sinon
// la couleur libre. Deux objets partagent un fil si leurs `key` sont égales ;
// deux objets SANS fil le partagent si leur RGB est identique.
struct ThreadIdentity {
    std::optional<thread_palette::ThreadKey> key;
    std::array<std::uint8_t, 3> rgb{};

    bool operator==(const ThreadIdentity&) const = default;
};

// Un fil utilisé par le design, avec ses mesures.
struct ThreadUsage {
    ThreadIdentity identity;
    // Métadonnées du fil, résolues dans la bibliothèque fournie (vides si le fil
    // est une couleur libre ou si son nuancier n'est pas chargé : le RGB reste
    // exact, seul le nom manque).
    std::string brand;
    std::string code; // référence fabricant (ou copie de key->code)
    std::string name;
    std::vector<ObjectId> objects; // objets cousus avec ce fil, dans l'ordre de couture
    std::size_t stitch_count{0};   // piqûres (commandes Stitch, toutes passes)
    double length_mm{0.0};         // longueur de fil cousue (piqûre à piqûre, sauts exclus)
    std::size_t color_blocks{0};   // nombre de passages (blocs de couleur) de ce fil
    double estimated_seconds{0.0}; // coutures + changements + coupes
};

// Fils utilisés, dans l'ordre de PREMIÈRE utilisation dans la séquence. Les
// commandes sans objet source connu (design importé) sont regroupées sous une
// couleur libre noire, comme `color_blocks`. `library` (facultative) sert à
// renseigner brand/code/name d'un fil assigné.
[[nodiscard]] std::vector<ThreadUsage>
thread_usage(const document::Project& project, const stitch::StitchSequence& sequence,
             const thread_palette::ThreadLibrary* library = nullptr,
             const ThreadUsageOptions& options = {});

// Libellé d'affichage d'un fil : « Marque Réf — Nom », « Réf — Nom » ou, pour
// une couleur libre, « #RRGGBB ».
[[nodiscard]] std::string thread_label(const ThreadUsage& usage);

// Export tableur de la liste des fils (UTF-8, séparateur `;`, nombres au point
// décimal, une ligne par fil, en-tête fixe) :
// ordre;marque;nuancier;reference;nom;couleur;objets;points;longueur_mm;duree_s
[[nodiscard]] std::string thread_usage_csv(const std::vector<ThreadUsage>& usage);

// Objets de broderie qui utilisent exactement ce fil (ordre du document) :
// `key` renseigné -> objets portant ce fil ; sinon objets SANS fil de cette
// couleur. Sert à « sélectionner les objets de ce fil » et « remplacer ce fil
// par… ».
[[nodiscard]] std::vector<ObjectId> objects_using_thread(const document::Project& project,
                                                         const ThreadIdentity& identity);

// ----------------------------- film couleur ---------------------------------

// Un bloc du film couleur : un passage d'un fil, entre deux changements de
// couleur de la séquence.
struct ColorFilmBlock {
    ThreadIdentity identity;
    std::vector<ObjectId> objects; // objets distincts du bloc, dans l'ordre de couture
    std::size_t stitch_count{0};
    double length_mm{0.0};
    bool locked{false}; // au moins un objet du bloc a un ordre figé
};

// Blocs de couleur dans l'ordre de couture. Un bloc vide est omis. L'identité
// vient de l'objet source de la première commande du bloc.
[[nodiscard]] std::vector<ColorFilmBlock> color_film(const document::Project& project,
                                                     const stitch::StitchSequence& sequence);

// Nouvel ordre des objets de broderie (liste complète d'ObjectId, à passer à
// `ReorderEmbroideryCommand`) après avoir déplacé le bloc `from` à la position
// `to` du film. Les objets FIGÉS (`locked`) et les objets absents du film
// (masqués, sans point) gardent leur place absolue dans l'ordre du document ;
// seuls les objets libres du film sont redistribués dans les emplacements
// libres, dans l'ordre voulu. `nullopt` si `from`/`to` sont hors du film.
[[nodiscard]] std::optional<std::vector<ObjectId>>
reorder_film_blocks(const document::Project& project, const std::vector<ColorFilmBlock>& film,
                    std::size_t from, std::size_t to);

// Nouvel ordre des objets après avoir déplacé la COULEUR (le fil, tous ses blocs) `from`
// à la position `to` de la liste des fils distincts, dans l'ordre de première utilisation
// du film (le même que `thread_usage`). Tous les objets de cette couleur passent d'un bloc :
// la couleur est cousue après (donc au-dessus de) les couleurs qu'elle dépasse. Les autres
// couleurs gardent leur ordre relatif ; chaque couleur garde l'ordre de ses objets. Mêmes
// règles de verrouillage que `reorder_film_blocks`. `nullopt` si `from`/`to` sont hors liste.
[[nodiscard]] std::optional<std::vector<ObjectId>>
reorder_threads(const document::Project& project, const std::vector<ColorFilmBlock>& film,
                std::size_t from, std::size_t to);

// Nouvel ordre qui regroupe les blocs de MÊME fil (identité égale) à la place
// de leur première apparition, pour réduire les changements de fil. Même
// règles de verrouillage que `reorder_film_blocks`. ATTENTION : fusionner
// change l'empilement des couches (un fil cousu plus tard passe plus tôt) ;
// l'appelant avertit l'utilisateur et s'appuie sur l'annulation.
[[nodiscard]] std::vector<ObjectId>
merge_same_thread_blocks(const document::Project& project, const std::vector<ColorFilmBlock>& film);

// Séquencer par couleur (HP-SEQ-003) : regroupe tous les objets d'un même fil en un seul bloc
// et range les fils selon `thread_order` (identités listées d'abord, dans cet ordre ; les fils
// non listés suivent dans l'ordre de première apparition du film ; les identités absentes du
// film sont ignorées). Avec `thread_order` vide, équivaut à `merge_same_thread_blocks`. Même
// règles de verrouillage que `reorder_film_blocks` ; l'appelant avertit que l'empilement des
// couches change.
[[nodiscard]] std::vector<ObjectId>
sequence_by_color(const document::Project& project, const std::vector<ColorFilmBlock>& film,
                  const std::vector<ThreadIdentity>& thread_order = {});

// Visibilité des fils (HP-THR-005, masquage depuis le film). Contrairement au film, calculée
// sur TOUS les objets de broderie du document, y compris les objets masqués (absents de la
// séquence effective) : un fil entièrement masqué reste donc listé et peut être réaffiché.
struct ThreadVisibility {
    ThreadIdentity identity;
    std::vector<ObjectId> objects; // tous les objets de ce fil, ordre du document
    std::size_t hidden{0};         // dont masqués

    [[nodiscard]] bool fully_hidden() const { return !objects.empty() && hidden == objects.size(); }
    [[nodiscard]] bool partly_hidden() const { return hidden > 0 && hidden < objects.size(); }
};

// Fils du document dans l'ordre de première apparition des objets. Déterministe.
[[nodiscard]] std::vector<ThreadVisibility> thread_visibility(const document::Project& project);

// Nombre de changements de fil qu'impose un film (blocs - 1, plancher 0).
[[nodiscard]] std::size_t color_change_count(const std::vector<ColorFilmBlock>& film);

} // namespace openstitch::stitch_analysis
