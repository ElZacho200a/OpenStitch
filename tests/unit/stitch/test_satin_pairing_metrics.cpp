// SPDX-License-Identifier: Apache-2.0
//
// Phase A (audit défaut satin, roadmap "suivi des rails") : fixtures
// géométriques minimales + métriques reproductibles, pour démontrer et
// verrouiller la correction de l'appariement local rails gauche/droit
// (libs/stitch_generation/src/satin.cpp, ladder_correspondence).
//
// Chaque fixture ci-dessous produit un ruban satin (deux rails) dans un cas
// géométrique connu pour révéler le défaut documenté (docs/source/satin.md,
// docs/stitch-feature-gap-audit.md) : appariement des deux rails par la MÊME
// fraction d'abscisse curviligne appliquée indépendamment à chacun. Dès que
// les rails divergent en longueur ou en courbure (virage, coude, largeur
// variable), cette hypothèse produit des fils non perpendiculaires au ruban
// (éventails, quasi-croisements, densité irrégulière) -- exactement le défaut
// signalé par l'utilisateur sur un ruban courbe/anguleux.
//
// Pour objectiver l'amélioration, une réplique fidèle (mais isolée, non
// utilisée en production) de l'ANCIEN algorithme est incluse ici à des fins de
// mesure comparative uniquement (voir `naive_fraction_pairs`).

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include "openstitch/stitch_generation/satin.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

namespace {

struct P {
    double x{0.0};
    double y{0.0};
};

P operator+(P a, P b) { return {a.x + b.x, a.y + b.y}; }
P operator-(P a, P b) { return {a.x - b.x, a.y - b.y}; }
P operator*(P a, double s) { return {a.x * s, a.y * s}; }
double dot(P a, P b) { return a.x * b.x + a.y * b.y; }
double cross(P a, P b) { return a.x * b.y - a.y * b.x; }
double norm(P a) { return std::sqrt(dot(a, a)); }
P unit(P a) {
    const double n = norm(a);
    return n > 1e-9 ? P{a.x / n, a.y / n} : P{0.0, 0.0};
}
P toP(Vec2um v) { return {static_cast<double>(v.x.value), static_cast<double>(v.y.value)}; }
Vec2um toUm(P p) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(p.x))},
                  Micrometers{static_cast<std::int32_t>(std::lround(p.y))}};
}

geometry::Path path_from(const std::vector<P>& pts) {
    geometry::Path path;
    path.closed = false;
    path.nodes.reserve(pts.size());
    for (const P& p : pts) {
        path.nodes.push_back({toUm(p), geometry::NodeType::Corner, std::nullopt, std::nullopt});
    }
    return path;
}

std::vector<P> to_pts(const geometry::Path& path) {
    std::vector<P> pts;
    pts.reserve(path.nodes.size());
    for (const auto& n : path.nodes) {
        pts.push_back(toP(n.pos));
    }
    return pts;
}

std::vector<double> cumulative(const std::vector<P>& pts) {
    std::vector<double> cum(pts.size(), 0.0);
    for (std::size_t i = 1; i < pts.size(); ++i) {
        cum[i] = cum[i - 1] + norm(pts[i] - pts[i - 1]);
    }
    return cum;
}

P point_at(const std::vector<P>& pts, const std::vector<double>& cum, double s) {
    if (pts.empty()) return {};
    if (s <= 0.0) return pts.front();
    if (s >= cum.back()) return pts.back();
    const auto it = std::upper_bound(cum.begin(), cum.end(), s);
    const std::size_t i = static_cast<std::size_t>(it - cum.begin());
    const double segLen = cum[i] - cum[i - 1];
    const double t = segLen > 0.0 ? (s - cum[i - 1]) / segLen : 0.0;
    return pts[i - 1] + (pts[i] - pts[i - 1]) * t;
}

// Abscisse curviligne du point de `pts` le plus proche de P (projection),
// réplique locale de la même primitive que satin.cpp (non exposée
// publiquement), utilisée ici uniquement pour ré-identifier après coup à quel
// rail appartient chaque extrémité d'un fil et mesurer la monotonie.
double project_arclen(const std::vector<P>& pts, const std::vector<double>& cum, P target) {
    double best = 1e30;
    double bestS = 0.0;
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        const P d = pts[i + 1] - pts[i];
        const double segLen2 = dot(d, d);
        double t = 0.0;
        if (segLen2 > 1e-9) {
            t = dot(target - pts[i], d) / segLen2;
            t = std::clamp(t, 0.0, 1.0);
        }
        const P proj = pts[i] + d * t;
        const double dd = norm(target - proj);
        if (dd < best) {
            best = dd;
            bestS = cum[i] + t * std::sqrt(segLen2);
        }
    }
    return bestS;
}

