// SPDX-License-Identifier: Apache-2.0
#include "openstitch/auto_satin/shapes.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>
#include <vector>

#include "openstitch/geometry/clean.hpp"

namespace openstitch::auto_satin {

namespace {

using geometry::Path;
using geometry::PathNode;
using geometry::PathSet;

PathNode node(double x, double y) {
    return PathNode{Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(x))},
                           Micrometers{static_cast<std::int32_t>(std::lround(y))}},
                    geometry::NodeType::Corner, std::nullopt, std::nullopt};
}

// Bande fermée à partir d'une ligne centrale et d'une demi-largeur (µm).
Path band(const std::function<std::pair<double, double>(double)>& centerline, int samples,
          double half_width) {
    std::vector<std::pair<double, double>> left, right;
    for (int i = 0; i <= samples; ++i) {
        const double t = static_cast<double>(i) / samples;
        const auto [x, y] = centerline(t);
        const double dt = 1.0 / samples;
        const double t2 = std::min(1.0, t + dt);
        const double t0 = std::max(0.0, t - dt);
        const auto [xa, ya] = centerline(t0);
        const auto [xb, yb] = centerline(t2);
        double tx = xb - xa, ty = yb - ya;
        const double len = std::hypot(tx, ty);
        if (len > 0) {
            tx /= len;
            ty /= len;
        }
        const double nx = -ty, ny = tx;  // normale
        left.emplace_back(x + nx * half_width, y + ny * half_width);
        right.emplace_back(x - nx * half_width, y - ny * half_width);
    }
    Path p;
    p.closed = true;
    for (const auto& [x, y] : left) {
        p.nodes.push_back(node(x, y));
    }
    for (auto it = right.rbegin(); it != right.rend(); ++it) {
        p.nodes.push_back(node(it->first, it->second));
    }
    return p;
}

PathSet single(Path outer) { return PathSet{std::move(outer), {}}; }

Path rect(double x0, double y0, double x1, double y1) {
    Path p;
    p.closed = true;
    p.nodes = {node(x0, y0), node(x1, y0), node(x1, y1), node(x0, y1)};
    return p;
}

Path circle(double cx, double cy, double r, int n = 96) {
    Path p;
    p.closed = true;
    for (int i = 0; i < n; ++i) {
        const double a = 2.0 * std::numbers::pi * i / n;
        p.nodes.push_back(node(cx + r * std::cos(a), cy + r * std::sin(a)));
    }
    return p;
}

PathSet from_union(const std::vector<Path>& parts) {
    auto sets = geometry::union_nonzero(parts);
    if (sets && !sets->empty()) {
        // Garde le plus grand morceau.
        std::size_t best = 0;
        double bestA = -1.0;
        for (std::size_t i = 0; i < sets->size(); ++i) {
            const double a = std::abs(geometry::signed_area_um2((*sets)[i].outer));
            if (a > bestA) {
                bestA = a;
                best = i;
            }
        }
        return (*sets)[best];
    }
    return single(parts.front());
}

}  // namespace

