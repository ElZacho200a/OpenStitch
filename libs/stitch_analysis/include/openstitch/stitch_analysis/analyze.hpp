// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::stitch_analysis {

enum class Severity { Info, Warning, Error };

struct Finding {
    Severity severity{Severity::Warning};
    std::string category; // slug court, ex. "point-court"
    std::string message;  // phrase montrable à l'utilisateur
    Vec2um location{};    // où, sur le canevas
    ObjectId object{};    // objet concerné (0 = global)
    std::string hint;     // piste de correction (vide = aucune)
};

struct AnalysisOptions {
    Micrometers min_stitch{500};   // en dessous : point trop court (0,5 mm)
    Micrometers max_stitch{7'000}; // au-dessus : point trop long (7 mm)
    Micrometers max_jump{30'000};  // saut trop long (30 mm)
    // Déplacement plus long que ce seuil sans coupe (Lot G) : le fil traîne
    // sur le tissu. Même seuil par défaut que les coupes automatiques.
    Micrometers trim_threshold{3'000};
    std::size_t max_stitches{100'000};
    std::optional<stitch::BoundsUm> hoop;      // cadre : hors limites = erreur
    std::size_t max_findings_per_category{50}; // anti-inondation
    // Épaisseur de fil, en « couches de remplissage dense » (longueur de fil x
    // largeur du fil / surface ; un remplissage dont l'écart égale la largeur du
    // fil vaut 1, une sous-couche espacée de 2 mm ~0,2). Au-delà de ce seuil, le
    // tissu s'épaissit et se déforme (Liu et al., CGF 2023, ne superposent que 2
    // couches) : 2 remplissages + leurs sous-couches font ~2,4 (bords ~2,6),
    // trois ~3,6 ; seuil 3,0. 0 = règle désactivée.
    double max_layer_thickness{3.0};
    Micrometers thread_width{400}; // largeur du fil retenue pour la mesure (0,4 mm)
    // Grille de mesure : 2,5 mm. Une zone n'est signalée que si elle contient un
    // bloc de 2 x 2 cases (5 mm) en excès : ni le bord d'un objet, ni des objets
    // simplement voisins ne comptent.
    Micrometers layer_cell{2'500};
};

// Analyse une séquence et renvoie les problèmes détectés, du plus grave au
// moins grave (§15). Ne modifie rien ; les corrections restent manuelles.
[[nodiscard]] std::vector<Finding> analyze(const stitch::StitchSequence& sequence,
                                           const AnalysisOptions& options = {});

// Résultat détaillé : en plus des problèmes retenus, le nombre de problèmes
// masqués par le plafond `max_findings_per_category` pour chaque catégorie
// (clé absente = rien de masqué), afin que l'IHM puisse écrire « … et N autres ».
struct AnalysisReport {
    std::vector<Finding> findings;
    std::map<std::string, std::size_t> suppressed;
};
[[nodiscard]] AnalysisReport analyze_detailed(const stitch::StitchSequence& sequence,
                                              const AnalysisOptions& options = {});

// HP-ENG-008 : options d'analyse alignées sur les réglages du projet (longueurs minimale et
// maximale de point, seuil de coupe), pour que l'analyse juge avec les mêmes limites que
// celles que la génération applique (`SequenceFinishing`). Projet aux finitions
// désactivées (ancien .osp) : options par défaut inchangées.
[[nodiscard]] AnalysisOptions options_from_project(const document::Project& project);

// HP-ENG-008 : avertissements AVANT export portant sur les RÉGLAGES des objets (longueur
// de point demandée hors des limites du projet), sans générer les points : un objet
// réglé à 9 mm produira des points trop longs, ce que l'on peut dire d'avance.
// Catégorie « parametre-hors-limites », objet concerné renseigné, ordre du document.
[[nodiscard]] std::vector<Finding> check_stitch_limits(const document::Project& project);

// Nombre de millimètres formaté à la française, une décimale, sans dépendre
// de la locale : 3500 µm -> « 3,5 ». Troncature vers zéro comme avant.
[[nodiscard]] std::string format_mm_fr(double micrometers);

} // namespace openstitch::stitch_analysis
