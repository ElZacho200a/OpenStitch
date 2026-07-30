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

// Hachage déterministe (splitmix64) -> [0,1). Aucun aléa non reproductible.
double jitter01(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    x = x ^ (x >> 31);
    return static_cast<double>(x >> 11) / static_cast<double>(1ull << 53);
}

PointD lerpP(PointD a, PointD b, double t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}; }
PointD midP(PointD a, PointD b) { return {(a.x + b.x) / 2.0, (a.y + b.y) / 2.0}; }

// Fil satin : deux extrémités + drapeau « barreau » (ne pas retirer/altérer).
struct Thread {
    PointD a;
    PointD b;
    bool anchor{false};
    bool dropped{false};
};

// Profil de réduction de largeur d'une terminaison (0 = point, 1 = pleine
// largeur), sur `len` fils depuis le bout.
double cap_factor(SatinCapType type, int stepsFromEnd, int len) {
    if (len <= 0 || stepsFromEnd >= len) {
        return 1.0;
    }
    const double u = static_cast<double>(stepsFromEnd) / static_cast<double>(len);  // 0 au bout
    constexpr double kMin = 0.18;  // jamais 0 : évite d'empiler sur un point unique
    switch (type) {
    case SatinCapType::Tapered:
        return kMin + (1.0 - kMin) * u;  // linéaire -> pointe
    case SatinCapType::Rounded:
        return kMin + (1.0 - kMin) * std::sin(u * std::acos(-1.0) / 2.0);  // arrondi
    default:
        return 1.0;  // Flat : inchangé
    }
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
    std::vector<Thread> threads;
    const auto midOf = [&](double sa, double sb) {
        return midP(point_at(a, cumA, sa), point_at(b, cumB, sb));
    };
    threads.push_back({anchors.front().pa, anchors.front().pb, true, false});
    for (std::size_t k = 0; k + 1 < anchors.size(); ++k) {
        const Anchor& a0 = anchors[k];
        const Anchor& a1 = anchors[k + 1];
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
                               point_at(b, cumB, a0.sb + (a1.sb - a0.sb) * u), false, false});
        }
        threads.push_back({a1.pa, a1.pb, true, false});  // barreau traversé exactement
    }
    const int nThreads = static_cast<int>(threads.size());

    // --- Terminaisons (§9) : réduit la largeur des fils aux deux bouts. ---
    for (int i = 0; i < nThreads; ++i) {
        const double fStart = cap_factor(config.cap_start, i, config.cap_length);
        const double fEnd = cap_factor(config.cap_end, nThreads - 1 - i, config.cap_length);
        const double f = std::min(fStart, fEnd);
        if (f < 1.0) {
            const PointD m = midP(threads[static_cast<std::size_t>(i)].a,
                                  threads[static_cast<std::size_t>(i)].b);
            threads[static_cast<std::size_t>(i)].a = lerpP(m, threads[static_cast<std::size_t>(i)].a, f);
            threads[static_cast<std::size_t>(i)].b = lerpP(m, threads[static_cast<std::size_t>(i)].b, f);
        }
    }

    // --- Points courts (§7) : dans un virage serré, le rail intérieur reçoit
    // des pénétrations rentrées (inset), ou est allégé (remove/redistribute). ---
    if (config.short_stitch != ShortStitchMode::Disabled) {
        const double minGap = static_cast<double>(config.short_stitch_min_gap.value);
        const int levels = std::max(1, config.short_stitch_levels);
        bool prevDropped = false;
        for (int i = 1; i < nThreads; ++i) {
            auto& t = threads[static_cast<std::size_t>(i)];
            auto& p = threads[static_cast<std::size_t>(i - 1)];
            const double advA = dist(t.a, p.a);
            const double advB = dist(t.b, p.b);
            const double lo = std::min(advA, advB);
            const double hi = std::max(advA, advB);
            const bool tight = hi > 1e-6 && (lo / hi) < config.short_stitch_curvature;
            if (!tight || lo >= minGap || t.anchor) {
                prevDropped = false;
                continue;
            }
            const bool innerIsA = advA < advB;
            if (config.short_stitch == ShortStitchMode::RemoveAndRedistribute) {
                // Retire un fil serré sur deux (jamais deux d'affilée, jamais un
                // barreau) : le voisin couvre.
                if (!prevDropped) {
                    t.dropped = true;
                    prevDropped = true;
                } else {
                    prevDropped = false;
                }
                continue;
            }
            // Inset : profondeur selon le niveau (triangulaire en MultiLevel).
            double frac = config.short_stitch_inset;
            if (config.short_stitch == ShortStitchMode::MultiLevelInset) {
                const int period = 2 * levels;
                const int ph = i % period;
                const int tri = ph <= levels ? ph : period - ph;  // 0..levels..0
                frac = config.short_stitch_inset * static_cast<double>(tri) / levels;
            }
            const PointD m = midP(t.a, t.b);
            if (innerIsA) {
                t.a = lerpP(t.a, m, frac);
            } else {
                t.b = lerpP(t.b, m, frac);
            }
            prevDropped = false;
        }
    }

    // --- Émission zigzag L0,R0,L1,R1,… avec split (§8) des traversées longues. ---
    const double maxLen = static_cast<double>(std::max<std::int32_t>(1, config.max_stitch_length.value));
    int emitted = 0;
    for (int i = 0; i < nThreads; ++i) {
        const auto& t = threads[static_cast<std::size_t>(i)];
        if (t.dropped) {
            continue;
        }
        PointD pa = t.a;
        PointD pb = t.b;
        result.max_width_um = std::max(result.max_width_um, dist(pa, pb));
        std::tie(pa, pb) = compensate(pa, pb, comp);
        result.satin.push_back(toUm(pa));
        // Split : pénétrations intermédiaires le long de la traversée pa->pb.
        const double len = dist(pa, pb);
        if (config.split_stitch != SplitStitchMode::Disabled && len > maxLen) {
            const int nsplit = std::max(1, static_cast<int>(std::ceil(len / maxLen)) - 1);
            for (int s = 1; s <= nsplit; ++s) {
                double frac = static_cast<double>(s) / (nsplit + 1);
                const double amp = 0.35 / (nsplit + 1);  // amplitude du décalage
                if (config.split_stitch == SplitStitchMode::Staggered) {
                    frac += (emitted % 2 == 0 ? amp : -amp);
                } else if (config.split_stitch == SplitStitchMode::DeterministicJitter) {
                    const std::uint64_t h = config.split_seed * 1000003ull +
                                            static_cast<std::uint64_t>(emitted) * 97ull +
                                            static_cast<std::uint64_t>(s);
                    frac += (jitter01(h) * 2.0 - 1.0) * amp;
                }
                frac = std::clamp(frac, 0.05, 0.95);
                result.satin.push_back(toUm(lerpP(pa, pb, frac)));
            }
        }
        result.satin.push_back(toUm(pb));
        ++emitted;
    }

    // Sous-couche centrale optionnelle (point droit sur la médiane).
    if (config.center_underlay) {
        const double uspace =
            static_cast<double>(std::max<std::int32_t>(1, config.underlay_spacing.value));
        const std::size_t stride = std::max<std::size_t>(1, static_cast<std::size_t>(uspace / density));
        for (std::size_t i = 0; i < threads.size(); i += stride) {
            result.underlay.push_back(
                toUm(midP(threads[i].a, threads[i].b)));
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
