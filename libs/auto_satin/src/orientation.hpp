// SPDX-License-Identifier: Apache-2.0
// En-tête interne à libs/auto_satin : fonction d'orientation g(s) du moteur
// d'auto-satin par squelette (specs/plans/satin-squelette-traversees.md, §5 et §7).
//
// Une orientation de fil est définie modulo π (une droite n'a pas de sens). Tous
// les écarts sont donc calculés dans (-π/2, π/2] : 170° et 10° sont distants de
// 20°, pas de 160°. Un écart d'exactement π/2 est ambigu (deux arcs
// équivalents) ; la règle est déterministe : on tourne dans le sens POSITIF.
#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace openstitch::auto_satin::detail {

inline constexpr double kPi = std::numbers::pi;
inline constexpr double kHalfPi = std::numbers::pi / 2.0;

// Ramène un angle (radians) dans (-π/2, π/2]. La tolérance fait tomber un écart
// de π/2 « à un bruit numérique près » du bon côté (+π/2).
inline double wrap_half_pi(double a) {
    constexpr double kEps = 1e-9;
    a = std::fmod(a, kPi);
    if (a > kHalfPi + kEps) {
        a -= kPi;
    } else if (a <= -kHalfPi + kEps) {
        a += kPi;
    }
    return a;
}

// Écart signé le plus court de `from` vers `to`, modulo π.
inline double angle_diff_pi(double from, double to) {
    return wrap_half_pi(to - from);
}

// Interpolation linéaire sur l'arc le plus court modulo π (t ∈ [0, 1]).
inline double lerp_angle_pi(double a, double b, double t) {
    return wrap_half_pi(a + angle_diff_pi(a, b) * t);
}

// Clé d'orientation posée par l'utilisateur sur l'axe. `value` est un angle
// ABSOLU (radians, repère modèle) si `absolute`, sinon un écart `δ` par rapport
// à la perpendiculaire à l'axe (g = α + π/2 + δ).
struct AngleKey {
    double s{0.0}; // abscisse curviligne sur l'axe, en µm
    bool absolute{false};
    double value{0.0};
};

// Ensemble de clés, avec l'angle α de l'axe à chaque clé (pour convertir un
// absolu en relatif quand l'une des deux clés voisines est relative).
struct OrientationKeys {
    std::vector<AngleKey> keys; // triées par s croissant
    std::vector<double> alpha;  // α de l'axe à keys[i].s
};

// Orientation g(s) à l'abscisse `s`, pour un axe de tangente `alpha` à cette
// abscisse :
//  - aucune clé : perpendiculaire, g = α + π/2 ;
//  - avant la première clé / après la dernière : valeur de la clé la plus proche ;
//  - entre deux clés : interpolation modulo π. Deux clés ABSOLUES interpolent
//    l'angle absolu (deux angles égaux donnent un angle constant) ; sinon, on
//    interpole l'écart relatif δ.
inline double orientation_at(const OrientationKeys& k, double s, double alpha) {
    const double perp = alpha + kHalfPi;
    const auto n = k.keys.size();
    if (n == 0) {
        return perp;
    }
    const auto relative = [&](std::size_t i) {
        const auto& key = k.keys[i];
        return key.absolute ? wrap_half_pi(key.value - (k.alpha[i] + kHalfPi)) : key.value;
    };
    const auto single = [&](std::size_t i) {
        const auto& key = k.keys[i];
        return key.absolute ? key.value : perp + key.value;
    };
    if (s <= k.keys.front().s) {
        return single(0);
    }
    if (s >= k.keys.back().s) {
        return single(n - 1);
    }
    std::size_t hi = 1;
    while (hi < n && k.keys[hi].s < s) {
        ++hi;
    }
    const std::size_t lo = hi - 1;
    const double span = k.keys[hi].s - k.keys[lo].s;
    const double t = span > 1e-12 ? (s - k.keys[lo].s) / span : 0.0;
    if (k.keys[lo].absolute && k.keys[hi].absolute) {
        return lerp_angle_pi(k.keys[lo].value, k.keys[hi].value, t);
    }
    return perp + lerp_angle_pi(relative(lo), relative(hi), t);
}

} // namespace openstitch::auto_satin::detail
