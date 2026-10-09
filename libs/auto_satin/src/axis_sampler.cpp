// SPDX-License-Identifier: Apache-2.0
#include "axis_sampler.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace openstitch::auto_satin::detail {

namespace {

// Axe prolongé en ligne droite au-delà de ses extrémités (le squelette de
// Zhang-Suen s'arrête environ un rayon avant le bord d'un bout ouvert).
struct ExtendedAxis {
    const Axis& axis;
    P2 startPoint;
    P2 endPoint;
    P2 startTangent; // unitaire, direction des s croissants
    P2 endTangent;
    double length;

    explicit ExtendedAxis(const Axis& ax)
        : axis(ax), startPoint(ax.position(0.0)), endPoint(ax.position(ax.length())),
          length(ax.length()) {
        const double a0 = ax.alpha(0.0);
        const double a1 = ax.alpha(ax.length());
        startTangent = {std::cos(a0), std::sin(a0)};
        endTangent = {std::cos(a1), std::sin(a1)};
    }

    [[nodiscard]] P2 position(double s) const {
        if (axis.closed()) {
            return axis.position(s);
        }
        if (s < 0.0) {
            return startPoint + startTangent * s;
        }
        if (s > length) {
            return endPoint + endTangent * (s - length);
        }
        return axis.position(s);
    }
    [[nodiscard]] double alpha(double s) const { return axis.alpha(s); }
};

// Longueur dont on peut prolonger le bout depuis `from` dans la direction `dir`
// tant que le point reste dans la région, par pas de 50 µm, plafonnée.
double extension_length(const std::vector<Poly>& polys, P2 from, P2 dir, double cap) {
    constexpr double kStep = 50.0;
    const double r = distance_to_polys(polys, from);
    const double limit = std::min(cap, 3.0 * r + 1000.0);
    // Marge de 1 µm : un point exactement SUR le contour rend la corde
    // colinéaire à une arête et sa longueur indéfinie (parité de la règle
    // demi-ouverte), il ne doit jamais devenir un échantillon.
    constexpr double kBoundaryMargin = 1.0;
    double ext = 0.0;
    for (double d = kStep; d <= limit; d += kStep) {
        const P2 q = from + dir * d;
        if (!in_region(polys, q) || distance_to_polys(polys, q) <= kBoundaryMargin) {
            break;
        }
        ext = d;
    }
    return ext;
}

struct Evaluated {
    AxisSample sample;
    double h_next{0.0};
    bool ok{false};
};

} // namespace

