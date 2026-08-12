// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/satin.hpp"

#include "openstitch/geometry/polyline.hpp"

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

// Aplatit `path` avant de le convertir en points de travail : un rail
// paramétrique (§ refonte auto-satin) est une courbe de Bézier ÉPARSE
// (quelques `PathNode` à tangentes `tan_in`/`tan_out`), jamais une
// polyligne dense — la lire nœud par nœud sans passer par
// `geometry::flatten` ignorerait purement et simplement la courbure et
// reconnecterait les poignées de contrôle par des cordes. Un rail SANS
// tangente (legacy, `rails_from_contour`, saisie manuelle) traverse
// `flatten` inchangé (chaque segment reste une droite), donc ce changement
// est un sur-ensemble strict du comportement précédent.
constexpr Micrometers kRailFlattenTolerance{30};  // 0,03 mm : sous la résolution DST (0,1 mm)

std::vector<PointD> to_points(const geometry::Path& path) {
    const auto flat = geometry::flatten(path, kRailFlattenTolerance);
    std::vector<PointD> pts;
    pts.reserve(flat.points.size());
    for (const auto& p : flat.points) {
        pts.push_back(toD(p));
    }
    return pts;
}

// Contrat de fill_satin/fill_satin_columns : les deux rails doivent être
// orientés dans le même sens (bout 0 <-> bout 0, bout N <-> bout N). Fourni
// tête-bêche, `ladder_correspondence` produit un nœud papillon (chaque fil
// tend vers la diagonale opposée, croisements en O(n²)) au lieu d'un ruban.
// `rails_from_contour` et `auto_satin::build_satin_columns` garantissent déjà
// ce sens ; ce recalage protège les appelants futurs (import, script, saisie
// manuelle) qui ne le garantiraient pas. Heuristique bon marché plutôt qu'une
// garantie géométrique générale : les deux bouts d'un ruban valide sont par
// construction proches d'un côté ou de l'autre, donc comparer les deux
// appariements bout-à-bout possibles suffit à détecter l'inversion.
bool opposite_orientation(const std::vector<PointD>& a, const std::vector<PointD>& b) {
    const double sameOrder = dist(a.front(), b.front()) + dist(a.back(), b.back());
    const double reversed = dist(a.front(), b.back()) + dist(a.back(), b.front());
    return reversed < sameOrder;
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

// --- Correspondance locale rail A <-> rail B (appariement, cf. audit satin) ---
//
// Défaut corrigé ici : l'ancien code appariait les deux rails par la MÊME
// fraction d'abscisse curviligne appliquée indépendamment à chacun (ou, avec
// barreaux, la même fraction `u` sur tout un intervalle entre deux barreaux).
// Dès que les deux rails ont des longueurs/courbures différentes dans un
// virage, cette hypothèse de proportionnalité est fausse : les fils cessent
// d'être localement perpendiculaires au ruban (éventails, quasi-croisements,
// densité visuelle irrégulière). Remplacé par une correspondance "ladder"
// (triangulation de bande par diagonale la plus courte, technique classique
// indépendante de tout logiciel de broderie) : monotone par construction (les
// indices n'avancent jamais en arrière -> jamais d'inversion/croisement
// structurel), stable aux différences d'échantillonnage des deux rails,
// O(nA + nB) par intervalle (linéaire).

double cross2(PointD o, PointD a, PointD b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

// Intersection PROPRE de deux segments (croisement transversal strict).
bool segments_cross_strict(PointD a, PointD b, PointD c, PointD d) {
    const double o1 = cross2(a, b, c);
    const double o2 = cross2(a, b, d);
    const double o3 = cross2(c, d, a);
    const double o4 = cross2(c, d, b);
    return (o1 > 0) != (o2 > 0) && (o3 > 0) != (o4 > 0) && o1 != 0 && o2 != 0 && o3 != 0 && o4 != 0;
}

// Un point de correspondance locale rail A <-> rail B (une "diagonale").
struct CorrespondencePoint {
    double sa{0.0};
    double sb{0.0};
    PointD pa{};
    PointD pb{};
};

// Grille d'abscisses entre s0 et s1, résolution bornée par `maxStep` : donne à
// l'algorithme de correspondance assez de détail même si les sommets d'origine
// du rail sont épars (rail dessiné à la main, contour simplifié).
std::vector<double> fine_s_grid(double s0, double s1, double maxStep) {
    const double len = s1 - s0;
    if (len <= 1e-9) {
        return {s0, s1};
    }
    const int n = std::max(1, static_cast<int>(std::ceil(len / std::max(1e-6, maxStep))));
    std::vector<double> ss;
    ss.reserve(static_cast<std::size_t>(n) + 1);
    for (int i = 0; i <= n; ++i) {
        ss.push_back(s0 + len * static_cast<double>(i) / n);
    }
    return ss;
}

// Correspondance locale monotone entre deux rails sur un intervalle borné par
// deux ancres EXACTES (barreaux ou extrémités de colonne). À chaque pas,
// avance sur le rail dont le prochain sommet forme la diagonale la plus
// courte avec le point courant de l'autre rail (ties : alterne, pour ne pas
// épuiser un rail avant l'autre sur un cas symétrique). Garde-fou : si
// l'avance choisie croiserait la diagonale précédente, tente l'autre côté.
std::vector<CorrespondencePoint> ladder_correspondence(const std::vector<PointD>& a,
                                                       const std::vector<double>& cumA, double sa0,
                                                       PointD pa0, double sa1, PointD pa1,
                                                       const std::vector<PointD>& b,
                                                       const std::vector<double>& cumB, double sb0,
                                                       PointD pb0, double sb1, PointD pb1,
                                                       double maxStep) {
    const auto ssa = fine_s_grid(sa0, sa1, maxStep);
    const auto ssb = fine_s_grid(sb0, sb1, maxStep);
    std::vector<PointD> chainA(ssa.size());
    std::vector<PointD> chainB(ssb.size());
    for (std::size_t i = 0; i < ssa.size(); ++i) {
        chainA[i] = (i == 0) ? pa0 : (i + 1 == ssa.size() ? pa1 : point_at(a, cumA, ssa[i]));
    }
    for (std::size_t i = 0; i < ssb.size(); ++i) {
        chainB[i] = (i == 0) ? pb0 : (i + 1 == ssb.size() ? pb1 : point_at(b, cumB, ssb[i]));
    }
    const std::size_t lastA = chainA.size() - 1;
    const std::size_t lastB = chainB.size() - 1;
    std::vector<CorrespondencePoint> out;
    out.reserve(lastA + lastB + 1);
    out.push_back({ssa[0], ssb[0], chainA[0], chainB[0]});
    std::size_t ia = 0;
    std::size_t ib = 0;
    bool lastWasA = true;
    while (ia < lastA || ib < lastB) {
        const bool canA = ia < lastA;
        const bool canB = ib < lastB;
        bool advanceA;
        if (canA && canB) {
            const double diagA = dist(chainA[ia + 1], chainB[ib]);
            const double diagB = dist(chainA[ia], chainB[ib + 1]);
            const double eps = 1e-6 * std::max({1.0, diagA, diagB});
            advanceA = std::abs(diagA - diagB) < eps ? !lastWasA : diagA < diagB;
            const CorrespondencePoint& prev = out.back();
            if (advanceA && segments_cross_strict(prev.pa, prev.pb, chainA[ia + 1], chainB[ib]) &&
                !segments_cross_strict(prev.pa, prev.pb, chainA[ia], chainB[ib + 1])) {
                advanceA = false;
            } else if (!advanceA && segments_cross_strict(prev.pa, prev.pb, chainA[ia], chainB[ib + 1]) &&
                      !segments_cross_strict(prev.pa, prev.pb, chainA[ia + 1], chainB[ib])) {
                advanceA = true;
            }
        } else {
            advanceA = canA;
        }
        if (advanceA) {
            ++ia;
        } else {
            ++ib;
        }
        lastWasA = advanceA;
        out.push_back({ssa[ia], ssb[ib], chainA[ia], chainB[ib]});
    }
    return out;
}

// Ré-échantillonne une correspondance locale par pas fixe le long de sa ligne
// médiane (~perpendiculaire aux fils) : (sa, sb) sont interpolés dans le PAS
// LOCAL du ladder qui encadre la cible, jamais sur tout l'intervalle -> erreur
// bornée par la résolution du ladder (maxStep), pas par la longueur totale de
// l'intervalle. `ladder` doit contenir au moins un point.
std::vector<CorrespondencePoint> resample_by_medial_spacing(const std::vector<CorrespondencePoint>& ladder,
                                                             const std::vector<PointD>& a,
                                                             const std::vector<double>& cumA,
                                                             const std::vector<PointD>& b,
                                                             const std::vector<double>& cumB,
                                                             double spacing) {
    std::vector<double> cumM(ladder.size(), 0.0);
    for (std::size_t i = 1; i < ladder.size(); ++i) {
        cumM[i] =
            cumM[i - 1] + dist(midP(ladder[i - 1].pa, ladder[i - 1].pb), midP(ladder[i].pa, ladder[i].pb));
    }
    const double total = cumM.back();
    const int n = std::max(1, static_cast<int>(std::lround(total / spacing)));
    const double step = total / n;
    std::vector<CorrespondencePoint> out;
    out.reserve(static_cast<std::size_t>(n) + 1);
    out.push_back(ladder.front());
    for (int jj = 1; jj < n; ++jj) {
        const double target = jj * step;
        const auto it = std::lower_bound(cumM.begin(), cumM.end(), target);
        const std::size_t idx =
            std::min<std::size_t>(static_cast<std::size_t>(it - cumM.begin()), ladder.size() - 1);
        const std::size_t lo = idx == 0 ? 0 : idx - 1;
        const double segLen = cumM[idx] - cumM[lo];
        const double f = segLen > 1e-9 ? (target - cumM[lo]) / segLen : 0.0;
        const double sa = ladder[lo].sa + (ladder[idx].sa - ladder[lo].sa) * f;
        const double sb = ladder[lo].sb + (ladder[idx].sb - ladder[lo].sb) * f;
        out.push_back({sa, sb, point_at(a, cumA, sa), point_at(b, cumB, sb)});
    }
    out.push_back(ladder.back());
    return out;
}

// Fil satin : deux extrémités + drapeau « barreau » (ne pas retirer/altérer).
struct Thread {
    PointD a;
    PointD b;
    bool anchor{false};
    bool dropped{false};
    bool jump_before{false};  // atteint par un saut, pas un point continu depuis le fil précédent
};

// Écart directionnel entre deux barreaux consécutifs : au-delà de
// `kJumpCosThreshold` (~75°) entre les deux vecteurs d'avance rail A / rail B,
// la colonne ne se comporte plus comme un ruban sur cet intervalle -- les deux
// rails ne progressent plus dans une direction globalement commune (coude
// serré, jonction mal placée : cf. audit lettres, région d'une lettre en T où
// un barreau proche de la pointe d'un coin est immédiatement suivi d'un
// barreau sur le bord perpendiculaire). Un ruban qui rétrécit/s'évase
// (terminaison en pointe normale) garde des directions colinéaires -- seul un
// changement de direction franc déclenche le saut, donc aucun faux positif
// sur une terminaison effilée ordinaire. `kJumpMinSpan` (µm) évite de
// déclencher sur du bruit sous-résolution entre deux barreaux très proches.
constexpr double kJumpMinSpan = 800.0;
constexpr double kJumpCosThreshold = 0.258819045102521;  // cos(75°)

bool non_ribbon_interval(PointD a0, PointD a1, PointD b0, PointD b1) {
    const double dirAx = a1.x - a0.x, dirAy = a1.y - a0.y;
    const double dirBx = b1.x - b0.x, dirBy = b1.y - b0.y;
    const double lenA = std::hypot(dirAx, dirAy);
    const double lenB = std::hypot(dirBx, dirBy);
    if (lenA < kJumpMinSpan || lenB < kJumpMinSpan) {
        return false;
    }
    const double cosTheta = (dirAx * dirBx + dirAy * dirBy) / (lenA * lenB);
    return cosTheta < kJumpCosThreshold;
}

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
    auto b = to_points(rail_b);
    if (a.size() < 2 || b.size() < 2) {
        return result;
    }
    if (opposite_orientation(a, b)) {
        std::reverse(b.begin(), b.end());
    }
    const auto cumA = cumulative(a);
    const auto cumB = cumulative(b);
    const double density = static_cast<double>(std::max<std::int32_t>(1, config.density.value));
    const double comp = static_cast<double>(config.pull_compensation.value);

    // Ancres = barreaux projetés sur les deux rails (abscisses curvilignes).
    // Un barreau est une station transversale, pas un élément de séquence :
    // `rungs` n'est pas garanti trié par l'appelant (édition manuelle future,
    // import). Trié ici par station projetée AVANT le filtre anti-croisement
    // -- sans ce tri, un rung placé en tête de vecteur mais loin le long de la
    // colonne verrouillait `anchors.back()` et faisait rejeter silencieusement
    // tous les rungs suivants (repli sur `fill_satin` sans barreaux, alors que
    // la doc promet des barreaux toujours traversés exactement).
    //
    // Après tri, deux ancres gardées doivent avancer d'au moins `anchorMinGap`
    // (la moitié du pas de densité) sur LES DEUX rails, pas juste `> 0` : un écart
    // non nul mais minuscule (barreaux quasi-dupliqués, ou dupliqués après
    // arrondi micromètre) crée un intervalle dégénéré entre deux ancres —
    // aucun point ré-échantillonné n'y trouve place
    // (resample_by_medial_spacing replie alors sur les deux seules bornes), et
    // les deux fils-ancres consécutifs qui l'encadrent se retrouvent soudés
    // sans le filtrage habituel de `ladder_correspondence` (le garde-fou anti-
    // croisement n'agit qu'À L'INTÉRIEUR d'un intervalle, jamais entre deux
    // intervalles adjacents). Si ce point dégénéré tombe près d'un virage
    // serré, les deux fils-ancres peuvent alors se croiser (reproduit par le
    // test "barreau dupliqué/quasi-dupliqué"). Fusionner (garder le premier
    // rencontré après tri, donc le plus proche du début de colonne) élimine
    // la cause plutôt que le symptôme.
    struct Anchor {
        double sa{0.0};
        double sb{0.0};
        PointD pa{};
        PointD pb{};
    };
    std::vector<Anchor> anchors;
    anchors.reserve(rungs.size());
    for (const auto& r : rungs) {
        Anchor an;
        an.pa = toD(r.first);
        an.pb = toD(r.second);
        an.sa = project_arclen(a, cumA, an.pa);
        an.sb = project_arclen(b, cumB, an.pb);
        anchors.push_back(an);
    }
    std::stable_sort(anchors.begin(), anchors.end(),
                     [](const Anchor& x, const Anchor& y) { return x.sa + x.sb < y.sa + y.sb; });
    const double anchorMinGap = density * 0.5;
    std::vector<Anchor> kept;
    kept.reserve(anchors.size());
    for (const auto& an : anchors) {
        if (kept.empty() ||
            (an.sa > kept.back().sa + anchorMinGap && an.sb > kept.back().sb + anchorMinGap)) {
            kept.push_back(an);
        }
    }
    anchors = std::move(kept);
    if (anchors.size() < 2) {
        return fill_satin(rail_a, rail_b, config);
    }

    // Fils : correspondance locale monotone (ladder, voir plus haut) entre
    // barreaux consécutifs, ré-échantillonnée par pas fixe sur la ligne
    // médiane (≈ perpendiculaire aux fils). Remplace l'ancienne interpolation
    // par fraction d'abscisse UNIQUE sur tout l'intervalle (source
    // d'éventails/quasi-croisements en virage).
    std::vector<Thread> threads;
    const double maxStep = std::clamp(density / 4.0, 100.0, 500.0);
    threads.push_back({anchors.front().pa, anchors.front().pb, true, false, false});
    for (std::size_t k = 0; k + 1 < anchors.size(); ++k) {
        const Anchor& a0 = anchors[k];
        const Anchor& a1 = anchors[k + 1];
        if (non_ribbon_interval(a0.pa, a1.pa, a0.pb, a1.pb)) {
            // Saut : aucun fil intermédiaire n'a de sens géométrique sur cet
            // intervalle (les deux rails divergent trop pour former un
            // ruban) -- le barreau suivant devient le point de reprise.
            threads.push_back({a1.pa, a1.pb, true, false, true});
            continue;
        }
        const auto ladder = ladder_correspondence(a, cumA, a0.sa, a0.pa, a1.sa, a1.pa, b, cumB, a0.sb,
                                                   a0.pb, a1.sb, a1.pb, maxStep);
        const auto resampled = resample_by_medial_spacing(ladder, a, cumA, b, cumB, density);
        for (std::size_t j = 1; j + 1 < resampled.size(); ++j) {
            threads.push_back({resampled[j].pa, resampled[j].pb, false, false, false});
        }
        threads.push_back({a1.pa, a1.pb, true, false, false});  // barreau traversé exactement
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

    // --- Push (§11) : étend/rétracte la colonne aux extrémités le long de l'axe. ---
    const auto shiftEnd = [&](int idx, int nb, double amount) {
        if (amount == 0.0 || idx < 0 || nb < 0 || idx >= nThreads || nb >= nThreads) {
            return;
        }
        auto& t = threads[static_cast<std::size_t>(idx)];
        const PointD mi = midP(t.a, t.b);
        const PointD mn = midP(threads[static_cast<std::size_t>(nb)].a,
                              threads[static_cast<std::size_t>(nb)].b);
        const double n = dist(mi, mn);
        if (n < 1e-6) {
            return;
        }
        // Une rétraction (amount < 0) ne doit jamais dépasser la station
        // voisine : au-delà, le premier (ou dernier) fil de la colonne se
        // retrouverait de l'AUTRE côté de son voisin, inversant leur ordre le
        // long de l'axe et produisant un point auto-croisé en tout début/fin
        // de colonne — défaut trouvé par revue (contrairement à la
        // compensation pull, bornée par `pull_max`, push n'avait aucune
        // borne). Une extension (amount > 0) reste volontairement non bornée :
        // elle éloigne le fil de la colonne sans jamais croiser un autre point.
        const double clamped = std::max(amount, -(n - 1.0));
        const PointD d{(mi.x - mn.x) / n * clamped, (mi.y - mn.y) / n * clamped};
        t.a = {t.a.x + d.x, t.a.y + d.y};
        t.b = {t.b.x + d.x, t.b.y + d.y};
    };
    shiftEnd(0, 1, static_cast<double>(config.push_start.value));
    shiftEnd(nThreads - 1, nThreads - 2, static_cast<double>(config.push_end.value));

    // Milieux + retrait aux extrémités (pour center/edge underlay).
    std::vector<PointD> mids(static_cast<std::size_t>(nThreads));
    for (int i = 0; i < nThreads; ++i) {
        mids[static_cast<std::size_t>(i)] = midP(threads[static_cast<std::size_t>(i)].a,
                                                 threads[static_cast<std::size_t>(i)].b);
    }
    std::vector<double> cumMid(static_cast<std::size_t>(nThreads), 0.0);
    for (int i = 1; i < nThreads; ++i) {
        cumMid[static_cast<std::size_t>(i)] =
            cumMid[static_cast<std::size_t>(i - 1)] +
            dist(mids[static_cast<std::size_t>(i)], mids[static_cast<std::size_t>(i - 1)]);
    }
    const double totalMid = cumMid.back();
    const double retract = static_cast<double>(config.underlay_end_retract.value);
    const auto keepEnd = [&](int i) {
        return nThreads <= 2 || (cumMid[static_cast<std::size_t>(i)] >= retract &&
                                 cumMid[static_cast<std::size_t>(i)] <= totalMid - retract);
    };

    // --- Sous-couches (§10) : passes distinctes, ordre center -> edge -> zigzag. ---
    if (config.center_underlay) {
        SatinPass center;
        for (int i = 0; i < nThreads; ++i) {
            if (keepEnd(i)) center.points.push_back(toUm(mids[static_cast<std::size_t>(i)]));
        }
        if (center.points.size() >= 2) result.underlays.push_back(std::move(center));
    }
    if (config.underlay_edge) {
        SatinPass ea;
        SatinPass eb;
        for (int i = 0; i < nThreads; ++i) {
            if (!keepEnd(i)) continue;
            const auto& t = threads[static_cast<std::size_t>(i)];
            const double half = dist(t.a, t.b) * 0.5;
            const double ins = std::min(static_cast<double>(config.underlay_edge_inset.value),
                                        half * 0.9);
            const double f = half > 1e-6 ? ins / (2.0 * half) : 0.0;  // fraction a->b
            ea.points.push_back(toUm(lerpP(t.a, t.b, f)));
            eb.points.push_back(toUm(lerpP(t.b, t.a, f)));
        }
        if (ea.points.size() >= 2) result.underlays.push_back(std::move(ea));
        if (eb.points.size() >= 2) result.underlays.push_back(std::move(eb));
    }
    if (config.underlay_zigzag) {
        const double zs =
            static_cast<double>(std::max<std::int32_t>(1, config.underlay_zigzag_spacing.value));
        const int stride = std::max(1, static_cast<int>(std::lround(zs / density)));
        const double wf = std::clamp(config.underlay_zigzag_width, 0.1, 1.0);
        SatinPass zz;
        for (int i = 0; i < nThreads; i += stride) {
            const PointD m = mids[static_cast<std::size_t>(i)];
            zz.points.push_back(toUm(lerpP(m, threads[static_cast<std::size_t>(i)].a, wf)));
            zz.points.push_back(toUm(lerpP(m, threads[static_cast<std::size_t>(i)].b, wf)));
        }
        if (zz.points.size() >= 2) result.underlays.push_back(std::move(zz));
    }

    // --- Émission couche supérieure : zigzag + split + compensation asym. ---
    const double maxLen = static_cast<double>(std::max<std::int32_t>(1, config.max_stitch_length.value));
    const double pmax = static_cast<double>(config.pull_max.value);
    int emitted = 0;
    for (int i = 0; i < nThreads; ++i) {
        const auto& t = threads[static_cast<std::size_t>(i)];
        if (t.dropped) {
            continue;
        }
        if (t.jump_before) {
            result.jump_before.push_back(result.satin.size());
        }
        PointD pa = t.a;
        PointD pb = t.b;
        const double w = dist(pa, pb);
        result.max_width_um = std::max(result.max_width_um, w);
        // Pull latérale : base symétrique (pull_compensation) + fixe + proportionnelle par côté.
        const double offA =
            std::clamp(comp + config.pull_left.value + config.pull_left_prop * w, 0.0, pmax);
        const double offB =
            std::clamp(comp + config.pull_right.value + config.pull_right_prop * w, 0.0, pmax);
        if (w > 1e-6) {
            const double ux = (pa.x - pb.x) / w;
            const double uy = (pa.y - pb.y) / w;
            pa = {pa.x + ux * offA, pa.y + uy * offA};
            pb = {pb.x - ux * offB, pb.y - uy * offB};
        }
        result.satin.push_back(toUm(pa));
        const double len = dist(pa, pb);
        if (config.split_stitch != SplitStitchMode::Disabled && len > maxLen) {
            const int nsplit = std::max(1, static_cast<int>(std::ceil(len / maxLen)) - 1);
            for (int s = 1; s <= nsplit; ++s) {
                double frac = static_cast<double>(s) / (nsplit + 1);
                const double amp = 0.35 / (nsplit + 1);
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
    return result;
}

SatinResult fill_satin(const geometry::Path& rail_a, const geometry::Path& rail_b,
                       const SatinConfig& config) {
    SatinResult result;
    const auto a = to_points(rail_a);
    auto b = to_points(rail_b);
    if (a.size() < 2 || b.size() < 2) {
        return result;
    }
    if (opposite_orientation(a, b)) {
        std::reverse(b.begin(), b.end());
    }
    const auto cumA = cumulative(a);
    const auto cumB = cumulative(b);
    const double lenA = cumA.back();
    const double lenB = cumB.back();
    const double density = static_cast<double>(std::max<std::int32_t>(1, config.density.value));
    const double comp = static_cast<double>(config.pull_compensation.value);

    // Correspondance locale monotone (ladder, cf. fill_satin_columns) sur tout
    // le rail : remplace l'ancien appariement par fraction d'abscisse
    // identique sur les deux rails (source d'éventails/croisements dès que les
    // rails divergent en longueur ou en courbure, p. ex. virage/coude).
    const double maxStep = std::clamp(density / 4.0, 100.0, 500.0);
    const auto ladder = ladder_correspondence(a, cumA, 0.0, a.front(), lenA, a.back(), b, cumB, 0.0,
                                              b.front(), lenB, b.back(), maxStep);

    // Sous-couche : point droit sur l'axe central (aller simple, pas grossier).
    if (config.center_underlay) {
        const double uspace =
            static_cast<double>(std::max<std::int32_t>(1, config.underlay_spacing.value));
        const auto resampled = resample_by_medial_spacing(ladder, a, cumA, b, cumB, uspace);
        SatinPass center;
        center.points.reserve(resampled.size());
        for (const auto& cp : resampled) {
            center.points.push_back(toUm(midP(cp.pa, cp.pb)));
        }
        result.underlays.push_back(std::move(center));
    }

    // Satin : zigzag A0, B0, B1, A1, A2, B2, … en avançant sur la correspondance.
    const auto resampled = resample_by_medial_spacing(ladder, a, cumA, b, cumB, density);
    for (std::size_t i = 0; i < resampled.size(); ++i) {
        PointD pa = resampled[i].pa;
        PointD pb = resampled[i].pb;
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

std::vector<SatinRungSeg> default_rungs(const geometry::Path& rail_a, const geometry::Path& rail_b,
                                        Micrometers spacing) {
    std::vector<SatinRungSeg> out;
    const auto a = to_points(rail_a);
    auto b = to_points(rail_b);
    if (a.size() < 2 || b.size() < 2) {
        return out;
    }
    if (opposite_orientation(a, b)) {
        std::reverse(b.begin(), b.end());
    }
    const auto cumA = cumulative(a);
    const auto cumB = cumulative(b);
    const double lenA = cumA.back();
    const double lenB = cumB.back();
    const double sp = static_cast<double>(std::max<std::int32_t>(1, spacing.value));
    const double maxStep = std::clamp(sp / 4.0, 100.0, 500.0);
    const auto ladder = ladder_correspondence(a, cumA, 0.0, a.front(), lenA, a.back(), b, cumB, 0.0,
                                              b.front(), lenB, b.back(), maxStep);
    const auto resampled = resample_by_medial_spacing(ladder, a, cumA, b, cumB, sp);
    out.reserve(resampled.size());
    for (const auto& cp : resampled) {
        out.emplace_back(toUm(cp.pa), toUm(cp.pb));
    }
    return out;
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