double dist_to_polyline(const std::vector<P>& pts, P target) {
    double best = 1e30;
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        const P d = pts[i + 1] - pts[i];
        const double segLen2 = dot(d, d);
        double t = 0.0;
        if (segLen2 > 1e-9) {
            t = std::clamp(dot(target - pts[i], d) / segLen2, 0.0, 1.0);
        }
        best = std::min(best, norm(target - (pts[i] + d * t)));
    }
    return best;
}

// Intersection PROPRE de deux segments (croisement transversal strict).
bool segments_cross(P a, P b, P c, P d) {
    const auto o = [](P p, P q, P r) { return cross(q - p, r - p); };
    const double o1 = o(a, b, c), o2 = o(a, b, d), o3 = o(c, d, a), o4 = o(c, d, b);
    return (o1 > 0) != (o2 > 0) && (o3 > 0) != (o4 > 0) && o1 != 0 && o2 != 0 && o3 != 0 && o4 != 0;
}

// Une passe de fils reconstruite depuis un SatinResult.satin brut : chaque fil
// est une paire de points consécutifs. Valable pour fill_satin (alternance
// A,B / B,A) et fill_satin_columns (toujours A,B) car un fil est un ensemble
// non-ordonné {P, Q} -- l'ordre d'émission n'affecte pas la géométrie mesurée.
std::vector<std::pair<P, P>> threads_from_satin(const std::vector<Vec2um>& satin) {
    std::vector<std::pair<P, P>> threads;
    for (std::size_t i = 0; i + 1 < satin.size(); i += 2) {
        threads.push_back({toP(satin[i]), toP(satin[i + 1])});
    }
    return threads;
}

// Réplique FIDÈLE (isolée, test uniquement) de l'ancien algorithme
// `fill_satin` : même fraction d'abscisse curviligne appliquée
// indépendamment à chaque rail. Sert de référence comparative pour objectiver
// l'amélioration -- jamais utilisée en production.
std::vector<std::pair<P, P>> naive_fraction_pairs(const geometry::Path& railA, const geometry::Path& railB,
                                                  double density) {
    const auto a = to_pts(railA);
    const auto b = to_pts(railB);
    const auto cumA = cumulative(a);
    const auto cumB = cumulative(b);
    const double lenA = cumA.back();
    const double lenB = cumB.back();
    const double columnLen = std::max(lenA, lenB);
    const int steps = std::max(1, static_cast<int>(std::ceil(columnLen / density)));
    std::vector<std::pair<P, P>> out;
    out.reserve(static_cast<std::size_t>(steps) + 1);
    for (int i = 0; i <= steps; ++i) {
        const double f = static_cast<double>(i) / steps;
        out.push_back({point_at(a, cumA, f * lenA), point_at(b, cumB, f * lenB)});
    }
    return out;
}

// Métriques géométriques d'une séquence de fils {A,B} par rapport aux deux
// rails d'origine.
struct PairingMetrics {
    int crossings_adjacent{0};       // fils consécutifs qui se croisent
    int crossings_total{0};          // toute paire de fils qui se croise
    bool monotoneA{true};            // abscisse sur le rail A non-decroissante
    bool monotoneB{true};            // abscisse sur le rail B non-decroissante
    double max_angle_to_normal_deg{0.0};  // pire écart fil/normale locale
    double mean_angle_to_normal_deg{0.0};
    double max_angular_jump_deg{0.0};     // pire saut de direction entre fils consecutifs
    double mean_spacing{0.0};
    double stddev_spacing{0.0};
    double min_length{1e30};
    double max_length{0.0};
    std::size_t thread_count{0};
};

