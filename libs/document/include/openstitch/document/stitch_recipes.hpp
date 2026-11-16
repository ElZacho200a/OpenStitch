// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string_view>
#include <vector>

#include "openstitch/document/embroidery_object.hpp"

namespace openstitch::document {

// Recettes de points (STCH-CROS-01, ADR 0007) : catalogue intégré, en lecture seule, de
// réglages qui reproduisent un point de broderie à la main avec les types de points existants.
// Une recette s'applique par l'opération `apply_recipe` (`project_ops`, même conversion que
// `set_stitch_type`) : le document ne garde que les `StitchParams` obtenus, jamais l'identifiant.

// Objet auquel la recette est destinée (indication pour l'interface, non bloquante).
enum class RecipeTarget : std::uint8_t { Outline, Fill };

// `Faithful` : même séquence de pénétrations que le point à la main. `Approximation` : aspect
// voisin seulement (boucle, nœud ou fil glissé que la machine à point noué ne fait pas).
enum class RecipeFidelity : std::uint8_t { Faithful, Approximation };

struct StitchRecipe {
    std::string_view id; // stable : identifiant d'URL de la fiche source (CLI, tests)
    int version{1};      // incrémentée à chaque changement des paramètres produits
    std::string_view name_fr;
    std::string_view name_en;
    RecipeTarget target{RecipeTarget::Fill};
    RecipeFidelity fidelity{RecipeFidelity::Faithful};
    std::string_view source_url; // fiche d'origine (provenance seulement, aucun contenu copié)
    std::string_view note;       // limite ou conseil d'usage, en une phrase
    StitchParams params{RunningStitchParams{}};
};

// Ordre stable : contours puis remplissages, chacun du plus simple au plus élaboré.
[[nodiscard]] const std::vector<StitchRecipe>& stitch_recipes();
[[nodiscard]] const StitchRecipe* find_stitch_recipe(std::string_view id);
[[nodiscard]] std::string_view recipe_target_id(RecipeTarget target);
[[nodiscard]] std::string_view recipe_fidelity_id(RecipeFidelity fidelity);

} // namespace openstitch::document
