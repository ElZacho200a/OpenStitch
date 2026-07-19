// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"

namespace openstitch::optimization {

// Un objet à ordonner, réduit à ce qui compte pour le coût de couture.
struct OrderItem {
    ObjectId id;
    std::array<std::uint8_t, 3> rgb{};
    Vec2um centroid{};  // position représentative (déplacements estimés par les centres)
    bool locked{false};  // reste à sa position d'origine
};

enum class OrderStrategy {
    Document,           // ordre actuel (aucun changement)
    ByColor,            // regroupe les couleurs (minimise les changements de fil)
    ByProximity,        // plus proche voisin (minimise les déplacements)
    ColorThenProximity  // groupes de couleur, puis proximité à l'intérieur
};

struct OrderCost {
    double travel_um{0.0};        // somme des déplacements entre centres
    std::size_t color_changes{0};  // nombre de changements de fil

    // Coût scalaire : les changements de couleur sont chers (arrêt machine).
    [[nodiscard]] double score() const {
        return travel_um + static_cast<double>(color_changes) * 50'000.0;
    }
};

[[nodiscard]] OrderCost compute_cost(const std::vector<OrderItem>& items);

// Renvoie le nouvel ordre (liste d'ObjectId). Les objets verrouillés
// conservent leur position ; seuls les objets libres sont réarrangés dans
// les emplacements libres, dans l'ordre produit par la stratégie.
[[nodiscard]] std::vector<ObjectId> optimize_order(const std::vector<OrderItem>& items,
                                                   OrderStrategy strategy);

}  // namespace openstitch::optimization
