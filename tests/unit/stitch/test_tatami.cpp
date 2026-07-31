// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

#include "openstitch/stitch_generation/tatami.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

namespace {

geometry::Path rect(std::int32_t w, std::int32_t h) {
    geometry::Path p;
    p.closed = true;
    p.nodes = {
        {Vec2um{Micrometers{0}, Micrometers{0}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{w}, Micrometers{0}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{w}, Micrometers{h}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{0}, Micrometers{h}}, geometry::NodeType::Corner, {}, {}},
    };
    return p;
}

document::TatamiParams params(std::int32_t spacing, std::int32_t len, double angle = 0.0) {
    document::TatamiParams p;
    p.row_spacing = Micrometers{spacing};
    p.stitch_length = Micrometers{len};
    p.angle = Angle{angle};
    p.inset = Micrometers{0};
    return p;
}

bool inside_rect(Vec2um p, std::int32_t w, std::int32_t h, std::int32_t tol = 2) {
    return p.x.value >= -tol && p.x.value <= w + tol && p.y.value >= -tol && p.y.value <= h + tol;
}

// Positions seules (indépendamment du drapeau couture/déplacement).
std::vector<Vec2um> positions(const std::vector<FillStitch>& f) {
    std::vector<Vec2um> out;
    out.reserve(f.size());
    for (const auto& fs : f) {
        out.push_back(fs.pos);
    }
    return out;
}

// Point dans polygone (rayon horizontal, pair-impair) — repere modele en um.
bool point_in_poly(const geometry::Path& poly, Vec2um p) {
    bool inside = false;
    const auto& n = poly.nodes;
    const std::size_t m = n.size();
    for (std::size_t i = 0, j = m - 1; i < m; j = i++) {
        const auto ax = n[i].pos.x.value, ay = n[i].pos.y.value;
        const auto bx = n[j].pos.x.value, by = n[j].pos.y.value;
        if (((ay > p.y.value) != (by > p.y.value)) &&
            (static_cast<double>(p.x.value) <
             static_cast<double>(bx - ax) * static_cast<double>(p.y.value - ay) /
                     static_cast<double>(by - ay) +
                 static_cast<double>(ax))) {
            inside = !inside;
        }
    }
    return inside;
}

// p est dans la region = dans l'exterieur ET hors de tous les trous.
bool in_region(const geometry::PathSet& region, Vec2um p) {
    if (!point_in_poly(region.outer, p)) {
        return false;
    }
    for (const auto& hole : region.holes) {
        if (point_in_poly(hole, p)) {
            return false;
        }
    }
    return true;
}

// p est-il FRANCHEMENT hors de la region ? Un point sur un bord (une couture qui
// longe le contour, cas parfaitement legitime) est ambigu pour le test pair-impair :
// on ne le compte comme debordement que si lui ET tout son voisinage (± tol) sont
// dehors. Seuls les vrais debordements (loin dans l'exterieur) sont retenus.
bool strictly_outside(const geometry::PathSet& region, Vec2um p, std::int32_t tol) {
    if (in_region(region, p)) {
        return false;
    }
    for (const std::int32_t dx : {-tol, 0, tol}) {
        for (const std::int32_t dy : {-tol, 0, tol}) {
            if (in_region(region, {Micrometers{p.x.value + dx}, Micrometers{p.y.value + dy}})) {
                return false;
            }
        }
    }
    return true;
}

// Forme en L (concave) : un routage naif relierait les deux branches a travers
// le coin manquant.
geometry::PathSet l_shape() {
    geometry::Path p;
    p.closed = true;
    const auto c = [](std::int32_t x, std::int32_t y) {
        return geometry::PathNode{Vec2um{Micrometers{x}, Micrometers{y}}, geometry::NodeType::Corner,
                                  {}, {}};
    };
    p.nodes = {c(0, 0),         c(20'000, 0),     c(20'000, 8'000),
               c(8'000, 8'000), c(8'000, 20'000), c(0, 20'000)};
    return {p, {}};
}

}  // namespace

// --- NON-REGRESSION DEBORDEMENT ---------------------------------------------
// Garde-fou central : sur une forme concave, a n'importe quel angle, AUCUNE
// couture (segment non-saut) ne doit sortir de la region. C'est exactement le
// mecanisme du debordement observe (les satins naifs sortaient jusqu'a 57 %).
TEST_CASE("tatami : aucune couture hors region (L concave, tous angles)") {
    const auto region = l_shape();
    const double pi = std::acos(-1.0);
    for (const double deg : {0.0, 20.0, 45.0, 90.0, 135.0}) {
        const auto fill = fill_tatami(region, params(1'000, 3'000, deg * pi / 180.0));
        REQUIRE_FALSE(fill.empty());
        int outside = 0;
        for (std::size_t i = 1; i < fill.size(); ++i) {
            if (fill[i].jump) {
                continue;  // saut : autorise a traverser le vide
            }
            // Points interieurs du segment cousu : aucun ne doit franchement
            // sortir de la region (tolerance ± 150 um pour les coutures de bord).
            for (const double t : {0.2, 0.4, 0.5, 0.6, 0.8}) {
                const auto a = fill[i - 1].pos;
                const auto b = fill[i].pos;
                const Vec2um s{
                    Micrometers{static_cast<std::int32_t>(a.x.value + (b.x.value - a.x.value) * t)},
                    Micrometers{static_cast<std::int32_t>(a.y.value + (b.y.value - a.y.value) * t)}};
                if (strictly_outside(region, s, 150)) {
                    ++outside;
                }
            }
        }
        INFO("angle = " << deg << " deg");
        CHECK(outside == 0);
    }
}

TEST_CASE("tatami : toutes les penetrations dans la region (L concave)") {
    const auto region = l_shape();
    const auto pts = positions(fill_tatami(region, params(1'000, 3'000, 0.6)));
    REQUIRE_FALSE(pts.empty());
    int outside = 0;
    for (const Vec2um& p : pts) {
        // Une penetration peut tomber sur le bord (arrondi um) : tolerance.
        if (strictly_outside(region, p, 150)) {
            ++outside;
        }
    }
    CHECK(outside == 0);
}

TEST_CASE("tatami : rectangle rempli, points dans la forme") {
    // 20x10 mm, rangées tous les 1 mm -> ~10 rangees.
    const auto pts = positions(fill_tatami({rect(20'000, 10'000), {}}, params(1'000, 4'000)));
    REQUIRE(pts.size() > 10);
    for (const Vec2um& p : pts) {
        CHECK(inside_rect(p, 20'000, 10'000));
    }
}

TEST_CASE("tatami : le nombre de rangees suit la densite") {
    // Compte les valeurs de y distinctes (angle 0 -> rangees horizontales).
    const auto pts = positions(fill_tatami({rect(20'000, 10'000), {}}, params(1'000, 4'000)));
    std::set<std::int32_t> rows;
    for (const Vec2um& p : pts) {
        rows.insert(p.y.value);
    }
    // Hauteur 10 mm, pas 1 mm : environ 10 rangees (+/- 1).
    CHECK(rows.size() >= 9);
    CHECK(rows.size() <= 11);
}

TEST_CASE("tatami : serpentin (rangees en sens alterne)") {
    const auto pts = positions(fill_tatami({rect(20'000, 4'000), {}}, params(1'000, 4'000)));
    REQUIRE(pts.size() >= 4);
    // Le remplissage doit balayer toute la largeur : min et max x atteints.
    std::int32_t minX = INT32_MAX, maxX = INT32_MIN;
    for (const Vec2um& p : pts) {
        minX = std::min(minX, p.x.value);
        maxX = std::max(maxX, p.x.value);
    }
    CHECK(minX <= 100);
    CHECK(maxX >= 19'900);
}

namespace {
geometry::PathSet ring_shape() {
    // Anneau : exterieur 20x20, trou central 8x8 (de 6,6 a 14,14 mm).
    geometry::PathSet ring;
    ring.outer = rect(20'000, 20'000);
    geometry::Path hole;
    hole.closed = true;
    hole.nodes = {
        {Vec2um{Micrometers{6'000}, Micrometers{6'000}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{14'000}, Micrometers{6'000}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{14'000}, Micrometers{14'000}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{6'000}, Micrometers{14'000}}, geometry::NodeType::Corner, {}, {}},
    };
    ring.holes.push_back(hole);
    return ring;
}
}  // namespace

TEST_CASE("tatami : trou respecte (pas de points dans le trou)") {
    const auto pts = positions(fill_tatami(ring_shape(), params(1'000, 4'000)));
    REQUIRE_FALSE(pts.empty());
    for (const Vec2um& p : pts) {
        const bool strictlyInHole = p.x.value > 6'500 && p.x.value < 13'500 &&
                                    p.y.value > 6'500 && p.y.value < 13'500;
        CHECK_FALSE(strictlyInHole);
    }
}

TEST_CASE("tatami : AUCUN point cousu ne traverse le trou (routage)") {
    // Correctif du debordement : un segment cousu (deux points consecutifs dont
    // le second n'est PAS un deplacement) ne doit jamais passer par le trou.
    const auto fill = fill_tatami(ring_shape(), params(1'000, 4'000));
    REQUIRE(fill.size() >= 2);
    int crossings = 0;
    for (std::size_t i = 1; i < fill.size(); ++i) {
        if (fill[i].jump) {
            continue;  // saut (aiguille levee) : autorise a traverser
        }
        const Vec2um a = fill[i - 1].pos;
        const Vec2um b = fill[i].pos;
        const Vec2um mid{Micrometers{(a.x.value + b.x.value) / 2},
                         Micrometers{(a.y.value + b.y.value) / 2}};
        const bool midInHole = mid.x.value > 6'200 && mid.x.value < 13'800 &&
                               mid.y.value > 6'200 && mid.y.value < 13'800;
        if (midInHole) {
            ++crossings;
        }
    }
    CHECK(crossings == 0);
    // Le routage contourne le trou avec TRES PEU de sauts (pas un eventail de
    // sauts a travers le trou) : quelques-uns suffisent.
    const auto jumps = static_cast<std::size_t>(
        std::count_if(fill.begin(), fill.end(), [](const FillStitch& f) { return f.jump; }));
    CHECK(jumps >= 1);
    CHECK(jumps <= 8);
}

TEST_CASE("tatami : forme concave U, aucune couture ne sort du polygone") {
    // Un « U » : le pont entre les deux branches se ferait en traversant l'exterieur
    // si le routage se fiait au seul chevauchement des rangees. La validation
    // geometrique doit l'empecher (le trajet devient un saut).
    geometry::Path u;
    u.closed = true;
    const auto c = [](std::int32_t x, std::int32_t y) {
        return geometry::PathNode{Vec2um{Micrometers{x}, Micrometers{y}}, geometry::NodeType::Corner,
                                  {}, {}};
    };
    // U ouvert vers le haut : encoche centrale de y=6000 a y=20000.
    u.nodes = {c(0, 0),      c(20'000, 0),      c(20'000, 20'000), c(13'000, 20'000),
               c(13'000, 6'000), c(7'000, 6'000), c(7'000, 20'000), c(0, 20'000)};

    const auto fill = fill_tatami({u, {}}, params(1'000, 4'000));
    REQUIRE_FALSE(fill.empty());

    // Aucune COUTURE ne doit traverser l'encoche centrale (la poche exterieure
    // du U) : x dans (7000,13000), y dans (6000,20000). Un routage naif fonde sur
    // le seul chevauchement des rangees relierait les deux branches a travers.
    int through_notch = 0;
    for (std::size_t i = 1; i < fill.size(); ++i) {
        if (fill[i].jump) {
            continue;
        }
        const Vec2um mid{Micrometers{(fill[i - 1].pos.x.value + fill[i].pos.x.value) / 2},
                         Micrometers{(fill[i - 1].pos.y.value + fill[i].pos.y.value) / 2}};
        if (mid.x.value > 7'300 && mid.x.value < 12'700 && mid.y.value > 6'300 &&
            mid.y.value < 19'700) {
            ++through_notch;
        }
    }
    CHECK(through_notch == 0);
}

TEST_CASE("tatami : longueur de point respectee le long des rangees") {
    const auto pts = positions(fill_tatami({rect(30'000, 3'000), {}}, params(1'000, 3'000)));
    REQUIRE(pts.size() >= 2);
    // Sur une meme rangee (meme y), l'ecart entre points consecutifs <= 3 mm.
    for (std::size_t i = 1; i < pts.size(); ++i) {
        if (pts[i].y == pts[i - 1].y) {
            CHECK(length_um(pts[i] - pts[i - 1]) <= 3'050.0);
        }
    }
}

TEST_CASE("tatami : angle 90 degres remplit aussi la forme") {
    const auto pts = positions(
        fill_tatami({rect(10'000, 20'000), {}}, params(1'000, 4'000, std::acos(-1.0) / 2.0)));
    REQUIRE(pts.size() > 10);
    for (const Vec2um& p : pts) {
        CHECK(inside_rect(p, 10'000, 20'000, 50));
    }
}

TEST_CASE("tatami : forme degeneree -> vide sans crash") {
    geometry::Path tiny;
    tiny.closed = true;
    tiny.nodes = {{Vec2um{Micrometers{0}, Micrometers{0}}, geometry::NodeType::Corner, {}, {}}};
    CHECK(fill_tatami({tiny, {}}, params(1'000, 4'000)).empty());
}

TEST_CASE("tatami : deterministe") {
    const auto a = fill_tatami({rect(20'000, 10'000), {}}, params(700, 3'000));
    const auto b = fill_tatami({rect(20'000, 10'000), {}}, params(700, 3'000));
    CHECK(a == b);
}

TEST_CASE("tatami : sous-couche de contour longe le bord, rentree dans la forme") {
    auto p = params(1'000, 3'000);
    p.underlay_edge = true;
    const auto u = tatami_underlay({rect(20'000, 10'000), {}}, p);
    REQUIRE_FALSE(u.empty());
    CHECK(u.front().size() >= 4);  // le contour rentré = plusieurs pénétrations
    for (const auto& pass : u) {
        for (const Vec2um& pt : pass) {
            CHECK(inside_rect(pt, 20'000, 10'000, 50));
        }
    }
}

TEST_CASE("tatami : sous-couche parallele = rangees espacees, dans la forme") {
    auto p = params(400, 3'000);  // remplissage dense 0,4 mm
    p.underlay_parallel = true;
    p.underlay_spacing = Micrometers{3'000};  // sous-couche très espacée
    const auto u = tatami_underlay({rect(20'000, 20'000), {}}, p);
    REQUIRE_FALSE(u.empty());
    for (const auto& pass : u) {
        for (const Vec2um& pt : pass) {
            CHECK(inside_rect(pt, 20'000, 20'000, 200));
        }
    }
}

TEST_CASE("tatami : aucune sous-couche par defaut") {
    CHECK(tatami_underlay({rect(20'000, 10'000), {}}, params(1'000, 3'000)).empty());
}

// --- POLITIQUE SURE : PAS DE REPLI SILENCIEUX SUR LE BORD BRUT ---------------
// Bug corrige : si le retrait de la sous-couche de contour echouait ou faisait
// disparaitre la forme (piece trop petite pour underlay_inset), le code cousait
// silencieusement sur le bord BRUT (sans marge de stabilisation, risque de
// deborder sous la compensation de la couche superieure). La politique sure :
// aucune sous-couche de contour n'est emise dans ce cas.

TEST_CASE("tatami : retrait de contour impossible (piece trop petite) -> aucune sous-couche, pas de repli sur le bord brut") {
    auto p = params(400, 1'000);
    p.underlay_edge = true;
    p.underlay_inset = Micrometers{2'000};  // > moitie du cote (2 mm de large)
    const auto u = tatami_underlay({rect(2'000, 2'000), {}}, p);
    CHECK(u.empty());  // aucun repli sur le bord brut : silence, pas de couture non stabilisante
}

TEST_CASE("tatami : retrait de contour nul explicite -> longe bien le bord brut (intention voulue)") {
    auto p = params(1'000, 3'000);
    p.underlay_edge = true;
    p.underlay_inset = Micrometers{0};  // intention explicite : pas de retrait
    const auto u = tatami_underlay({rect(20'000, 10'000), {}}, p);
    REQUIRE_FALSE(u.empty());
    for (const auto& pass : u) {
        for (const Vec2um& pt : pass) {
            CHECK(inside_rect(pt, 20'000, 10'000, 50));
        }
    }
}

TEST_CASE("tatami : retrait de contour qui echoue sur un trou d'un anneau -> sous-couche exterieure seule, jamais sur le trou") {
    // Le contour exterieur (grande forme) s'insete normalement ; seul le bord de
    // TROU n'est de toute facon jamais suivi (deja le cas), et un retrait
    // degenerant l'exterieur (piece globalement trop petite) doit rester silencieux.
    auto p = params(400, 1'000);
    p.underlay_edge = true;
    p.underlay_inset = Micrometers{50'000};  // bien plus grand que la forme entiere
    auto ring = ring_shape();
    const auto u = tatami_underlay(ring, p);
    CHECK(u.empty());
}

TEST_CASE("tatami : underpath cache coud une liaison au lieu de sauter") {
    const auto ring = ring_shape();
    auto p = params(2'000, 4'000);
    const auto off = fill_tatami(ring, p);
    p.hidden_underpath = true;
    const auto on = fill_tatami(ring, p);

    const auto jumps = [](const std::vector<FillStitch>& f) {
        return std::count_if(f.begin(), f.end(), [](const FillStitch& s) { return s.jump; });
    };
    const auto travels = [](const std::vector<FillStitch>& f) {
        return std::count_if(f.begin(), f.end(), [](const FillStitch& s) { return s.travel; });
    };
    // Au moins un saut devient un trajet cousu caché ; jamais l'inverse.
    CHECK(travels(on) > 0);
    CHECK(jumps(on) < jumps(off));
    CHECK(travels(off) == 0);

    // SÛRETÉ : aucun point cousu (couche sup. OU trajet caché) ne traverse le trou.
    int crossings = 0;
    for (std::size_t i = 1; i < on.size(); ++i) {
        if (on[i].jump) continue;
        const Vec2um mid{Micrometers{(on[i - 1].pos.x.value + on[i].pos.x.value) / 2},
                         Micrometers{(on[i - 1].pos.y.value + on[i].pos.y.value) / 2}};
        if (mid.x.value > 6'200 && mid.x.value < 13'800 && mid.y.value > 6'200 &&
            mid.y.value < 13'800) {
            ++crossings;
        }
    }
    CHECK(crossings == 0);
}

TEST_CASE("tatami : underpath deterministe") {
    auto p = params(2'000, 4'000);
    p.hidden_underpath = true;
    CHECK(fill_tatami(ring_shape(), p) == fill_tatami(ring_shape(), p));
}

// --- CONNECTOR_INVALID : ROBUSTESSE AUX CONTACTS SOMMETS ---------------------
// Bug corrige : connector_invalid ignorait tout contact sommet/extremite
// (proper_intersect excluait orient == 0) et ne sondait l'interieur du
// connecteur que si |dx| > 2*spacing. Un connecteur QUASI VERTICAL (petit dx)
// dont les extremites touchent un trou EXACTEMENT a ses sommets pouvait ainsi
// traverser le trou de part en part sans jamais etre detecte : ni par le test
// de croisement (les contacts sont degeneres), ni par l'echantillonnage
// (dx trop petit pour l'activer). segment_stays_in_region corrige ceci : la
// decoupe parametrique du segment a CHAQUE intersection (y compris degeneree)
// rend la detection independante de dx et de l'orientation.

namespace {
geometry::Path diamond_hole(std::int32_t cx, std::int32_t cy, std::int32_t r) {
    geometry::Path p;
    p.closed = true;
    const auto c = [](std::int32_t x, std::int32_t y) {
        return geometry::PathNode{Vec2um{Micrometers{x}, Micrometers{y}}, geometry::NodeType::Corner,
                                  {}, {}};
    };
    p.nodes = {c(cx, cy - r), c(cx + r, cy), c(cx, cy + r), c(cx - r, cy)};
    return p;
}
}  // namespace

TEST_CASE("segment_stays_in_region : accord vertical a travers un trou losange (contacts sommets)") {
    // Losange centre (10,10 mm), rayon 1 mm : sommets a (10,9), (11,10), (10,11), (9,10).
    // La ligne verticale x=10mm passe EXACTEMENT par les sommets haut et bas :
    // aucune arete n'est franchie « proprement », seuls des sommets sont touches.
    geometry::PathSet region{rect(20'000, 20'000), {diamond_hole(10'000, 10'000, 1'000)}};

    // Traverse le trou de part en part (touche les deux sommets, coupe l'interieur entre eux).
    CHECK_FALSE(segment_stays_in_region(region, Vec2um{Micrometers{10'000}, Micrometers{7'000}},
                                        Vec2um{Micrometers{10'000}, Micrometers{13'000}}));

    // Reste strictement au-dessus du trou : valide.
    CHECK(segment_stays_in_region(region, Vec2um{Micrometers{10'000}, Micrometers{1'000}},
                                  Vec2um{Micrometers{10'000}, Micrometers{7'000}}));

    // Part EXACTEMENT du sommet haut et plonge dans le trou : invalide (contact sommet unique).
    CHECK_FALSE(segment_stays_in_region(region, Vec2um{Micrometers{10'000}, Micrometers{9'000}},
                                        Vec2um{Micrometers{10'000}, Micrometers{9'500}}));
}

TEST_CASE("segment_stays_in_region : anneau multi-trous, seul le trou traverse est rejete") {
    geometry::PathSet region{rect(30'000, 20'000),
                             {diamond_hole(8'000, 10'000, 1'000), diamond_hole(22'000, 10'000, 1'000)}};

    // Traverse le second trou de part en part (contacts sommets).
    CHECK_FALSE(segment_stays_in_region(region, Vec2um{Micrometers{22'000}, Micrometers{7'000}},
                                        Vec2um{Micrometers{22'000}, Micrometers{13'000}}));
    // Le couloir entre les deux trous (tissu plein) reste valide.
    CHECK(segment_stays_in_region(region, Vec2um{Micrometers{15'000}, Micrometers{4'000}},
                                  Vec2um{Micrometers{15'000}, Micrometers{16'000}}));
    // Une couture pres du PREMIER trou (mais qui ne le traverse pas) reste valide.
    CHECK(segment_stays_in_region(region, Vec2um{Micrometers{8'000}, Micrometers{1'000}},
                                  Vec2um{Micrometers{8'000}, Micrometers{8'800}}));
}

TEST_CASE("segment_stays_in_region : coin rentrant d'un L, contact sommet vers l'encoche") {
    // l_shape() : coin rentrant en (8,8 mm). Une diagonale a 45 deg par ce sommet
    // (petit dx = 1mm, donc sous l'ancien seuil de declenchement de l'echantillonnage)
    // touche le sommet SANS le croiser franchement puis plonge dans l'encoche manquante.
    const auto region = l_shape();
    CHECK_FALSE(segment_stays_in_region(region, Vec2um{Micrometers{7'500}, Micrometers{7'500}},
                                        Vec2um{Micrometers{8'500}, Micrometers{8'500}}));
    // La meme diagonale, arretee AVANT le sommet, reste entierement dans le bras vertical : valide.
    CHECK(segment_stays_in_region(region, Vec2um{Micrometers{6'000}, Micrometers{6'000}},
                                  Vec2um{Micrometers{7'500}, Micrometers{7'500}}));
}

TEST_CASE("segment_stays_in_region : suivi de bord colineaire reste autorise (non-regression)") {
    // Le long du bord gauche du rectangle : colineaire a l'arete, aucune decoupe attendue.
    const geometry::PathSet region{rect(20'000, 10'000), {}};
    CHECK(segment_stays_in_region(region, Vec2um{Micrometers{0}, Micrometers{1'000}},
                                  Vec2um{Micrometers{0}, Micrometers{9'000}}));
}

TEST_CASE("tatami : trou losange, aucune couture ne le traverse (fill_tatami)") {
    // Filet de securite de bout en bout : un trou dont les sommets s'alignent
    // verticalement (cas degenere ci-dessus) ne doit produire AUCUN point cousu
    // a l'interieur, quelle que soit la maniere dont fill_tatami route les liaisons.
    geometry::PathSet region{rect(20'000, 20'000), {diamond_hole(10'000, 10'000, 1'000)}};
    const auto fill = fill_tatami(region, params(500, 3'000));
    REQUIRE_FALSE(fill.empty());
    int crossings = 0;
    for (std::size_t i = 1; i < fill.size(); ++i) {
        if (fill[i].jump) continue;
        const Vec2um a = fill[i - 1].pos;
        const Vec2um b = fill[i].pos;
        for (const double t : {0.25, 0.5, 0.75}) {
            const Vec2um s{Micrometers{static_cast<std::int32_t>(a.x.value + (b.x.value - a.x.value) * t)},
                          Micrometers{static_cast<std::int32_t>(a.y.value + (b.y.value - a.y.value) * t)}};
            const bool strictlyInHole =
                std::abs(s.x.value - 10'000) + std::abs(s.y.value - 10'000) < 900;  // sous le losange, marge
            if (strictlyInHole) ++crossings;
        }
    }
    CHECK(crossings == 0);
}

TEST_CASE("tatami : le point d'entree oriente le demarrage") {
    auto p = params(1'000, 3'000);
    const auto base = fill_tatami({rect(20'000, 10'000), {}}, p);
    p.entry_point = Vec2um{Micrometers{20'000}, Micrometers{10'000}};  // coin haut-droit
    const auto withEntry = fill_tatami({rect(20'000, 10'000), {}}, p);
    REQUIRE_FALSE(base.empty());
    REQUIRE_FALSE(withEntry.empty());
    CHECK(base.front().pos.x.value < 5'000);        // sans entrée : démarre à gauche
    CHECK(withEntry.front().pos.x.value > 15'000);  // avec entrée : démarre à droite
}