PairingMetrics measure(const std::vector<std::pair<P, P>>& threads, const geometry::Path& railA,
                       const geometry::Path& railB, std::size_t skip_first = 0,
                       std::size_t skip_last = 0) {
    PairingMetrics m;
    m.thread_count = threads.size();
    if (threads.size() < 2) return m;

    const auto a = to_pts(railA);
    const auto b = to_pts(railB);
    const auto cumA = cumulative(a);
    const auto cumB = cumulative(b);

    // Ré-identifie, pour chaque fil, quelle extrémité appartient à quel rail
    // (indépendant de l'ordre d'émission), puis calcule sa et sb.
    std::vector<P> apoint(threads.size());
    std::vector<P> bpoint(threads.size());
    std::vector<double> sa(threads.size());
    std::vector<double> sb(threads.size());
    for (std::size_t k = 0; k < threads.size(); ++k) {
        const auto& [p, q] = threads[k];
        const double dpA = dist_to_polyline(a, p);
        const double dqA = dist_to_polyline(a, q);
        if (dpA <= dqA) {
            apoint[k] = p;
            bpoint[k] = q;
        } else {
            apoint[k] = q;
            bpoint[k] = p;
        }
        sa[k] = project_arclen(a, cumA, apoint[k]);
        sb[k] = project_arclen(b, cumB, bpoint[k]);
    }

    // Monotonie (tolérance résiduelle liée à l'arrondi micromètre entier).
    constexpr double kTol = 5.0;
    for (std::size_t k = 1; k < threads.size(); ++k) {
        if (sa[k] < sa[k - 1] - kTol) m.monotoneA = false;
        if (sb[k] < sb[k - 1] - kTol) m.monotoneB = false;
    }

    // Croisements.
    for (std::size_t k = 0; k + 1 < threads.size(); ++k) {
        if (segments_cross(threads[k].first, threads[k].second, threads[k + 1].first,
                           threads[k + 1].second)) {
            ++m.crossings_adjacent;
        }
    }
    for (std::size_t k = 0; k < threads.size(); ++k) {
        for (std::size_t j = k + 1; j < threads.size(); ++j) {
            if (segments_cross(threads[k].first, threads[k].second, threads[j].first, threads[j].second)) {
                ++m.crossings_total;
            }
        }
    }

    // Ligne médiane + tangente locale (différences centrées).
    std::vector<P> mid(threads.size());
    for (std::size_t k = 0; k < threads.size(); ++k) {
        mid[k] = (apoint[k] + bpoint[k]) * 0.5;
    }

    double sumAngle = 0.0;
    int nAngle = 0;
    double maxAngle = 0.0;
    const std::size_t lo = skip_first;
    const std::size_t hi = threads.size() > skip_last ? threads.size() - skip_last : threads.size();
    for (std::size_t k = lo; k < hi; ++k) {
        P tangent;
        if (k == 0) {
            tangent = mid[1] - mid[0];
        } else if (k + 1 == threads.size()) {
            tangent = mid[k] - mid[k - 1];
        } else {
            tangent = mid[k + 1] - mid[k - 1];
        }
        tangent = unit(tangent);
        if (norm(tangent) < 1e-9) continue;
        const P normal{-tangent.y, tangent.x};
        const P threadVec = unit(bpoint[k] - apoint[k]);
        if (norm(threadVec) < 1e-9) continue;
        double cosang = std::clamp(std::abs(dot(threadVec, normal)), -1.0, 1.0);
        const double angleDeg = std::acos(cosang) * 180.0 / std::numbers::pi;
        sumAngle += angleDeg;
        ++nAngle;
        maxAngle = std::max(maxAngle, angleDeg);
    }
    m.mean_angle_to_normal_deg = nAngle > 0 ? sumAngle / nAngle : 0.0;
    m.max_angle_to_normal_deg = maxAngle;

    // Continuité angulaire (direction non-orientée du fil, modulo 180°).
    double maxJump = 0.0;
    for (std::size_t k = lo + 1; k + 1 < hi; ++k) {
        const P v0 = unit(bpoint[k - 1] - apoint[k - 1]);
        const P v1 = unit(bpoint[k] - apoint[k]);
        if (norm(v0) < 1e-9 || norm(v1) < 1e-9) continue;
        double cosang = std::clamp(std::abs(dot(v0, v1)), -1.0, 1.0);
        const double angleDeg = std::acos(cosang) * 180.0 / std::numbers::pi;
        maxJump = std::max(maxJump, angleDeg);
    }
    m.max_angular_jump_deg = maxJump;

    // Espacement médian + longueur des fils.
    std::vector<double> spacing;
    for (std::size_t k = 1; k < mid.size(); ++k) {
        spacing.push_back(norm(mid[k] - mid[k - 1]));
    }
    double sumSp = 0.0;
    for (double s : spacing) sumSp += s;
    m.mean_spacing = spacing.empty() ? 0.0 : sumSp / static_cast<double>(spacing.size());
    double sqDiff = 0.0;
    for (double s : spacing) sqDiff += (s - m.mean_spacing) * (s - m.mean_spacing);
    m.stddev_spacing = spacing.empty() ? 0.0 : std::sqrt(sqDiff / static_cast<double>(spacing.size()));

    for (std::size_t k = 0; k < threads.size(); ++k) {
        const double len = norm(bpoint[k] - apoint[k]);
        m.min_length = std::min(m.min_length, len);
        m.max_length = std::max(m.max_length, len);
    }
    return m;
}

// --- Fixtures géométriques -----------------------------------------------

