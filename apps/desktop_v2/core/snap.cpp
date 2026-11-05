// SPDX-License-Identifier: Apache-2.0
#include "snap.hpp"

#include <algorithm>
#include <cmath>

namespace openstitch::desktop_v2 {

namespace {

struct Match {
    bool found{false};
    double correction{0.0}; // à ajouter au décalage demandé
    double position{0.0};   // position de la droite cible
    double distance{0.0};
};

// Meilleur accrochage d'un jeu de trois lignes mobiles (bord bas/gauche, centre, bord haut/droit)
// sur un jeu de lignes cibles.
void consider(Match& best, const double moving[3], double target, double threshold) {
    for (int i = 0; i < 3; ++i) {
        const double distance = std::abs(target - moving[i]);
        if (distance <= threshold && (!best.found || distance < best.distance - 1e-9)) {
            best = {true, target - moving[i], target, distance};
        }
    }
}

} // namespace

SnapResult snapMove(const SnapBox& moving, PointMm delta, const std::vector<SnapBox>& targets,
                    double thresholdMm, bool includeOrigin) {
    SnapResult out;
    out.delta = delta;
    if (!(thresholdMm > 0.0) || !std::isfinite(delta.x) || !std::isfinite(delta.y)) {
        return out;
    }
    const double mx[3] = {moving.x0 + delta.x, moving.centreX() + delta.x, moving.x1 + delta.x};
    const double my[3] = {moving.y0 + delta.y, moving.centreY() + delta.y, moving.y1 + delta.y};

    Match bx;
    Match by;
    for (const auto& t : targets) {
        for (const double line : {t.x0, t.centreX(), t.x1}) {
            consider(bx, mx, line, thresholdMm);
        }
        for (const double line : {t.y0, t.centreY(), t.y1}) {
            consider(by, my, line, thresholdMm);
        }
    }
    if (includeOrigin) {
        consider(bx, mx, 0.0, thresholdMm);
        consider(by, my, 0.0, thresholdMm);
    }
    if (bx.found) {
        out.delta.x += bx.correction;
        out.snappedX = true;
    }
    if (by.found) {
        out.delta.y += by.correction;
        out.snappedY = true;
    }

    // Repères : étendue = union de la sélection (après correction) et des cibles alignées.
    const SnapBox placed{moving.x0 + out.delta.x, moving.y0 + out.delta.y, moving.x1 + out.delta.x,
                         moving.y1 + out.delta.y};
    const auto extent = [&](bool vertical, double position) {
        double lo = vertical ? placed.y0 : placed.x0;
        double hi = vertical ? placed.y1 : placed.x1;
        for (const auto& t : targets) {
            const double a = vertical ? t.x0 : t.y0;
            const double c = vertical ? t.centreX() : t.centreY();
            const double b = vertical ? t.x1 : t.y1;
            if (std::abs(a - position) < 1e-6 || std::abs(c - position) < 1e-6 ||
                std::abs(b - position) < 1e-6) {
                lo = std::min(lo, vertical ? t.y0 : t.x0);
                hi = std::max(hi, vertical ? t.y1 : t.x1);
            }
        }
        return std::pair{lo, hi};
    };
    if (out.snappedX) {
        const auto [lo, hi] = extent(true, bx.position);
        out.guides.push_back({true, bx.position, lo, hi});
    }
    if (out.snappedY) {
        const auto [lo, hi] = extent(false, by.position);
        out.guides.push_back({false, by.position, lo, hi});
    }
    return out;
}

} // namespace openstitch::desktop_v2
