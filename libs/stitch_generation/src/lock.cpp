// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/lock.hpp"

#include <algorithm>
#include <cmath>

namespace openstitch::stitch_generation {

std::vector<Vec2um> lock_stitches(Vec2um anchor, Vec2um toward, LockType type, Micrometers length,
                                  int passes) {
    std::vector<Vec2um> pts;
    if (type == LockType::None) {
        return pts;
    }
    double dx = static_cast<double>(toward.x.value - anchor.x.value);
    double dy = static_cast<double>(toward.y.value - anchor.y.value);
    double n = std::hypot(dx, dy);
    const bool degenerate = n < 1e-6;
    if (degenerate) {
        dx = 1.0;
        dy = 0.0;
        n = 1.0;
    }
    dx /= n;
    dy /= n;
    // Le lock ne doit jamais dépasser `toward` (la station voisine réellement
    // cousue) : au-delà, l'aiguille pique hors de la matière déjà cousue — cas
    // réel sur une colonne fine (largeur locale < `length`, souvent la valeur
    // par défaut de 800 µm, p. ex. du texte fin) puisque `toward` est le point
    // du RAIL OPPOSÉ (largeur de la colonne), pas un point lointain le long de
    // la couture. Défaut trouvé par revue. Non borné dans le cas dégénéré
    // (anchor == toward) : aucune référence géométrique fiable n'existe alors,
    // comportement de repli inchangé.
    const double requested = static_cast<double>(std::max<std::int32_t>(1, length.value));
    const double L = degenerate ? requested : std::min(requested, n);
    const double px = -dy;  // perpendiculaire unitaire
    const double py = dx;
    const int reps = std::max(1, passes);
    const auto P = [&](double along, double side) {
        return Vec2um{
            Micrometers{static_cast<std::int32_t>(std::lround(anchor.x.value + dx * along + px * side))},
            Micrometers{static_cast<std::int32_t>(std::lround(anchor.y.value + dy * along + py * side))}};
    };

    if (type == LockType::BackAndForth) {
        pts.push_back(anchor);
        for (int i = 0; i < reps; ++i) {
            pts.push_back(P(L, 0.0));
            pts.push_back(anchor);
        }
    } else if (type == LockType::Triangle) {
        for (int i = 0; i < reps; ++i) {
            pts.push_back(anchor);
            pts.push_back(P(L, 0.0));
            pts.push_back(P(L * 0.5, L * 0.6));
            pts.push_back(anchor);
        }
    } else {  // MicroZigzag
        pts.push_back(anchor);
        for (int i = 0; i < reps * 2; ++i) {
            const double side = (i % 2 == 0) ? 1.0 : -1.0;
            pts.push_back(P(L * 0.4 * ((i + 1) / 2), L * 0.5 * side));
        }
        pts.push_back(anchor);
    }
    return pts;
}

}  // namespace openstitch::stitch_generation
