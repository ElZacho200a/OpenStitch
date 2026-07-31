// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <vector>

#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"

namespace openstitch::stitch_generation {

// Une colonne satin réduite à ses deux extrémités (centres des barreaux
// d'about), pour le routage (§13). L'ordre et l'orientation de couture sont
// choisis pour minimiser les déplacements ; la géométrie reste inchangée.
struct RouteColumn {
    ObjectId id{};
    Vec2um start{};  // extrémité « début » naturelle
    Vec2um end{};    // extrémité « fin » naturelle
};

// Nature d'une liaison entre deux colonnes consécutives.
enum class ConnectorKind {
    Start,      // première colonne : simple saut depuis l'origine (pose du fil)
    Underpath,  // trajet cousu caché (pas de coupe) : la sortie rejoint l'entrée
    Jump,       // saut avec coupe : liaison trop longue pour être cachée
};

// Une étape du plan : quelle colonne, dans quel sens, atteinte par quel type
// de liaison.
struct RouteStep {
    std::size_t column_index{0};  // index dans le vecteur d'entrée
    bool reversed{false};         // true = coudre end -> start
    ConnectorKind connector{ConnectorKind::Start};
};

struct RoutePlan {
    std::vector<RouteStep> steps;
    double travel_um{0.0};    // somme des déplacements entre colonnes
    std::size_t jumps{0};     // liaisons de type Jump (coupes)
    std::size_t underpaths{0};  // liaisons cachées
};

struct RoutingConfig {
    // Au-delà de ce seuil, la liaison devient un saut (coupe) plutôt qu'un
    // trajet caché : un trajet trop long traverserait la broderie à découvert.
    Micrometers underpath_max{8'000};
    bool two_opt{true};  // amélioration 2-opt de l'ordre après le glouton
};

// Ordonne et oriente les colonnes pour minimiser le déplacement total à partir
// de `origin` (position courante de l'aiguille). Glouton plus proche voisin
// (entrée par l'extrémité la plus proche), puis 2-opt ; l'orientation optimale
// d'un ordre donné est résolue exactement par programmation dynamique sur les
// deux extrémités. Déterministe. Vecteur vide -> plan vide.
[[nodiscard]] RoutePlan route_columns(const std::vector<RouteColumn>& columns, Vec2um origin,
                                      const RoutingConfig& config);

}  // namespace openstitch::stitch_generation
