// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <vector>

#include "openstitch/autodigitize/contour_network.hpp"
#include "openstitch/core/units.hpp"

namespace openstitch::autodigitize {

// Paramètres du routage en chemin unique (parcours eulérien) d'un réseau de
// traits.
struct ContourRouteParams {
    // Au-delà de cette distance à vol d'oiseau entre deux extrémités à relier,
    // le saut coûterait une COUPE de fil : repasser sur un tronçon déjà cousu
    // devient alors préférable (si le trajet est assez court). En dessous, un
    // simple saut suffit et rien n'est repassé.
    Micrometers trim_threshold{3'000};
    // Longueur maximale d'un trajet repassé (doublé) pour éviter une coupe.
    Micrometers max_retrace{12'000};
};

struct RoutedTrail {
    std::vector<Vec2um> points; // polyligne ; fermée : le dernier point ne répète pas le premier
    bool closed{false};
};

struct RouteStats {
    std::size_t trails{0};    // nombre de polylignes produites
    std::size_t odd_nodes{0}; // nœuds de degré impair avant doublage
    std::size_t retraces{0};  // tronçons repassés pour éviter une coupe
    double retraced_um{0.0};  // longueur supplémentaire cousue (repassée)
};

// Relie les segments `segs` (indices dans `comp.segments`) en un MINIMUM de
// polylignes en cherchant un parcours eulérien : chaque tronçon est cousu au
// moins une fois, et deux extrémités éloignées d'au moins `trim_threshold`
// sont reliées en repassant sur le plus court chemin du réseau (si celui-ci
// fait au plus `max_retrace`) plutôt qu'en coupant le fil. Une jonction n'impose
// plus de paire « alignée » : n'importe quelle arête peut en continuer une
// autre, ce qui donne au pire |impairs|/2 polylignes sans aucun repassage.
//
// Idée classique du problème du postier chinois, ici glouton et déterministe
// (implémentation d'après la théorie des graphes ; aucun code tiers). Les
// segments sans nœuds aux deux bouts (anneaux purs, traits de pixels isolés)
// sont rendus tels quels. Déterministe.
[[nodiscard]] std::vector<RoutedTrail> route_single_path(const ContourComponent& comp,
                                                         const std::vector<std::size_t>& segs,
                                                         const ContourRouteParams& params = {},
                                                         RouteStats* stats = nullptr);

} // namespace openstitch::autodigitize