struct Ribbon {
    geometry::Path rail_a;
    geometry::Path rail_b;
};

// 1. Ruban droit : cas trivial, doit rester parfait.
Ribbon straight_ribbon() {
    std::vector<P> a, b;
    for (int i = 0; i <= 30; ++i) {
        const double x = i * 1000.0;
        a.push_back({x, 0.0});
        b.push_back({x, 4000.0});
    }
    return {path_from(a), path_from(b)};
}

// Construit un ruban par décalage perpendiculaire d'une ligne centrale
// paramétrique, largeur éventuellement variable (halfWidth(t)).
template <typename CenterFn, typename WidthFn>
Ribbon offset_ribbon(CenterFn centerAt, WidthFn halfWidthAt, int n) {
    std::vector<P> centerline(n + 1);
    for (int i = 0; i <= n; ++i) {
        centerline[static_cast<std::size_t>(i)] = centerAt(static_cast<double>(i) / n);
    }
    std::vector<P> a, b;
    for (int i = 0; i <= n; ++i) {
        P tangent;
        if (i == 0) {
            tangent = centerline[1] - centerline[0];
        } else if (i == n) {
            tangent = centerline[static_cast<std::size_t>(n)] - centerline[static_cast<std::size_t>(n - 1)];
        } else {
            tangent = centerline[static_cast<std::size_t>(i + 1)] - centerline[static_cast<std::size_t>(i - 1)];
        }
        tangent = unit(tangent);
        const P normal{-tangent.y, tangent.x};
        const double hw = halfWidthAt(static_cast<double>(i) / n);
        const P c = centerline[static_cast<std::size_t>(i)];
        a.push_back(c + normal * hw);
        b.push_back(c - normal * hw);
    }
    return {path_from(a), path_from(b)};
}

// 2. Ruban en S : une sinusoïde complète, largeur constante.
Ribbon s_curve_ribbon() {
    return offset_ribbon(
        [](double t) {
            const double x = t * 30000.0;
            const double y = 6000.0 * std::sin(t * 2.0 * std::numbers::pi);
            return P{x, y};
        },
        [](double) { return 2000.0; }, 60);
}

// 3. Coude a angle vif (~90 degres) : rail exterieur/interieur bien
// distincts, non auto-intersectants par construction.
Ribbon corner_ribbon() {
    std::vector<P> a{{0.0, 2000.0}, {16000.0, 2000.0}, {16000.0, 16000.0}};
    std::vector<P> b{{0.0, -2000.0}, {12000.0, -2000.0}, {12000.0, 16000.0}};
    return {path_from(a), path_from(b)};
}

// 4. Largeur variable, rails droits.
Ribbon variable_width_ribbon() {
    std::vector<P> a, b;
    for (int i = 0; i <= 20; ++i) {
        const double x = i * 1000.0;
        const double hw = 3000.0 - (2500.0 * i / 20.0);  // 6 mm -> 1 mm
        a.push_back({x, hw});
        b.push_back({x, -hw});
    }
    return {path_from(a), path_from(b)};
}

// 5. Cas inspiré de la capture : courbe + coude + largeur variable combinés
// (ruban en crochet avec un virage serré au milieu et un rétrécissement).
Ribbon hook_ribbon() {
    return offset_ribbon(
        [](double t) {
            if (t < 0.5) {
                // Quart de cercle (rayon 8 mm) : arrondi progressif.
                const double u = t / 0.5;
                const double ang = u * std::numbers::pi * 0.5;
                return P{8000.0 * std::sin(ang), 8000.0 * (1.0 - std::cos(ang))};
            }
            // Coude serre : repart en ligne droite dans une direction tres
            // differente de la tangente d'arrivee de l'arc (kink net).
            const double u = (t - 0.5) / 0.5;
            const P corner{8000.0, 8000.0};
            const P dir = unit(P{1.0, 1.2});
            return corner + dir * (u * 12000.0);
        },
        [](double t) { return 1800.0 - 1200.0 * t; }, 80);
}

// 6. Coude serre en "epingle a cheveux" (~170 degres, C1 continu) : deux
// branches quasi paralleles reliees par un demi-tour serre. Plus severe que
// corner_ribbon (qui est un seul virage a 90 degres) : verifie qu'aucun fil
// pres du demi-tour ne croise un fil NON adjacent situe plus loin sur une
// branche (le garde-fou de ladder_correspondence ne compare qu'a la diagonale
// PRECEDENTE -- un hairpin est le cas plausible ou une regression pourrait
// laisser passer un croisement non-adjacent).
Ribbon hairpin_ribbon() {
    return offset_ribbon(
        [](double t) {
            const double ang = t * (170.0 * std::numbers::pi / 180.0);
            const double r = 6000.0;
            return P{r * std::sin(ang), r * (1.0 - std::cos(ang))};
        },
        [](double) { return 1500.0; }, 100);
}

