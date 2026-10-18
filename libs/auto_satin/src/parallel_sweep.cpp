// SPDX-License-Identifier: Apache-2.0
#include "openstitch/auto_satin/parallel_sweep.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>
#include <vector>

#include "chord.hpp"
#include "coverage.hpp"
#include "geometry_detail.hpp"
#include "openstitch/geometry/moments.hpp"
#include "openstitch/geometry/polyline.hpp"

namespace openstitch::auto_satin {

namespace {

using detail::ChordInterval;
using detail::P2;

constexpr double kMinOverlapUm = 1.0; // recouvrement minimal pour relier deux intervalles

Vec2um to_um(P2 p) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(p.x))},
                  Micrometers{static_cast<std::int32_t>(std::lround(p.y))}};
}

// Angle des traversées : explicite, ou perpendiculaire à l'axe principal d'inertie quand la
// forme est assez allongée (les fils traversent sa largeur), horizontal sinon.
double sweep_angle(const geometry::PathSet& region, const SkeletonSatinParameters& params) {
    if (!params.sweep_auto_angle) {
        return params.sweep_angle_rad;
    }
    if (const auto pa = geometry::principal_axis({region}); pa && pa->anisotropy > 1.3) {
        return pa->angle.radians + std::numbers::pi / 2.0;
    }
    return 0.0;
}

struct Band {
    std::vector<SkeletonSatinCrossing> crossings;
    std::vector<Vec2um> axis;
};

// Un intervalle intérieur d'une droite, avec ses extrémités et ses bornes dans la coordonnée
// COMMUNE `u0` : deux droites d'angles différents ne partagent pas le paramètre `t`.
struct SweepInterval {
    ChordInterval chord;
    double lo{0.0};
    double hi{0.0};
    P2 a{};
    P2 b{};
};

double overlap(const SweepInterval& a, const SweepInterval& b) {
    return std::min(a.hi, b.hi) - std::max(a.lo, b.lo);
}

// Écart maximal d'une droite à l'angle de référence : au-delà, les droites ne progressent plus
// le long de la forme (60° = espacement doublé pour garder la densité).
constexpr double kMaxDeviationRad = std::numbers::pi / 3.0;
// Part de `spacing / demi-étendue` dont l'angle peut varier d'une droite à la suivante : au-delà,
// deux droites voisines se couperaient à l'intérieur de la forme.
constexpr double kSlewFactor = 0.25;

// Ramène un écart d'angle (modulo π) dans ]-π/2, π/2] ; ±90° exact tourne dans le sens positif
// (même règle que le squelette).
double wrap_half_pi(double d) {
    return d - std::numbers::pi * std::ceil(d / std::numbers::pi - 0.5);
}

// Guide projeté sur l'axe de balayage : position `s` et écart d'angle à la référence.
struct GuideStop {
    double s{0.0};
    double dev{0.0};
};

// Écart d'angle interpolé linéairement entre guides ; constant avant le premier et après le
// dernier. Les écarts sont déjà bornés à ±60° : l'interpolation directe est l'arc le plus court.
double field_deviation(const std::vector<GuideStop>& stops, double s) {
    if (stops.empty()) {
        return 0.0;
    }
    if (s <= stops.front().s) {
        return stops.front().dev;
    }
    if (s >= stops.back().s) {
        return stops.back().dev;
    }
    for (std::size_t i = 1; i < stops.size(); ++i) {
        if (s <= stops[i].s) {
            const double span = stops[i].s - stops[i - 1].s;
            if (!(span > 0.0)) {
                return stops[i].dev;
            }
            const double w = (s - stops[i - 1].s) / span;
            return stops[i - 1].dev + w * (stops[i].dev - stops[i - 1].dev);
        }
    }
    return stops.back().dev;
}

} // namespace

