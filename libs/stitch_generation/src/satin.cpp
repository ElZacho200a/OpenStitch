// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/satin.hpp"

#include <algorithm>
#include <cmath>

namespace openstitch::stitch_generation {

namespace {

struct PointD {
    double x{0.0};
    double y{0.0};
};

PointD toD(Vec2um p) {
    return {static_cast<double>(p.x.value), static_cast<double>(p.y.value)};
}

Vec2um toUm(PointD p) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(p.x))},
                  Micrometers{static_cast<std::int32_t>(std::lround(p.y))}};
}

double dist(PointD a, PointD b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

// Longueurs cumulées d'une polyligne.
std::vector<double> cumulative(const std::vector<PointD>& pts) {
    std::vector<double> cum(pts.size(), 0.0);
    for (std::size_t i = 1; i < pts.size(); ++i) {
        cum[i] = cum[i - 1] + dist(pts[i - 1], pts[i]);
    }
    return cum;
}

// Point à l'abscisse curviligne `s` le long de la polyligne.
PointD point_at(const std::vector<PointD>& pts, const std::vector<double>& cum, double s) {
    if (pts.empty()) {
        return {};
    }
    if (s <= 0.0) {
        return pts.front();
    }
    if (s >= cum.back()) {
        return pts.back();
    }
    const auto it = std::upper_bound(cum.begin(), cum.end(), s);
    const std::size_t i = static_cast<std::size_t>(it - cum.begin());
    const double segLen = cum[i] - cum[i - 1];
    const double t = segLen > 0.0 ? (s - cum[i - 1]) / segLen : 0.0;
    return {pts[i - 1].x + t * (pts[i].x - pts[i - 1].x),
            pts[i - 1].y + t * (pts[i].y - pts[i - 1].y)};
}

std::vector<PointD> to_points(const geometry::Path& path) {
    std::vector<PointD> pts;
    pts.reserve(path.nodes.size());
    for (const auto& node : path.nodes) {
        pts.push_back(toD(node.pos));
    }
    return pts;
}

// Applique la compensation de tirage : éloigne a de b (et inversement) de
// `comp` micromètres le long de leur médiatrice.
std::pair<PointD, PointD> compensate(PointD a, PointD b, double comp) {
    if (comp == 0.0) {
        return {a, b};
    }
    const double d = dist(a, b);
    if (d < 1e-6) {
        return {a, b};
    }
    const double ux = (a.x - b.x) / d;
    const double uy = (a.y - b.y) / d;
    return {{a.x + ux * comp, a.y + uy * comp}, {b.x - ux * comp, b.y - uy * comp}};
}

// Abscisse curviligne du point de `pts` le plus proche de P (projection).
double project_arclen(const std::vector<PointD>& pts, const std::vector<double>& cum, PointD P) {
    double best = 1e30;
    double bestS = 0.0;
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        const double sx = pts[i + 1].x - pts[i].x;
        const double sy = pts[i + 1].y - pts[i].y;
        const double segLen2 = sx * sx + sy * sy;
        double t = 0.0;
        if (segLen2 > 1e-9) {
            t = ((P.x - pts[i].x) * sx + (P.y - pts[i].y) * sy) / segLen2;
            t = std::clamp(t, 0.0, 1.0);
        }
        const PointD proj{pts[i].x + t * sx, pts[i].y + t * sy};
        const double dd = dist(P, proj);
        if (dd < best) {
            best = dd;
            bestS = cum[i] + t * std::sqrt(segLen2);
        }
    }
    return bestS;
}

}  // namespace

