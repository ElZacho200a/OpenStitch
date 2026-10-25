// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"

namespace openstitch::optimization {

// Un objet à ordonner, réduit à ce qui compte pour le coût de couture.
struct OrderItem {
    ObjectId id;
    std::array<std::uint8_t, 3> rgb{};
    Vec2um centroid{};  // position représentative (déplacements estimés par les centres)
    bool locked{false}; // reste à sa position d'origine
    // Aire de la zone brodée (mm²) : n'influence que `LayeredColorThenProximity`.
    double area_mm2{0.0};
    // HP-ENG-010 -- extrémités de couture de l'objet (début / fin). Absentes : le centre sert
    // de repli (comportement historique). `reversible` : l'objet peut être cousu dans l'autre
    // sens (entrée et sortie échangées) ; `reversed` est renseigné en sortie de `plan_order`.
    std::optional<Vec2um> entry{};
    std::optional<Vec2um> exit{};
    bool reversible{false};
    bool reversed{false};
};

// Début / fin effectifs d'un objet (extrémité déclarée, sinon centre).
[[nodiscard]] Vec2um item_entry(const OrderItem& item);
[[nodiscard]] Vec2um item_exit(const OrderItem& item);

enum class OrderStrategy {
    Document,           // ordre actuel (aucun changement)
    ByColor,            // regroupe les couleurs (minimise les changements de fil)
    ByProximity,        // plus proche voisin (minimise les déplacements)
    ColorThenProximity, // groupes de couleur, puis proximité à l'intérieur
    // Lot C (audit marine plein cadre) : couches. Un remplissage qui déborde
    // sur ses voisins doit être cousu SOUS eux : les couleurs sont ordonnées
    // par aire de leur plus grande zone (fond d'abord, détails ensuite) ; à
    // couleur égale, les grandes zones par aire décroissante, puis les
    // petites par proximité. Le regroupement par couleur est conservé.
    LayeredColorThenProximity
};

struct OrderOptions {
    // `LayeredColorThenProximity` : une zone est « grande » (cousue en
    // premier, par aire décroissante) si son aire atteint cette fraction de
    // la plus grande zone de sa couleur.
    double layer_large_area_ratio{0.25};

    // HP-SEQ-001 -- amélioration locale (2-opt + Or-opt + inversion de sens) appliquée APRES la
    // stratégie. Borne DETERMINISTE : nombre maximal de candidats évalués (jamais un temps mur).
    // 0 : désactivée, résultat strictement identique à l'historique. Les objets verrouillés
    // gardent leur emplacement ; seuls les libres sont déplacés / retournés (si `reversible`).
    std::size_t improve_budget{0};

    // HP-SEQ-008 -- points de départ et d'arrivée du design. Quand ils sont renseignés, le trajet
    // départ -> premier objet et dernier objet -> arrivée entre dans le coût de l'amélioration, et
    // `ByProximity` / les chaînes de proximité démarrent au plus près du départ.
    std::optional<Vec2um> start{};
    std::optional<Vec2um> end{};
};

// Budget par défaut raisonnable pour l'IHM / les opérations (évaluations de candidats).
inline constexpr std::size_t kDefaultImproveBudget = 20'000;

struct OrderCost {
    double travel_um{0.0}; // somme des déplacements fin(i-1) -> début(i) (centres par défaut)
    std::size_t color_changes{0}; // nombre de changements de fil

    // Coût scalaire : les changements de couleur sont chers (arrêt machine).
    [[nodiscard]] double score() const {
        return travel_um + static_cast<double>(color_changes) * 50'000.0;
    }
};

[[nodiscard]] OrderCost compute_cost(const std::vector<OrderItem>& items);

// Coût d'une séquence avec les points de départ / d'arrivée optionnels (HP-SEQ-008) :
// ajoute départ -> début du premier et fin du dernier -> arrivée au trajet.
[[nodiscard]] OrderCost compute_cost(const std::vector<OrderItem>& items,
                                     const std::optional<Vec2um>& start,
                                     const std::optional<Vec2um>& end);

// Renvoie le nouvel ordre (liste d'ObjectId). Les objets verrouillés
// conservent leur position ; seuls les objets libres sont réarrangés dans
// les emplacements libres, dans l'ordre produit par la stratégie.
[[nodiscard]] std::vector<ObjectId> optimize_order(const std::vector<OrderItem>& items,
                                                   OrderStrategy strategy,
                                                   const OrderOptions& options = {});

// Un objet placé dans l'ordre, avec le sens de couture retenu.
struct OrderStep {
    ObjectId id;
    bool reversed{false};
};

// HP-ENG-010 : même ordre que `optimize_order`, mais le coût des déplacements est calculé de la
// sortie de l'objet précédent à l'entrée du suivant (extrémités, pas centres) et le sens de
// chaque objet `reversible` est choisi au plus proche. Sans extrémités ni `reversible`, le
// résultat est identique à `optimize_order` (`reversed == false`).
[[nodiscard]] std::vector<OrderStep> plan_order(const std::vector<OrderItem>& items,
                                                OrderStrategy strategy,
                                                const OrderOptions& options = {});

// Départ d'un contour fermé au sommet le plus proche de `from` : indice du sommet (premier en
// cas d'égalité, donc déterministe). `ring` vide : 0.
[[nodiscard]] std::size_t nearest_vertex_index(const std::vector<Vec2um>& ring, Vec2um from);

} // namespace openstitch::optimization