Result<SkeletonSatinResult> generate_parallel_sweep(const geometry::PathSet& curved,
                                                    const SkeletonSatinParameters& params) {
    // Les courbes sont aplaties (50 µm, comme la génération de points) : la région est ensuite
    // un polygone, quelle que soit l'origine de l'appelant (aperçu, API, export).
    const geometry::PathSet region = geometry::flatten_to_polygon(curved, Micrometers{50});
    SkeletonSatinResult result;
    auto& diag = result.diagnostics;
    // Région vide ou dégénérée : aucun guide ne peut s'appliquer. Recalculé plus bas sinon.
    diag.orphan_guides = static_cast<int>(params.guides.size());

    const std::vector<detail::Poly> polys = detail::region_polys(region);
    if (polys.empty()) {
        diag.messages.push_back("région vide : aucune traversée");
        return result;
    }
    const double spacing = std::max(50.0, static_cast<double>(params.spacing.value));
    const double minThread = static_cast<double>(params.min_thread_length.value);

    const double angle = sweep_angle(region, params);
    const P2 u{std::cos(angle), std::sin(angle)}; // direction des traversées
    const P2 n{-u.y, u.x};                        // direction du balayage

    double smin = 0.0;
    double smax = 0.0;
    bool first = true;
    for (const auto& p : polys.front()) { // l'extérieur borne la région
        const double s = detail::dot(p, n);
        smin = first ? s : std::min(smin, s);
        smax = first ? s : std::max(smax, s);
        first = false;
    }
    const double range = smax - smin;
    if (!(range > 0.0)) {
        diag.messages.push_back("région dégénérée : aucune traversée");
        return result;
    }

    // Guides : chacun fixe l'angle des fils à sa position le long de l'axe de balayage. Un guide
    // dont l'ancre sort de l'étendue de la forme est orphelin (signalé, ignoré).
    P2 centre{0.0, 0.0};
    for (const auto& p : polys.front()) {
        centre = centre + p;
    }
    centre = centre * (1.0 / static_cast<double>(polys.front().size()));
    double halfExtent = spacing; // demi-étendue le long des fils, bornant le taux de rotation
    for (const auto& p : polys.front()) {
        halfExtent = std::max(halfExtent, std::abs(detail::dot(p - centre, u)));
    }
    std::vector<GuideStop> stops;
    diag.orphan_guides = 0;
    for (const auto& g : params.guides) {
        const double s = detail::dot(
            P2{static_cast<double>(g.anchor.x.value), static_cast<double>(g.anchor.y.value)}, n);
        if (s < smin - spacing || s > smax + spacing) {
            ++diag.orphan_guides;
            continue;
        }
        const double raw = wrap_half_pi(g.absolute ? g.angle_rad - angle : g.angle_rad);
        const double dev = std::clamp(raw, -kMaxDeviationRad, kMaxDeviationRad);
        if (dev != raw) {
            ++diag.clamped_angle;
        }
        stops.push_back({s, dev});
    }
    std::stable_sort(stops.begin(), stops.end(),
                     [](const GuideStop& a, const GuideStop& b) { return a.s < b.s; });
    if (diag.orphan_guides > 0) {
        diag.messages.push_back("balayage parallèle : " + std::to_string(diag.orphan_guides) +
                                " guide(s) hors de la forme ignoré(s)");
    }

    // Positions `s` des droites et écart d'angle de chacune. Sans guide : espacement constant
    // ajusté à l'étendue (chemin historique, sortie inchangée).
    std::vector<double> lineS;
    std::vector<double> lineDev;
    if (stops.empty()) {
        const int lines = std::max(1, static_cast<int>(std::ceil(range / spacing)));
        const double step = range / lines; // espacement réel, un peu inférieur à `spacing`
        for (int k = 0; k < lines; ++k) {
            lineS.push_back(smin + (k + 0.5) * step);
        }
        lineDev.assign(lineS.size(), 0.0);
    } else {
        // L'espacement perpendiculaire aux fils vaut `pas · cos(écart)` : le pas le long de
        // l'axe est donc agrandi de 1/cos pour garder la densité. Les droites sont réparties
        // à égalité sur l'intégrale de cos(écart)/spacing.
        const int samples =
            std::clamp(static_cast<int>(std::ceil(range / (spacing / 4.0))), 1, 200000);
        const double h = range / samples;
        std::vector<double> cumulative(static_cast<std::size_t>(samples) + 1, 0.0);
        for (int j = 0; j < samples; ++j) {
            const double dev = field_deviation(stops, smin + (j + 0.5) * h);
            cumulative[static_cast<std::size_t>(j) + 1] =
                cumulative[static_cast<std::size_t>(j)] + h * std::cos(dev) / spacing;
        }
        const double total = cumulative.back();
        const int lines = std::max(1, static_cast<int>(std::ceil(total)));
        std::size_t j = 0;
        for (int k = 0; k < lines; ++k) {
            const double target = (k + 0.5) * total / lines;
            while (j + 1 < static_cast<std::size_t>(samples) && cumulative[j + 1] < target) {
                ++j;
            }
            const double span = cumulative[j + 1] - cumulative[j];
            const double w = span > 0.0 ? (target - cumulative[j]) / span : 0.0;
            const double s = smin + (static_cast<double>(j) + std::clamp(w, 0.0, 1.0)) * h;
            lineS.push_back(s);
            lineDev.push_back(field_deviation(stops, s));
        }
        // Taux de rotation borné (aller puis retour) : deux droites voisines ne se coupent pas
        // à l'intérieur de la forme. Un guide peut donc n'être atteint qu'en partie.
        const double slew = kSlewFactor * spacing / halfExtent;
        const std::vector<double> wanted = lineDev;
        for (std::size_t k = 1; k < lineDev.size(); ++k) {
            lineDev[k] = lineDev[k - 1] + std::clamp(lineDev[k] - lineDev[k - 1], -slew, slew);
        }
        for (std::size_t k = lineDev.size(); k-- > 1;) {
            lineDev[k - 1] = lineDev[k] + std::clamp(lineDev[k - 1] - lineDev[k], -slew, slew);
        }
        for (std::size_t k = 0; k < lineDev.size(); ++k) {
            if (std::abs(lineDev[k] - wanted[k]) > 1e-9) {
                ++diag.clamped_angle;
            }
        }
    }

    std::vector<Band> bands;
    std::vector<SweepInterval> prev;
    std::vector<int> prevBand;
    for (std::size_t k = 0; k < lineS.size(); ++k) {
        const double s = lineS[k];
        // Sans guide : droite perpendiculaire au balayage (historique). Avec guides : droite qui
        // passe par l'axe de balayage (centre de la forme) et pivote selon l'écart d'angle.
        const P2 base = stops.empty() ? n * s : centre + n * (s - detail::dot(centre, n));
        const double theta = angle + lineDev[k];
        const P2 dir = stops.empty() ? u : P2{std::cos(theta), std::sin(theta)};
        std::vector<SweepInterval> cur;
        for (const auto& iv : detail::line_intervals(polys, base, dir)) {
            if (iv.length() < minThread) {
                ++diag.too_short;
                continue;
            }
            SweepInterval si;
            si.chord = iv;
            si.a = base + dir * iv.t_lo;
            si.b = base + dir * iv.t_hi;
            if (stops.empty()) {
                si.lo = iv.t_lo;
                si.hi = iv.t_hi;
            } else {
                const double ca = detail::dot(si.a, u);
                const double cb = detail::dot(si.b, u);
                si.lo = std::min(ca, cb);
                si.hi = std::max(ca, cb);
            }
            cur.push_back(si);
        }
        // Appariement un à un avec la ligne précédente ; toute division ou fusion ferme les
        // bandes concernées et ouvre de nouvelles bandes (pas de couture à travers le vide).
        std::vector<int> prevLinks(prev.size(), 0);
        std::vector<int> curLinks(cur.size(), 0);
        std::vector<int> curMatch(cur.size(), -1);
        for (std::size_t j = 0; j < cur.size(); ++j) {
            for (std::size_t i = 0; i < prev.size(); ++i) {
                if (overlap(prev[i], cur[j]) > kMinOverlapUm) {
                    ++prevLinks[i];
                    ++curLinks[j];
                    curMatch[j] = static_cast<int>(i);
                }
            }
        }
        std::vector<int> curBand(cur.size(), -1);
        for (std::size_t j = 0; j < cur.size(); ++j) {
            const int i = curMatch[j];
            if (i >= 0 && curLinks[j] == 1 && prevLinks[static_cast<std::size_t>(i)] == 1) {
                curBand[j] = prevBand[static_cast<std::size_t>(i)];
            } else {
                curBand[j] = static_cast<int>(bands.size());
                bands.emplace_back();
            }
            Band& band = bands[static_cast<std::size_t>(curBand[j])];
            const P2 a = cur[j].a;
            const P2 b = cur[j].b;
            band.crossings.push_back({to_um(a), to_um(b)});
            band.axis.push_back(to_um((a + b) * 0.5));
        }
        prev = std::move(cur);
        prevBand = std::move(curBand);
    }

    int singles = 0;
    for (auto& band : bands) {
        if (band.crossings.size() < 2) {
            ++singles; // un seul fil : pas de zigzag possible
            continue;
        }
        SkeletonSatinColumn column;
        column.crossings = std::move(band.crossings);
        result.columns.push_back(std::move(column));
        result.axes.push_back(std::move(band.axis));
    }
    diag.pieces = static_cast<int>(result.columns.size());
    if (singles > 0) {
        diag.messages.push_back("balayage parallèle : " + std::to_string(singles) +
                                " bande(s) d'un seul fil ignorée(s)");
    }
    if (result.columns.empty()) {
        diag.messages.push_back("balayage parallèle : aucune bande continue");
    }
    if (params.measure_coverage) {
        detail::measure_coverage(region, result);
    }
    return result;
}

} // namespace openstitch::auto_satin