SamplerResult sample_axis(const Axis& axis, const std::vector<Poly>& polys,
                          const OrientationKeys& keys, const SamplerParams& prm,
                          const SamplerContext& ctx) {
    SamplerResult out;
    if (axis.empty() || polys.empty()) {
        return out;
    }
    const ExtendedAxis ext(axis);
    const double h_max = prm.h_max_ratio * prm.spacing_um;
    const double sMin = std::asin(std::clamp(prm.min_sin, 0.0, 0.999));

    const bool closed = axis.closed();
    out.s_begin = (ctx.extend_start && !closed)
                      ? -extension_length(polys, ext.startPoint, ext.startTangent * -1.0,
                                          prm.max_extension_um)
                      : 0.0;
    out.s_end = ext.length +
                ((ctx.extend_end && !closed)
                     ? extension_length(polys, ext.endPoint, ext.endTangent, prm.max_extension_um)
                     : 0.0);

    // Orientation g(s) avec plancher |sin(g − α)| ≥ min_sin.
    const auto orient = [&](double s, double alpha, bool count) {
        double g = orientation_at(keys, s, alpha);
        const double d = wrap_half_pi(g - alpha);
        if (std::abs(std::sin(d)) < prm.min_sin) {
            g = alpha + (d >= 0.0 ? sMin : -sMin);
            if (count) {
                ++out.diagnostics.clamped_angle;
            }
        }
        return g;
    };

    // Dérivée de l'orientation par différence centrée (rad/µm), sur une fenêtre
    // indépendante du pas des traversées.
    const double dsw = 100.0;
    const auto gradient = [&](double s) {
        const double alo = ext.alpha(s - dsw);
        const double ahi = ext.alpha(s + dsw);
        const double glo = orient(s - dsw, alo, false);
        const double ghi = orient(s + dsw, ahi, false);
        return wrap_half_pi(ghi - glo) / (2.0 * dsw);
    };

    const auto evaluate = [&](double s) {
        Evaluated ev;
        const P2 p = ext.position(s);
        const double alpha = ext.alpha(s);
        const double g = orient(s, alpha, true);
        const P2 u{std::cos(g), std::sin(g)};
        auto chord = chord_through(polys, p, u, prm.tolerance_um);
        if (chord && ctx.clip) {
            chord = ctx.clip(s, p, u, *chord);
        }
        if (!chord) {
            ++out.diagnostics.outside_samples;
            return ev;
        }
        // Non-croisement : deux cordes voisines se coupent au centre instantané de rotation
        // de l'orientation, à t* = −σ/g' (σ = sin(α − g)). Du côté intérieur d'un virage
        // serré, une corde qui dépasse ce centre traverse ses voisines (nœud de fils). On
        // la borne là où l'espacement local retombe à `converge_keep` × celui de l'axe :
        // |σ + t·g'| ≥ κ|σ|, soit |t| ≤ (1 − κ)|σ|/|g'| du côté qui converge.
        const double sigma = std::sin(alpha - g);
        const double gp = gradient(s);
        if (std::abs(gp) > 1e-12) {
            const double tLimit = (1.0 - prm.converge_keep) * std::abs(sigma) / std::abs(gp);
            if (sigma * gp < 0.0) { // t > 0 converge
                chord->t_hi = std::min(chord->t_hi, tLimit);
            } else { // t < 0 converge
                chord->t_lo = std::max(chord->t_lo, -tLimit);
            }
            if (chord->t_hi < chord->t_lo) {
                ++out.diagnostics.too_short;
                return ev;
            }
        }
        // Garde de rayon : une corde bien plus longue que le diamètre inscrit
        // traverse un bras sur sa longueur (orientation fausse près d'un coude, ou
        // corde qui rejoint une autre branche). Elle est ÉCRÊTÉE à la borne, jamais
        // laissée à la longueur du bras. Le rayon est mesuré sur l'axe lui-même
        // (pas au point prolongé, proche du bord, qui raccourcirait les bouts).
        const double r = distance_to_polys(polys, ext.position(std::clamp(s, 0.0, ext.length)));
        if (r > 0.0) {
            const double bound = prm.radius_guard * r + prm.tolerance_um;
            if (-chord->t_lo > bound || chord->t_hi > bound) {
                ++out.diagnostics.radius_guard_hits;
                chord = ChordInterval{std::max(chord->t_lo, -bound), std::min(chord->t_hi, bound)};
            }
        }
        const double len = chord->length();
        if (len < prm.min_chord_um) {
            ++out.diagnostics.too_short;
            return ev;
        }
        // Côté droit (−n) / gauche (+n) de l'axe, avec n = (−sin α, cos α).
        const P2 n{-std::sin(alpha), std::cos(alpha)};
        const bool aligned = dot(u, n) >= 0.0;
        const P2 lo = p + u * chord->t_lo;
        const P2 hi = p + u * chord->t_hi;
        ev.sample = {s, p, alpha, g, aligned ? lo : hi, aligned ? hi : lo, len};

        // Pas : l'espacement perpendiculaire à distance t de l'axe vaut
        // h·|σ + t·g'|, avec σ = sin(α − g). On borne l'espacement au bord le
        // plus écarté à ρ.
        const double a = -chord->t_lo;
        const double b = chord->t_hi;
        const double m = std::max(std::abs(sigma - a * gp), std::abs(sigma + b * gp));
        ev.h_next = m > 1e-9 ? std::clamp(prm.spacing_um / m, prm.h_min_um, h_max) : h_max;
        ev.ok = true;
        return ev;
    };

    double s = out.s_begin;
    while (true) {
        const Evaluated ev = evaluate(s);
        double h = 0.25 * prm.spacing_um; // échec : petit pas, pour ne pas sauter un coude
        if (ev.ok) {
            out.samples.push_back(ev.sample);
            h = ev.h_next;
        }
        if (s >= out.s_end) {
            break;
        }
        double next = s + h;
        if (closed && next > out.s_end - 0.35 * h) {
            break; // s = L est le point de départ : pas de doublon à la couture
        }
        // Fusionne un dernier pas trop court avec la fin pour ne pas créer deux
        // traversées presque confondues au bout.
        if (next > out.s_end - 0.35 * h) {
            next = out.s_end;
        }
        s = next;
    }
    return out;
}

} // namespace openstitch::auto_satin::detail