SatinConfig cfg_for(double density_um) {
    SatinConfig cfg;
    cfg.density = Micrometers{static_cast<std::int32_t>(density_um)};
    return cfg;
}

}  // namespace

// --- Tests -----------------------------------------------------------------

TEST_CASE("appariement satin : ruban droit -- cas trivial parfait") {
    const auto ribbon = straight_ribbon();
    const auto cfg = cfg_for(1000.0);
    const auto result = fill_satin(ribbon.rail_a, ribbon.rail_b, cfg);
    const auto m = measure(threads_from_satin(result.satin), ribbon.rail_a, ribbon.rail_b);
    CHECK(m.crossings_adjacent == 0);
    CHECK(m.crossings_total == 0);
    CHECK(m.monotoneA);
    CHECK(m.monotoneB);
    CHECK(m.max_angle_to_normal_deg < 1.0);
    CHECK(m.thread_count >= 28);
}

TEST_CASE("appariement satin : ruban en S -- pas d'eventail, correspondance locale") {
    const auto ribbon = s_curve_ribbon();
    const auto cfg = cfg_for(800.0);
    const auto result = fill_satin(ribbon.rail_a, ribbon.rail_b, cfg);
    const auto threads = threads_from_satin(result.satin);
    const auto m = measure(threads, ribbon.rail_a, ribbon.rail_b);
    const auto naive = naive_fraction_pairs(ribbon.rail_a, ribbon.rail_b, 800.0);
    const auto mNaive = measure(naive, ribbon.rail_a, ribbon.rail_b);

    WARN("S-curve : angle max nouveau=" << m.max_angle_to_normal_deg
                                        << " deg, ancien=" << mNaive.max_angle_to_normal_deg
                                        << " deg ; angle moyen nouveau=" << m.mean_angle_to_normal_deg
                                        << ", ancien=" << mNaive.mean_angle_to_normal_deg
                                        << " ; croisements nouveau=" << m.crossings_total
                                        << ", ancien=" << mNaive.crossings_total);

    CHECK(m.crossings_adjacent == 0);
    CHECK(m.crossings_total == 0);
    CHECK(m.monotoneA);
    CHECK(m.monotoneB);
    CHECK(m.max_angle_to_normal_deg < 20.0);
    CHECK(m.max_angular_jump_deg < 15.0);
    CHECK(m.stddev_spacing / m.mean_spacing < 0.35);
    // Amelioration mesuree par rapport a l'ancien algorithme (marge large).
    CHECK(m.max_angle_to_normal_deg < mNaive.max_angle_to_normal_deg);
}

TEST_CASE("appariement satin : coude a angle vif -- pas de croisement, densite bornee") {
    // Coude a 90 degres franc (C0, sans conge) : au point exact du coude, la
    // notion meme de "normale locale" est discontinue -- AUCUN algorithme
    // d'appariement ne peut y produire un fil parfaitement perpendiculaire
    // (un seul fil "absorbe" la discontinuite ; un vrai conge de coude releve
    // de ShortStitchMode/mitre, hors perimetre de l'appariement de base). Ce
    // qui doit rester vrai malgre le coude : AUCUN croisement, correspondance
    // monotone sur les deux rails, et une deviation moyenne bornee (le defaut
    // observe par l'utilisateur est un eventail etendu sur plusieurs fils,
    // pas un unique point dur).
    const auto ribbon = corner_ribbon();
    const auto cfg = cfg_for(1000.0);
    const auto result = fill_satin(ribbon.rail_a, ribbon.rail_b, cfg);
    const auto threads = threads_from_satin(result.satin);
    const auto m = measure(threads, ribbon.rail_a, ribbon.rail_b);
    const auto naive = naive_fraction_pairs(ribbon.rail_a, ribbon.rail_b, 1000.0);
    const auto mNaive = measure(naive, ribbon.rail_a, ribbon.rail_b);

    WARN("Coude : angle max nouveau=" << m.max_angle_to_normal_deg
                                      << " deg, ancien=" << mNaive.max_angle_to_normal_deg
                                      << " deg ; angle moyen nouveau=" << m.mean_angle_to_normal_deg
                                      << ", ancien=" << mNaive.mean_angle_to_normal_deg
                                      << " ; croisements nouveau=" << m.crossings_total
                                      << ", ancien=" << mNaive.crossings_total);

    CHECK(m.crossings_adjacent == 0);
    CHECK(m.crossings_total == 0);
    CHECK(m.monotoneA);
    CHECK(m.monotoneB);
    // Deviation MOYENNE bornee (l'eventail etendu est corrige) ; le pire fil
    // (au coude meme) reste borne et strictement meilleur que l'ancien.
    CHECK(m.mean_angle_to_normal_deg < 15.0);
    CHECK(m.max_angle_to_normal_deg < mNaive.max_angle_to_normal_deg);
}