SatinResult fill_satin_columns(const geometry::Path& rail_a, const geometry::Path& rail_b,
                               const std::vector<SatinRungSeg>& rungs, const SatinConfig& config) {
    if (rungs.size() < 2) {
        return fill_satin(rail_a, rail_b, config);  // satin manuel / legacy
    }
    SatinResult result;
    const auto a = to_points(rail_a);
    const auto b = to_points(rail_b);
    if (a.size() < 2 || b.size() < 2) {
        return result;
    }
    const auto cumA = cumulative(a);
    const auto cumB = cumulative(b);
    const double density = static_cast<double>(std::max<std::int32_t>(1, config.density.value));
    const double comp = static_cast<double>(config.pull_compensation.value);

    // Ancres = barreaux projetés sur les deux rails (abscisses curvilignes),
    // gardées strictement croissantes sur les DEUX rails (correspondance saine).
    struct Anchor {
        double sa{0.0};
        double sb{0.0};
        PointD pa{};
        PointD pb{};
    };
    std::vector<Anchor> anchors;
    for (const auto& r : rungs) {
        Anchor an;
        an.pa = toD(r.first);
        an.pb = toD(r.second);
        an.sa = project_arclen(a, cumA, an.pa);
        an.sb = project_arclen(b, cumB, an.pb);
        if (anchors.empty() || (an.sa > anchors.back().sa && an.sb > anchors.back().sb)) {
            anchors.push_back(an);
        }
    }
    if (anchors.size() < 2) {
        return fill_satin(rail_a, rail_b, config);
    }

    // Fils : abscisses (sa, sb) + point exact aux barreaux. Espacement mesuré sur
    // la ligne médiane (≈ perpendiculaire aux fils).
    std::vector<std::pair<PointD, PointD>> threads;
    const auto midOf = [&](double sa, double sb) {
        const PointD pa = point_at(a, cumA, sa);
        const PointD pb = point_at(b, cumB, sb);
        return PointD{(pa.x + pb.x) / 2.0, (pa.y + pb.y) / 2.0};
    };
    threads.push_back({anchors.front().pa, anchors.front().pb});
    for (std::size_t k = 0; k + 1 < anchors.size(); ++k) {
        const Anchor& a0 = anchors[k];
        const Anchor& a1 = anchors[k + 1];
        // Échantillonnage fin de la ligne médiane de l'intervalle -> longueur réelle.
        constexpr int kSub = 48;
        std::vector<double> us(kSub + 1);
        std::vector<double> cumM(kSub + 1, 0.0);
        PointD prevM = midOf(a0.sa, a0.sb);
        for (int j = 0; j <= kSub; ++j) {
            const double u = static_cast<double>(j) / kSub;
            us[static_cast<std::size_t>(j)] = u;
            const PointD m = midOf(a0.sa + (a1.sa - a0.sa) * u, a0.sb + (a1.sb - a0.sb) * u);
            if (j > 0) {
                cumM[static_cast<std::size_t>(j)] =
                    cumM[static_cast<std::size_t>(j - 1)] + dist(prevM, m);
            }
            prevM = m;
        }
        const double total = cumM.back();
        const int n = std::max(1, static_cast<int>(std::lround(total / density)));
        const double step = total / n;
        // Fils intermédiaires (j=1..n-1) à espacement médian régulier.
        for (int jj = 1; jj < n; ++jj) {
            const double target = jj * step;
            const auto it = std::lower_bound(cumM.begin(), cumM.end(), target);
            const std::size_t idx =
                std::min<std::size_t>(static_cast<std::size_t>(it - cumM.begin()), kSub);
            const std::size_t lo = idx == 0 ? 0 : idx - 1;
            const double segLen = cumM[idx] - cumM[lo];
            const double f = segLen > 1e-9 ? (target - cumM[lo]) / segLen : 0.0;
            const double u = us[lo] + (us[idx] - us[lo]) * f;
            threads.push_back({point_at(a, cumA, a0.sa + (a1.sa - a0.sa) * u),
                               point_at(b, cumB, a0.sb + (a1.sb - a0.sb) * u)});
        }
        threads.push_back({a1.pa, a1.pb});  // barreau traversé exactement
    }

    // Émission zigzag L0,R0,L1,R1,… (chaque point cousu traverse la colonne).
    for (auto& [pa, pb] : threads) {
        result.max_width_um = std::max(result.max_width_um, dist(pa, pb));
        std::tie(pa, pb) = compensate(pa, pb, comp);
        result.satin.push_back(toUm(pa));
        result.satin.push_back(toUm(pb));
    }

    // Sous-couche centrale optionnelle (point droit sur la médiane).
    if (config.center_underlay) {
        const double uspace =
            static_cast<double>(std::max<std::int32_t>(1, config.underlay_spacing.value));
        for (std::size_t i = 0; i < threads.size(); ++i) {
            // Réutilise les positions des fils, sous-échantillonnées.
            if (i % std::max<std::size_t>(1, static_cast<std::size_t>(uspace / density)) == 0) {
                const auto& t = threads[i];
                result.underlay.push_back(toUm({(t.first.x + t.second.x) / 2.0,
                                                (t.first.y + t.second.y) / 2.0}));
            }
        }
    }
    return result;
}

