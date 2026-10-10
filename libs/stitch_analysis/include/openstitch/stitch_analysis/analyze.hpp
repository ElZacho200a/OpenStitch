// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"
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

// Nombre de millimètres formaté à la française, une décimale, sans dépendre
// de la locale : 3500 µm -> « 3,5 ». Troncature vers zéro comme avant.
[[nodiscard]] std::string format_mm_fr(double micrometers);

} // namespace openstitch::stitch_analysis