TEST_CASE("appariement satin : largeur variable -- pas de fils degeneres") {
    const auto ribbon = variable_width_ribbon();
    const auto cfg = cfg_for(700.0);
    const auto result = fill_satin(ribbon.rail_a, ribbon.rail_b, cfg);
    const auto m = measure(threads_from_satin(result.satin), ribbon.rail_a, ribbon.rail_b);
    CHECK(m.crossings_adjacent == 0);
    CHECK(m.crossings_total == 0);
    CHECK(m.monotoneA);
    CHECK(m.monotoneB);
    CHECK(m.max_angle_to_normal_deg < 5.0);  // rails droits : normale constante
    CHECK(m.min_length > 500.0);             // jamais degenere (largeur mini ~1mm)
    CHECK(m.max_length < 6100.0);
}

TEST_CASE("appariement satin : cas inspire de la capture -- courbe+coude+largeur") {
    const auto ribbon = hook_ribbon();
    const auto cfg = cfg_for(700.0);
    const auto result = fill_satin(ribbon.rail_a, ribbon.rail_b, cfg);
    const auto threads = threads_from_satin(result.satin);
    const auto m = measure(threads, ribbon.rail_a, ribbon.rail_b);
    const auto naive = naive_fraction_pairs(ribbon.rail_a, ribbon.rail_b, 700.0);
    const auto mNaive = measure(naive, ribbon.rail_a, ribbon.rail_b);

    WARN("Crochet (capture) : angle max nouveau=" << m.max_angle_to_normal_deg
                                                   << " deg, ancien=" << mNaive.max_angle_to_normal_deg
                                                   << " deg ; angle moyen nouveau="
                                                   << m.mean_angle_to_normal_deg
                                                   << ", ancien=" << mNaive.mean_angle_to_normal_deg
                                                   << " ; croisements nouveau=" << m.crossings_total
                                                   << ", ancien=" << mNaive.crossings_total
                                                   << " ; saut angulaire max nouveau="
                                                   << m.max_angular_jump_deg);

    CHECK(m.crossings_adjacent == 0);
    CHECK(m.crossings_total == 0);
    CHECK(m.monotoneA);
    CHECK(m.monotoneB);
    CHECK(m.max_angle_to_normal_deg < 35.0);
    CHECK(m.max_angle_to_normal_deg < mNaive.max_angle_to_normal_deg);
    CHECK(m.max_angular_jump_deg < 25.0);
}

TEST_CASE("appariement satin : correspondance par barreaux -- memes metriques (auto-satin)") {
    // Meme fixture "crochet" (courbe+coude+largeur), mais route via
    // fill_satin_columns avec des barreaux (chemin de l'auto-satin, cf.
    // build_satin_columns qui pose des barreaux aux virages/changements de
    // largeur) : doit beneficier de la meme correction que le chemin manuel.
    // Barreaux pris aux index synchronises de offset_ribbon (rail_a.nodes[i]
    // et rail_b.nodes[i] partagent la meme station de la centerline -> ce
    // sont des sections transversales valides, comme dans build_column).
    const auto ribbon = hook_ribbon();
    const std::vector<int> stationIdx{0, 20, 40, 60, 80};
    std::vector<SatinRungSeg> rungs;
    for (int i : stationIdx) {
        rungs.push_back({ribbon.rail_a.nodes[static_cast<std::size_t>(i)].pos,
                         ribbon.rail_b.nodes[static_cast<std::size_t>(i)].pos});
    }
    const auto cfg = cfg_for(700.0);
    const auto result = fill_satin_columns(ribbon.rail_a, ribbon.rail_b, rungs, cfg);
    const auto m = measure(threads_from_satin(result.satin), ribbon.rail_a, ribbon.rail_b);
    CHECK(m.crossings_adjacent == 0);
    CHECK(m.crossings_total == 0);
    CHECK(m.monotoneA);
    CHECK(m.monotoneB);
    CHECK(m.max_angle_to_normal_deg < 35.0);
}

