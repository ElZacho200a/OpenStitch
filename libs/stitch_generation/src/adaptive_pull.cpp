// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/adaptive_pull.hpp"

#include <algorithm>
#include <cmath>

namespace openstitch::stitch_generation {

namespace {

struct V {
    double x{0.0};
    double y{0.0};
};

double len(V a, V b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

constexpr double kUmPerMm = 1000.0;

} // namespace

std::vector<SatinPullSample> adaptive_satin_pull(const std::vector<SatinPullInput>& stations,
                                                 const AdaptiveSatinModel& model,
                                                 double max_per_side_um) {
    const std::size_t n = stations.size();
    std::vector<SatinPullSample> out(n);
    if (n == 0) {
        return out;
    }

    std::vector<V> mid(n);
    std::vector<double> widthMm(n);
    std::vector<double> arc(n, 0.0); // µm
    std::vector<std::size_t> segment(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        const SatinPullInput& s = stations[i];
        mid[i] = {(s.ax + s.bx) * 0.5, (s.ay + s.by) * 0.5};
        widthMm[i] = len({s.ax, s.ay}, {s.bx, s.by}) / kUmPerMm;
        if (i > 0) {
            arc[i] = arc[i - 1] + len(mid[i], mid[i - 1]);
            segment[i] = segment[i - 1] + (s.jump_before ? 1u : 0u);
        }
    }
    const double total = arc.back();

    // Début / fin de chaque tronçon continu (un saut coupe la propagation).
    std::vector<std::size_t> segLo(n);
    std::vector<std::size_t> segHi(n);
    for (std::size_t i = 0; i < n;) {
        std::size_t j = i;
        while (j + 1 < n && segment[j + 1] == segment[i]) {
            ++j;
        }
        for (std::size_t k = i; k <= j; ++k) {
            segLo[k] = i;
            segHi[k] = j;
        }
        i = j + 1;
    }

    // 1) Modèle de base + modulations (extrémité, jonction, variation de largeur).
    std::vector<double> value(n); // mm, par côté
    std::vector<std::uint32_t> flags(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        const double base =
            std::clamp(model.a_mm + model.b * widthMm[i], model.min_mm, model.max_mm);
        out[i].base_mm = base;
        double f = 1.0;

        // Proximité des extrémités de la colonne ENTIÈRE (arc depuis le premier/dernier point).
        const double dEnd = std::min(arc[i], total - arc[i]) / kUmPerMm;
        if (model.end_taper_mm > 0.0 && dEnd < model.end_taper_mm) {
            f *= lerp(model.end_factor, 1.0, dEnd / model.end_taper_mm);
            flags[i] |= kPullEndReduced;
        }
        // Proximité d'une jonction = bord d'un tronçon qui n'est pas une extrémité de colonne.
        double dJunction = 1e30;
        if (segLo[i] > 0) {
            dJunction = std::min(dJunction, arc[i] - arc[segLo[i]]);
        }
        if (segHi[i] + 1 < n) {
            dJunction = std::min(dJunction, arc[segHi[i]] - arc[i]);
        }
        dJunction /= kUmPerMm;
        if (model.junction_mm > 0.0 && dJunction < model.junction_mm) {
            f *= lerp(model.junction_factor, 1.0, dJunction / model.junction_mm);
            flags[i] |= kPullJunctionReduced;
        }
        // Variation de largeur : une colonne qui s'évase vite n'a pas de largeur « stable ».
        const std::size_t lo = i > segLo[i] ? i - 1 : i;
        const std::size_t hi = i < segHi[i] ? i + 1 : i;
        const double dArc = (arc[hi] - arc[lo]) / kUmPerMm;
        if (dArc > 1e-9) {
            const double slope = std::abs(widthMm[hi] - widthMm[lo]) / dArc;
            if (slope * model.slope_gain > 0.05) {
                flags[i] |= kPullSlopeReduced;
            }
            f /= 1.0 + model.slope_gain * slope;
        }
        value[i] = base * f * model.scale;
    }

    // 2) Lissage spatial : moyenne à poids triangulaires dans le rayon `smooth_mm`, bornée au
    //    tronçon courant. Déterministe (ordre d'accumulation fixe).
    std::vector<double> smooth(value);
    const double radius = model.smooth_mm * kUmPerMm;
    if (radius > 0.0) {
        for (std::size_t i = 0; i < n; ++i) {
            double acc = 0.0;
            double wsum = 0.0;
            for (std::size_t j = segLo[i]; j <= segHi[i]; ++j) {
                const double d = std::abs(arc[j] - arc[i]);
                if (d > radius) {
                    continue;
                }
                const double w = 1.0 - d / radius;
                acc += w * value[j];
                wsum += w;
            }
            if (wsum > 0.0) {
                smooth[i] = acc / wsum;
            }
        }
    }

    // 3) Bornes de sécurité par côté : fraction de la largeur, rayon intérieur des virages,
    //    plafond absolu. Le côté intérieur est déterminé par le signe du virage de l'axe.
    for (std::size_t i = 0; i < n; ++i) {
        const SatinPullInput& s = stations[i];
        const double wUm = widthMm[i] * kUmPerMm;
        double offUm = smooth[i] * kUmPerMm;
        double left = offUm;
        double right = offUm;

        // Courbure locale par le cercle circonscrit à (i-1, i, i+1) au sein du tronçon.
        double kappa = 0.0;
        double turn = 0.0;
        if (i > segLo[i] && i < segHi[i]) {
            const V p0 = mid[i - 1];
            const V p1 = mid[i];
            const V p2 = mid[i + 1];
            const double a = len(p0, p1);
            const double b = len(p1, p2);
            const double c = len(p0, p2);
            turn = (p1.x - p0.x) * (p2.y - p1.y) - (p1.y - p0.y) * (p2.x - p1.x);
            const double area2 = std::abs(turn); // 2 × aire
            if (a > 1e-9 && b > 1e-9 && c > 1e-9 && area2 > 1e-9) {
                kappa = 2.0 * area2 / (a * b * c); // 1/R = 4·aire / (abc)
            }
        }
        out[i].curvature_per_mm = kappa * kUmPerMm;
        if (kappa > 0.0) {
            const double radiusUm = 1.0 / kappa;
            const double innerRadius = radiusUm - wUm * 0.5;
            const double cap = innerRadius > 0.0 ? model.inner_radius_fraction * innerRadius : 0.0;
            // Normale gauche de la marche : (-ty, tx) avec t = p2 - p0. A est à gauche si
            // (A - milieu) · normale > 0 ; virage à gauche (turn > 0) => gauche = intérieur.
            const V t{mid[i + 1].x - mid[i - 1].x, mid[i + 1].y - mid[i - 1].y};
            const double aLeft = (s.ax - mid[i].x) * (-t.y) + (s.ay - mid[i].y) * t.x;
            const bool aIsInner = (aLeft > 0.0) == (turn > 0.0);
            double& inner = aIsInner ? left : right;
            if (inner > cap) {
                inner = cap;
                flags[i] |= kPullCurvatureCapped;
            }
        }
        const double widthCap = model.max_width_fraction * wUm;
        const double hard = std::min(model.hard_max_mm * kUmPerMm, std::max(0.0, max_per_side_um));
        for (double* side : {&left, &right}) {
            if (*side > widthCap) {
                *side = widthCap;
                flags[i] |= kPullWidthCapped;
            }
            *side = std::clamp(*side, 0.0, hard);
        }
        out[i].left_um = left;
        out[i].right_um = right;
        out[i].arc_mm = arc[i] / kUmPerMm;
        out[i].width_mm = widthMm[i];
        out[i].flags = flags[i];
    }
    return out;
}

} // namespace openstitch::stitch_generation