SatinResult fill_satin(const geometry::Path& rail_a, const geometry::Path& rail_b,
                       const SatinConfig& config) {
    SatinResult result;
    const auto a = to_points(rail_a);
    const auto b = to_points(rail_b);
    if (a.size() < 2 || b.size() < 2) {
        return result;
    }
    const auto cumA = cumulative(a);
    const auto cumB = cumulative(b);
    const double lenA = cumA.back();
    const double lenB = cumB.back();
    const double columnLen = std::max(lenA, lenB);
    const double density = static_cast<double>(std::max<std::int32_t>(1, config.density.value));
    const double comp = static_cast<double>(config.pull_compensation.value);

    const int steps = std::max(1, static_cast<int>(std::ceil(columnLen / density)));

    // Sous-couche : point droit sur l'axe central (aller simple, pas grossier).
    if (config.center_underlay) {
        const double uspace =
            static_cast<double>(std::max<std::int32_t>(1, config.underlay_spacing.value));
        const int usteps = std::max(1, static_cast<int>(std::ceil(columnLen / uspace)));
        for (int i = 0; i <= usteps; ++i) {
            const double f = static_cast<double>(i) / static_cast<double>(usteps);
            const PointD pa = point_at(a, cumA, f * lenA);
            const PointD pb = point_at(b, cumB, f * lenB);
            result.underlay.push_back(toUm({(pa.x + pb.x) / 2.0, (pa.y + pb.y) / 2.0}));
        }
    }

    // Satin : zigzag A0, B0, A1, B1, … en avançant par fraction d'abscisse.
    for (int i = 0; i <= steps; ++i) {
        const double f = static_cast<double>(i) / static_cast<double>(steps);
        PointD pa = point_at(a, cumA, f * lenA);
        PointD pb = point_at(b, cumB, f * lenB);
        result.max_width_um = std::max(result.max_width_um, dist(pa, pb));
        std::tie(pa, pb) = compensate(pa, pb, comp);
        // Alternance du bord de départ pour former le zigzag.
        if (i % 2 == 0) {
            result.satin.push_back(toUm(pa));
            result.satin.push_back(toUm(pb));
        } else {
            result.satin.push_back(toUm(pb));
            result.satin.push_back(toUm(pa));
        }
    }
    return result;
}

std::optional<std::pair<geometry::Path, geometry::Path>> rails_from_contour(
    const geometry::Path& contour) {
    const auto pts = to_points(contour);
    if (pts.size() < 4) {
        return std::nullopt;
    }

    // Deux sommets les plus éloignés = les « bouts » de la colonne.
    std::size_t iEnd0 = 0;
    std::size_t iEnd1 = 0;
    double best = -1.0;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        for (std::size_t j = i + 1; j < pts.size(); ++j) {
            const double d = dist(pts[i], pts[j]);
            if (d > best) {
                best = d;
                iEnd0 = i;
                iEnd1 = j;
            }
        }
    }
    if (best <= 0.0) {
        return std::nullopt;
    }

    // Les deux chaînes entre iEnd0 et iEnd1 forment les deux rails.
    geometry::Path railA;
    geometry::Path railB;
    railA.closed = false;
    railB.closed = false;
    for (std::size_t i = iEnd0; i != iEnd1; i = (i + 1) % pts.size()) {
        railA.nodes.push_back(contour.nodes[i]);
    }
    railA.nodes.push_back(contour.nodes[iEnd1]);
    for (std::size_t i = iEnd1; i != iEnd0; i = (i + 1) % pts.size()) {
        railB.nodes.push_back(contour.nodes[i]);
    }
    railB.nodes.push_back(contour.nodes[iEnd0]);
    // railB parcourt le contour dans l'autre sens : on l'inverse pour que les
    // deux rails aillent du même bout au même bout.
    std::reverse(railB.nodes.begin(), railB.nodes.end());

    if (railA.nodes.size() < 2 || railB.nodes.size() < 2) {
        return std::nullopt;
    }
    return std::pair{railA, railB};
}

}  // namespace openstitch::stitch_generation