TEST_CASE("appariement satin : determinisme sur toutes les fixtures") {
    const std::vector<Ribbon> ribbons{straight_ribbon(), s_curve_ribbon(), corner_ribbon(),
                                      variable_width_ribbon(), hook_ribbon()};
    const auto cfg = cfg_for(750.0);
    for (const auto& r : ribbons) {
        const auto r1 = fill_satin(r.rail_a, r.rail_b, cfg);
        const auto r2 = fill_satin(r.rail_a, r.rail_b, cfg);
        CHECK(r1.satin == r2.satin);
    }
}

TEST_CASE("appariement satin : echantillonnage asymetrique et segments nuls") {
    // Même ruban droit, mais le rail A est sur-échantillonné et contient des
    // doublons tandis que le rail B ne comporte que ses extrémités. Ces
    // segments de longueur nulle apparaissent après simplification/import et
    // ne doivent ni casser la monotonie ni produire de pénétration dégénérée.
    const auto railA = path_from({{0, 0}, {0, 0}, {500, 0}, {1000, 0}, {1000, 0},
                                  {4000, 0}, {9000, 0}, {15000, 0}, {20000, 0}});
    const auto railB = path_from({{0, 4000}, {20000, 4000}});
    const auto cfg = cfg_for(650.0);

    const auto first = fill_satin(railA, railB, cfg);
    const auto second = fill_satin(railA, railB, cfg);
    const auto m = measure(threads_from_satin(first.satin), railA, railB);

    CHECK(first.satin == second.satin);
    CHECK(m.crossings_adjacent == 0);
    CHECK(m.crossings_total == 0);
    CHECK(m.monotoneA);
    CHECK(m.monotoneB);
    CHECK(m.min_length > 3900.0);
    CHECK(m.max_length < 4100.0);
}

TEST_CASE("appariement satin : rails tete-beche -- normalises, pas de noeud papillon") {
    // Meme ruban droit, mais rail_b est fourni dans le sens OPPOSE de rail_a
    // (bout 0 de A pres du bout N de B). C'est le contrat viole : le
    // commentaire de fill_satin exige des rails "orientes dans le meme sens".
    // Avant normalisation, ladder_correspondence demarre sur l'ancre
    // (A.front(), B.front()=B "physiquement a l'autre bout") -- un noeud
    // papillon ou pratiquement tous les fils se croisent pres du centre.
    // opposite_orientation() doit detecter ce cas et retourner b en interne.
    const auto railA = path_from({{0, 0}, {5000, 0}, {10000, 0}, {15000, 0}, {20000, 0}});
    const auto railB = path_from({{20000, 4000}, {15000, 4000}, {10000, 4000}, {5000, 4000}, {0, 4000}});
    const auto cfg = cfg_for(1000.0);

    const auto result = fill_satin(railA, railB, cfg);
    const auto second = fill_satin(railA, railB, cfg);
    const auto m = measure(threads_from_satin(result.satin), railA, railB);

    CHECK(result.satin == second.satin);
    CHECK(m.crossings_adjacent == 0);
    CHECK(m.crossings_total == 0);
    CHECK(m.max_angle_to_normal_deg < 1.0);
    CHECK(m.min_length > 3900.0);
    CHECK(m.max_length < 4100.0);
}

TEST_CASE("appariement satin : longueurs tres differentes + largeur quasi nulle") {
    // Rail A long (30 mm) et tres courbe, rail B court (4 mm) et quasi droit,
    // les deux se rapprochant a moins de 10 um a une extremite (largeur quasi
    // nulle) -- combinaison de deux cas limites du meme coup : ratio de
    // longueur extreme (les grilles fine_s_grid des deux rails ont des
    // nombres de pas tres differents) et division potentielle par une largeur
    // proche de zero (compensate()/pull, gardees par un epsilon dans le code
    // de production).
    std::vector<P> a;
    for (int i = 0; i <= 60; ++i) {
        const double t = static_cast<double>(i) / 60.0;
        const double ang = t * std::numbers::pi * 0.5;
        a.push_back({30000.0 * std::sin(ang), 30000.0 * (1.0 - std::cos(ang))});
    }
    const std::vector<P> b{{0.0, 5.0}, {2000.0, 5.0}, {4000.0, 5.0}};
    const auto railA = path_from(a);
    const auto railB = path_from(b);
    const auto cfg = cfg_for(600.0);

    const auto result = fill_satin(railA, railB, cfg);
    const auto second = fill_satin(railA, railB, cfg);
    const auto m = measure(threads_from_satin(result.satin), railA, railB);

    CHECK(result.satin == second.satin);
    CHECK(m.crossings_adjacent == 0);
    CHECK(m.crossings_total == 0);
    CHECK(m.monotoneA);
    CHECK(m.monotoneB);
    // Aucun point degenere malgre la largeur quasi nulle sur B : chaque fil
    // garde une longueur finie non-NaN.
    for (const auto& t : threads_from_satin(result.satin)) {
        CHECK(std::isfinite(t.first.x));
        CHECK(std::isfinite(t.first.y));
        CHECK(std::isfinite(t.second.x));
        CHECK(std::isfinite(t.second.y));
    }
}

