// SPDX-License-Identifier: Apache-2.0
#include "openstitch/auto_satin/shapes.hpp"

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
        ps.holes.push_back(circle(0, 0, 8'000));
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
    return std::nullopt;
}

}  // namespace openstitch::auto_satin