std::optional<geometry::PathSet> make_shape(const std::string& name) {
    const double W = 2500.0;  // demi-largeur 2,5 mm (bande de 5 mm)
    if (name == "rectangle") {
        return single(rect(0, -W, 40'000, W));
    }
    if (name == "capsule") {
        // Rectangle + deux demi-cercles aux bouts.
        std::vector<Path> parts{rect(0, -W, 40'000, W), circle(0, 0, W, 48),
                                circle(40'000, 0, W, 48)};
        return from_union(parts);
    }
    if (name == "ribbon") {
        // Arc de cercle (bande courbe continue).
        return single(band(
            [](double t) {
                const double a = std::numbers::pi * (0.15 + 0.7 * t);
                return std::pair{20'000.0 + 18'000.0 * std::cos(a), 18'000.0 * std::sin(a)};
            },
            40, W));
    }
    if (name == "s") {
        return single(band(
            [](double t) {
                return std::pair{40'000.0 * t, 9'000.0 * std::sin(2.0 * std::numbers::pi * t)};
            },
            60, W));
    }
    if (name == "y") {
        std::vector<Path> parts{
            band([](double t) { return std::pair{0.0, -20'000.0 + 20'000.0 * t}; }, 20, W),
            band([](double t) { return std::pair{-14'000.0 * t, 14'000.0 * t}; }, 20, W),
            band([](double t) { return std::pair{14'000.0 * t, 14'000.0 * t}; }, 20, W)};
        return from_union(parts);
    }
    if (name == "t") {
        std::vector<Path> parts{rect(-18'000, W, 18'000, -W + 5'000),
                                rect(-W, -22'000, W, W)};
        // stem vertical + barre horizontale
        parts[0] = rect(-18'000, 12'000, 18'000, 12'000 + 5'000);
        parts[1] = rect(-2'500, -20'000, 2'500, 17'000);
        return from_union(parts);
    }
    if (name == "cross") {
        std::vector<Path> parts{rect(-20'000, -W, 20'000, W), rect(-W, -20'000, W, 20'000)};
        return from_union(parts);
    }
    if (name == "h") {
        // Deux barres verticales (40 mm) reliees par un pont horizontal (5 mm)
        // a mi-hauteur : 2 jonctions T, dont une arete de squelette Jonction-
        // Jonction (le pont lui-meme) -- topologie absente des autres fixtures.
        std::vector<Path> parts{rect(-20'000 - W, -20'000, -20'000 + W, 20'000),
                                rect(20'000 - W, -20'000, 20'000 + W, 20'000),
                                rect(-20'000, -W, 20'000, W)};
        return from_union(parts);
    }
    if (name == "circle") {
        return single(circle(0, 0, 15'000));
    }
    if (name == "ring") {
        PathSet ps;
        ps.outer = circle(0, 0, 15'000);
        // `circle()` parcourt toujours dans le même sens (angle croissant) :
        // un trou doit être orienté à L'OPPOSÉ de l'extérieur (convention
        // « région vectorielle », cf. `path.hpp`) pour que les opérations
        // sensibles à l'orientation (ex. `geometry::inset_path_set`) érodent
        // le trou dans le bon sens plutôt que de le faire grandir/rétrécir à
        // l'envers -- trouvé via `satin_coverage::analyze_satin_coverage` sur
        // cette fixture (couverture cœur mesurée à 71 % au lieu de ~99 %,
        // alors qu'aucune autre partie du pipeline n'est sensible à
        // l'orientation d'un trou, ce qui rendait le défaut invisible avant).
        Path hole = circle(0, 0, 8'000);
        std::reverse(hole.nodes.begin(), hole.nodes.end());
        ps.holes.push_back(std::move(hole));
        return ps;
    }
    if (name == "wide") {
        // Allongée ET large (100 x 20 mm) : un vrai cas « trop large pour satin ».
        return single(rect(0, -10'000, 100'000, 10'000));
    }
    if (name == "tiny") {
        return single(rect(0, -300, 4'000, 300));  // 0,6 mm de large
    }
    if (name == "notch") {
        // Bande de 40 x 5 mm entaillée d'une profonde encoche en V sur le bord
        // haut (creux jusqu'a 1 mm du bord bas au point le plus etroit) :
        // regression pour l'audit "generation partielle des colonnes auto-satin
        // sur formes concaves" (rails discontinus / stations ignorees en
        // silence pres d'une forte variation de largeur locale).
        Path p;
        p.closed = true;
        p.nodes = {node(0, -W), node(40'000, -W), node(40'000, W), node(25'000, W),
                  node(20'000, -1'500), node(15'000, W), node(0, W)};
        return single(p);
    }
    if (name == "pinch") {
        // Variante plus severe de "notch" : l'encoche descend a moins de
        // 0,3 mm du bord oppose (quasi-contact), pour stresser davantage la
        // section transversale pres du point le plus etroit.
        Path p;
        p.closed = true;
        p.nodes = {node(0, -W), node(40'000, -W), node(40'000, W), node(25'000, W),
                  node(20'000, -2'200), node(15'000, W), node(0, W)};
        return single(p);
    }
    if (name == "trident") {
        // Jonction concave ASYMÉTRIQUE à 3 branches très inégales, régression
        // pour l'audit « raccord de jonction sur formes branchées/concaves » :
        // une grande branche verticale épaisse (8 mm), une branche interne
        // POINTUE (triangle effilé, pas une bande à largeur constante) partant
        // à angle aigu, et une branche latérale étroite (2 mm) quasi
        // perpendiculaire aux deux autres. Les trois se rejoignent près de
        // l'origine avec une confluence délibérément non étoilée (aucune
        // symétrie n'aide un ancrage indépendant par branche à deviner la
        // bonne encoche partagée).
        std::vector<Path> parts;
        // Grande branche verticale (6 mm de large), du bas vers la confluence
        // (léger débord au-dessus de l'origine, juste assez pour la fusion).
        parts.push_back(rect(-3'000, -25'000, 3'000, 1'200));
        // Branche latérale étroite (1,2 mm), quasi horizontale vers la droite.
        parts.push_back(rect(-1'500, -600, 16'000, 600));
        // Branche interne pointue : triangle effilé partant vers le haut-gauche,
        // base 1,2 mm à la confluence, pointe nette à l'autre bout (pas de
        // largeur constante comme les deux autres branches).
        Path wedge;
        wedge.closed = true;
        wedge.nodes = {node(-600, 0), node(600, 0), node(-6'000, 9'000)};
        parts.push_back(wedge);
        return from_union(parts);
    }

    // ------------------------------------------------------------------
    // Corpus de torture (mission de durcissement du contrat SatinPlanner,
    // 2026-08-17, §9-14) : fixtures délibérément difficiles, chacune ciblant
    // une propriété précise du planner récursif que le corpus ci-dessus
    // n'exerçait pas encore (5+ branches, asymétrie forte, nombreuses
    // branches parallèles, récursion prouvée à profondeur >= 3, trous
    // multiples, trou près d'une jonction, coupe polygonale exercée bout en
    // bout).
    // ------------------------------------------------------------------

    if (name == "star5") {
        // §9.1 : étoile a 5 branches egales rayonnant d'un centre commun --
        // une seule jonction, mais a 5 voies (aucune autre fixture du corpus
        // n'en a plus de 3) : teste que le planner produit plusieurs
        // SatinRegions sans explosion combinatoire des candidats de coupe.
        std::vector<Path> parts;
        constexpr int kBranches = 5;
        constexpr double kArmLen = 20'000.0;
        constexpr double kHalfW = 1'800.0;
        for (int i = 0; i < kBranches; ++i) {
            const double angle = 2.0 * std::numbers::pi * i / kBranches - std::numbers::pi / 2.0;
            parts.push_back(band(
                [angle](double t) { return std::pair{kArmLen * t * std::cos(angle), kArmLen * t * std::sin(angle)}; },
                20, kHalfW));
        }
        return from_union(parts);
    }
    if (name == "asymmetric_star") {
        // §9.2 : memes 5 branches, mais chacune d'une longueur/largeur
        // DIFFERENTE (longue/large, courte/fine, longue/fine, courte/large,
        // moyenne) -- evite qu'une symetrie implicite rende le test
        // artificiellement facile (§9.2).
        struct Arm {
            double angleDeg;
            double length;
            double halfWidth;
        };
        const std::vector<Arm> arms = {
            {0.0, 25'000.0, 3'000.0}, {80.0, 10'000.0, 900.0}, {160.0, 22'000.0, 900.0},
            {230.0, 9'000.0, 2'600.0}, {300.0, 16'000.0, 1'600.0},
        };
        std::vector<Path> parts;
        for (const auto& arm : arms) {
            const double angle = arm.angleDeg * std::numbers::pi / 180.0;
            const double length = arm.length;
            const double halfWidth = arm.halfWidth;
            parts.push_back(band(
                [angle, length](double t) { return std::pair{length * t * std::cos(angle), length * t * std::sin(angle)}; },
                20, halfWidth));
        }
        return from_union(parts);
    }
    if (name == "comb") {
        // §9.3 : tronc horizontal + 6 dents paralleles -- teste la
        // recursion, le nombre de regions final, la performance, et
        // l'explosion eventuelle du nombre de coupes candidates sur de
        // nombreuses branches paralleles attachees au meme tronc.
        std::vector<Path> parts;
        constexpr double kTrunkHalfH = 2'000.0;
        parts.push_back(rect(-2'000, -kTrunkHalfH, 42'000, kTrunkHalfH));
        constexpr int kTeeth = 6;
        constexpr double kToothHalfW = 900.0;
        constexpr double kToothLen = 12'000.0;
        for (int i = 0; i < kTeeth; ++i) {
            const double cx = 2'000.0 + i * 7'000.0;
            parts.push_back(rect(cx - kToothHalfW, kTrunkHalfH - 500.0, cx + kToothHalfW,
                                 kTrunkHalfH - 500.0 + kToothLen));
        }
        return from_union(parts);
    }
    if (name == "E") {
        // §9.4 : meme interet que "comb" mais avec plusieurs branches du
        // MEME cote d'une colonne verticale (lettre E) -- topologie de
        // jonctions differente d'un peigne symetrique.
        std::vector<Path> parts{
            rect(-2'000, -20'000, 2'000, 20'000),     // colonne verticale
            rect(-2'000, 16'000, 16'000, 20'000),      // barre haute
            rect(-2'000, -2'000, 13'000, 2'000),        // barre milieu
            rect(-2'000, -20'000, 16'000, -16'000),      // barre basse
        };
        return from_union(parts);
    }
    if (name == "deep_recursive") {
        // §9.5 : arbre de branches en cascade (tronc -> branche -> sous-
        // branche -> sous-sous-branche) construit specifiquement pour
        // PROUVER qu'au moins une region enfant est reellement redecoupee a
        // son tour -- profondeur de plan >= 3 attendue sur cette fixture.
        std::vector<Path> parts{
            rect(0, -2'500, 30'000, 2'500),         // niveau 0 : tronc
            rect(14'000, 0, 18'000, 20'000),          // niveau 1 : branche
            rect(16'000, 14'000, 30'000, 18'000),       // niveau 2 : sous-branche
            rect(26'000, 8'000, 30'000, 14'000),          // niveau 3 : sous-sous-branche
        };
        return from_union(parts);
    }
    if (name == "multi_neck") {
        // §11 : trois masses reliees par DEUX etranglements en serie --
        // le planner doit pouvoir considerer plusieurs coupes, pas
        // seulement une.
        std::vector<Path> parts{
            circle(0, 0, 6'000, 64),      rect(4'000, -600, 16'000, 600),  circle(20'000, 0, 6'000, 64),
            rect(24'000, -600, 36'000, 600), circle(40'000, 0, 6'000, 64),
        };
        return from_union(parts);
    }
    if (name == "dumbbell") {
        // §10 : bras fin (1mm) -> masse large -> bras fin (1mm) -- teste si
        // une subdivision supplementaire transforme la masse large en
        // plusieurs regions satinables plutot qu'un simple rejet.
        std::vector<Path> parts{
            rect(0, -500, 17'000, 500),
            circle(22'000, 0, 7'000, 64),
            rect(27'000, -500, 44'000, 500),
        };
        return from_union(parts);
    }
    if (name == "deep_channel") {
        // §12 : concavite tres profonde mais RECTANGULAIRE (un vrai canal a
        // deux coins reflex, pas une simple pointe en V comme "notch"/
        // "pinch") -- variante adversariale sans trou reel.
        Path p;
        p.closed = true;
        p.nodes = {node(0, -8'000), node(40'000, -8'000), node(40'000, 8'000), node(24'000, 8'000),
                  node(24'000, -4'000), node(16'000, -4'000), node(16'000, 8'000), node(0, 8'000)};
        return single(p);
    }
    if (name == "two_holes") {
        // §14 : rectangle perce de DEUX trous circulaires separes -- ni
        // trou ne doit jamais etre rempli, traverse par une coupe invalide,
        // ou compter comme couverture manquante.
        PathSet ps;
        ps.outer = rect(0, -15'000, 60'000, 15'000);
        Path hole1 = circle(15'000, 0, 5'000);
        std::reverse(hole1.nodes.begin(), hole1.nodes.end());
        Path hole2 = circle(45'000, 0, 5'000);
        std::reverse(hole2.nodes.begin(), hole2.nodes.end());
        ps.holes.push_back(std::move(hole1));
        ps.holes.push_back(std::move(hole2));
        return ps;
    }
    if (name == "ring_branch") {
        // §14 : anneau (memes rayons que "ring", deja valide) avec une
        // branche supplementaire attachee a son bord EXTERIEUR -- le trou
        // interieur reste entierement a l'ecart de la branche.
        std::vector<Path> parts{circle(0, 0, 15'000), rect(15'000, -1'500, 30'000, 1'500)};
        PathSet merged = from_union(parts);
        Path hole = circle(0, 0, 8'000);
        std::reverse(hole.nodes.begin(), hole.nodes.end());
        merged.holes.push_back(std::move(hole));
        return merged;
    }
    if (name == "junction_with_hole") {
        // §14 : jonction en T (memes proportions que "t") avec un petit
        // trou PRES de la confluence, pas au milieu d'une branche -- cas le
        // plus dur pour ne jamais traverser le trou par une coupe.
        std::vector<Path> parts{rect(-18'000, 12'000, 18'000, 17'000), rect(-2'500, -20'000, 2'500, 17'000)};
        PathSet merged = from_union(parts);
        Path hole = circle(0, 13'000, 1'800, 32);
        std::reverse(hole.nodes.begin(), hole.nodes.end());
        merged.holes.push_back(std::move(hole));
        return merged;
    }
    if (name == "polygonal_cut_fixture") {
        // §13 : ruban 100x8mm entaille de deux notches pointus sur des
        // bords opposes DESALIGNES, obstrue par un bloc rectangulaire qui
        // bloque specifiquement la coupe DROITE entre les deux concavites
        // sans toucher les notches -- exact fixture qui a prouve le
        // mecanisme de coupe polygonale bout en bout
        // (`tests/unit/satin_planning/test_concavity_cuts.cpp`,
        // `elbow_obstructed_hourglass_shape`), exposee ici pour qu'un test
        // end-to-end du planner COMPLET (pas seulement `generate_concavity_
        // cut_candidates`) puisse aussi l'exercer.
        Path p;
        p.closed = true;
        p.nodes = {node(0, -4'000),      node(53'000, -4'000), node(54'000, -1'000), node(55'000, -4'000),
                  node(100'000, -4'000), node(100'000, 4'000), node(62'000, 4'000),  node(61'000, 1'000),
                  node(60'000, 4'000),   node(58'500, 4'000),  node(58'500, -1'500), node(56'500, -1'500),
                  node(56'500, 4'000),   node(0, 4'000)};
        return single(p);
    }

    return std::nullopt;
}

}  // namespace openstitch::auto_satin