TEST_CASE("appariement satin : epingle a cheveux -- pas de croisement non adjacent") {
    const auto ribbon = hairpin_ribbon();
    const auto cfg = cfg_for(700.0);
    const auto result = fill_satin(ribbon.rail_a, ribbon.rail_b, cfg);
    const auto threads = threads_from_satin(result.satin);
    const auto m = measure(threads, ribbon.rail_a, ribbon.rail_b);

    CHECK(m.crossings_adjacent == 0);
    CHECK(m.crossings_total == 0);
    CHECK(m.monotoneA);
    CHECK(m.monotoneB);
}

TEST_CASE("appariement satin : barreaux desordonnes -- ordre du vecteur sans effet") {
    // Meme fixture "crochet" et memes barreaux que le test barreaux existant,
    // mais MELANGES dans le vecteur d'entree (ordre non trie le long de la
    // colonne). Un barreau est une station transversale, pas un element de
    // sequence : le resultat doit etre identique a la version triee, pas un
    // repli silencieux sur fill_satin (qui produirait un resultat different,
    // sans barreaux traverses exactement).
    const auto ribbon = hook_ribbon();
    const auto cfg = cfg_for(700.0);
    const auto rungAt = [&](int i) {
        return SatinRungSeg{ribbon.rail_a.nodes[static_cast<std::size_t>(i)].pos,
                            ribbon.rail_b.nodes[static_cast<std::size_t>(i)].pos};
    };
    const std::vector<SatinRungSeg> sorted{rungAt(0), rungAt(20), rungAt(40), rungAt(60), rungAt(80)};
    const std::vector<SatinRungSeg> shuffled{rungAt(60), rungAt(0), rungAt(80), rungAt(20), rungAt(40)};

    const auto resultSorted = fill_satin_columns(ribbon.rail_a, ribbon.rail_b, sorted, cfg);
    const auto resultShuffled = fill_satin_columns(ribbon.rail_a, ribbon.rail_b, shuffled, cfg);

    CHECK(resultSorted.satin == resultShuffled.satin);
    CHECK(resultSorted.satin.size() > 10);  // pas le repli fill_satin (barreaux ignores)

    const auto m = measure(threads_from_satin(resultShuffled.satin), ribbon.rail_a, ribbon.rail_b);
    CHECK(m.crossings_adjacent == 0);
    CHECK(m.crossings_total == 0);
    CHECK(m.monotoneA);
    CHECK(m.monotoneB);
}

TEST_CASE("appariement satin : barreau duplique/quasi-duplique -- filtre sans crash") {
    // Deux barreaux projetes a (quasi) la meme station sur les deux rails :
    // le garde-fou "strictement croissant" doit en ecarter un sans produire
    // d'intervalle degenere (division par une longueur nulle) ni de fil
    // NaN/infini.
    const auto ribbon = hook_ribbon();
    const auto cfg = cfg_for(700.0);
    const auto rungAt = [&](int i) {
        return SatinRungSeg{ribbon.rail_a.nodes[static_cast<std::size_t>(i)].pos,
                            ribbon.rail_b.nodes[static_cast<std::size_t>(i)].pos};
    };
    const std::vector<SatinRungSeg> rungs{rungAt(0), rungAt(40), rungAt(40), rungAt(41), rungAt(80)};

    const auto result = fill_satin_columns(ribbon.rail_a, ribbon.rail_b, rungs, cfg);
    const auto second = fill_satin_columns(ribbon.rail_a, ribbon.rail_b, rungs, cfg);
    CHECK(result.satin == second.satin);
    // Le ruban tient dans [-2000, 20000] x [-2000, 28000] um ; un intervalle
    // degenere (division par une longueur de station quasi nulle) produirait
    // des coordonnees demesurees plutot que NaN (converties en int32 par
    // std::lround), donc une simple borne large suffit a le detecter.
    for (const auto& v : result.satin) {
        CHECK(std::abs(v.x.value) < 100'000);
        CHECK(std::abs(v.y.value) < 100'000);
    }
    const auto m = measure(threads_from_satin(result.satin), ribbon.rail_a, ribbon.rail_b);
    CHECK(m.crossings_adjacent == 0);
    CHECK(m.crossings_total == 0);
    CHECK(m.monotoneA);
    CHECK(m.monotoneB);
}
