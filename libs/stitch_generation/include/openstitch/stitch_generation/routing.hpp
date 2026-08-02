// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"

namespace openstitch::stitch_generation {

// Une colonne satin réduite à ses deux extrémités (centres des barreaux
// d'about), pour le routage (§13). L'ordre et l'orientation de couture sont
// choisis d'abord pour conserver le maximum de jonctions explicites valides,
// puis pour minimiser les déplacements ; la géométrie reste inchangée.
struct RouteColumn {
    ObjectId id{};
    Vec2um start{};  // extrémité « début » naturelle
    Vec2um end{};    // extrémité « fin » naturelle
    std::optional<std::uint32_t> start_junction;
    std::optional<std::uint32_t> end_junction;
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
    // Jonction logique commune à la sortie précédente et à cette entrée. Elle
    // n'est retenue que si l'écart géométrique reste sous `underpath_max`.
    std::optional<std::uint32_t> junction;
};

struct RoutePlan {
    std::vector<RouteStep> steps;
    double travel_um{0.0};    // somme des déplacements entre colonnes
    std::size_t jumps{0};     // liaisons de type Jump (coupes)
    std::size_t underpaths{0};  // liaisons cachées
    std::size_t junction_links{0};  // liaisons validées par une jonction commune
};

struct RoutingConfig {
    // Au-delà de ce seuil, la liaison devient un saut (coupe) plutôt qu'un
    // trajet caché : un trajet trop long traverserait la broderie à découvert.
    // S'applique quand la liaison est VALIDÉE par une jonction commune
    // (`RouteStep::junction` a une valeur) : la jonction garantit que les deux
    // colonnes appartiennent au même réseau topologique, donc que l'espace
    // entre elles est bien couvert par le tissu (§ ancrage des jonctions,
    // auto_satin — les rails y convergent désormais exactement).
    Micrometers underpath_max{8'000};
    // Sans jonction commune validée, `route_columns` n'a AUCUNE garantie que
    // l'espace entre deux colonnes est réellement du tissu : une simple
    // proximité géométrique peut être fortuite (deux lettres rapprochées, une
    // forme en C étroite dont les deux bouts se frôlent sans être reliés).
    // Un trajet caché y est donc borné beaucoup plus strictement — seulement
    // les cas de quasi-contact (arrondi de rastérisation, extrémités
    // coïncidentes) — tout le reste devient un saut plutôt qu'un fil cousu à
    // travers du vide. Défaut trouvé par revue : avant, `underpath_max` seul
    // décidait, sans jamais regarder si la liaison était justifiée par une
    // jonction — un trajet caché de 5 à 8 mm entre deux formes sans aucun
    // lien topologique était accepté silencieusement.
    Micrometers underpath_max_without_junction{1'500};
    bool two_opt{true};  // amélioration 2-opt de l'ordre après le glouton
};

// Ordonne et oriente les colonnes pour conserver les jonctions explicites puis
// minimiser le déplacement total à partir de `origin`. Une jonction éloignée
// de plus de `underpath_max` est ignorée comme incohérente. Glouton plus proche voisin
// (entrée par l'extrémité la plus proche), puis 2-opt ; l'orientation optimale
// d'un ordre donné est résolue exactement par programmation dynamique sur les
// deux extrémités. Déterministe. Vecteur vide -> plan vide.
//
// Un trajet caché (`ConnectorKind::Underpath`) n'est jamais accordé sur la
// seule proximité géométrique : il exige soit une jonction commune validée
// (borne `underpath_max`), soit un quasi-contact (borne, bien plus stricte,
// `underpath_max_without_junction`) — au-delà, la liaison devient un saut,
// jamais un fil cousu à travers un espace dont rien ne garantit qu'il est
// couvert de tissu.
[[nodiscard]] RoutePlan route_columns(const std::vector<RouteColumn>& columns, Vec2um origin,
                                      const RoutingConfig& config);

}  // namespace openstitch::stitch_generation
