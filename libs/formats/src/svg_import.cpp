// SPDX-License-Identifier: Apache-2.0
#include "openstitch/formats/svg_import.hpp"

#include <pugixml.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>

#include "openstitch/geometry/polyline.hpp"

namespace openstitch::formats {

namespace {

// --- Géométrie de travail en double précision (espace "unités utilisateur"
// SVG, avant conversion finale en Vec2um) -------------------------------

struct V2 {
    double x{0.0};
    double y{0.0};
};

V2 operator+(V2 a, V2 b) { return {a.x + b.x, a.y + b.y}; }
V2 operator-(V2 a, V2 b) { return {a.x - b.x, a.y - b.y}; }
V2 operator*(V2 a, double s) { return {a.x * s, a.y * s}; }

// Transformation affine 2D (matrice SVG [a b c d e f], cf. spec §7.6) :
// point' = (a*x + c*y + e, b*x + d*y + f). Composition SVG : appliquer T1
// puis T2 revient à la matrice T2*T1 (pré-multiplication), exactement ce que
// `compose(outer, inner)` calcule ci-dessous.
struct Affine {
    double a{1.0}, b{0.0}, c{0.0}, d{1.0}, e{0.0}, f{0.0};

    [[nodiscard]] V2 apply_point(V2 p) const { return {a * p.x + c * p.y + e, b * p.x + d * p.y + f}; }
    // Un vecteur (tangente Bézier relative) ignore la translation.
    [[nodiscard]] V2 apply_vector(V2 v) const { return {a * v.x + c * v.y, b * v.x + d * v.y}; }
};

Affine compose(const Affine& outer, const Affine& inner) {
    return Affine{
        outer.a * inner.a + outer.c * inner.b,
        outer.b * inner.a + outer.d * inner.b,
        outer.a * inner.c + outer.c * inner.d,
        outer.b * inner.c + outer.d * inner.d,
        outer.a * inner.e + outer.c * inner.f + outer.e,
        outer.b * inner.e + outer.d * inner.f + outer.f,
    };
}

// --- Nœud de chemin en double précision (miroir de geometry::PathNode,
// converti en Vec2um seulement à la toute fin -- évite d'accumuler l'erreur
// d'arrondi au fil de transformations imbriquées) ------------------------

struct DNode {
    V2 pos{};
    bool smooth{false};
    std::optional<V2> tan_in;   // relatif à pos, déjà transformé
    std::optional<V2> tan_out;  // relatif à pos, déjà transformé
};

struct DPath {
    std::vector<DNode> nodes;
    bool closed{false};
};

// --- Analyse des attributs numériques (longueurs avec unité, transform,
// données de chemin `d`) -------------------------------------------------

void skip_ws_and_commas(std::string_view s, std::size_t& i) {
    while (i < s.size() && (std::isspace(static_cast<unsigned char>(s[i])) != 0 || s[i] == ',')) ++i;
}

// Un seul nombre flottant SVG (signe, exposant, point décimal) -- pas de
// séparateur consommé après (laisse ça à l'appelant, cf. le format de
// données de chemin qui autorise des nombres collés : "1.5.5" == "1.5 .5").
std::optional<double> parse_number(std::string_view s, std::size_t& i) {
    const std::size_t start = i;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
    bool sawDigitOrDot = false;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])) != 0) {
        ++i;
        sawDigitOrDot = true;
    }
    if (i < s.size() && s[i] == '.') {
        ++i;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])) != 0) {
            ++i;
            sawDigitOrDot = true;
        }
    }
    if (!sawDigitOrDot) {
        i = start;
        return std::nullopt;
    }
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        const std::size_t expStart = i;
        ++i;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
        const std::size_t digitsStart = i;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])) != 0) ++i;
        if (i == digitsStart) i = expStart;  // "1e" sans chiffre : pas un exposant
    }
    double value = 0.0;
    const auto res = std::from_chars(s.data() + start, s.data() + i, value);
    if (res.ec != std::errc{}) {
        i = start;
        return std::nullopt;
    }
    return value;
}

// Longueur CSS/SVG avec unité optionnelle (mm/cm/in/pt/pc/px, ou sans unité
// == px) -- renvoyée en "unités utilisateur" (== px, 1/96 pouce, valeur
// normative CSS ; jamais une supposition arbitraire). `%` non résolu
// proprement (dépendrait du viewport de référence) : traité comme un
// nombre brut avec un signalement à l'appelant.
struct ParsedLength {
    double user_units{0.0};
    bool had_percent{false};
};

ParsedLength parse_length(std::string_view raw, double fallback = 0.0) {
    std::size_t i = 0;
    skip_ws_and_commas(raw, i);
    const auto num = parse_number(raw, i);
    if (!num) return {fallback, false};
    const std::string_view unit = raw.substr(i);
    constexpr double kPxPerInch = 96.0;
    if (unit == "mm") return {*num * kPxPerInch / 25.4, false};
    if (unit == "cm") return {*num * kPxPerInch / 2.54, false};
    if (unit == "in") return {*num * kPxPerInch, false};
    if (unit == "pt") return {*num * kPxPerInch / 72.0, false};
    if (unit == "pc") return {*num * kPxPerInch / 6.0, false};
    if (unit == "%") return {*num, true};
    return {*num, false};  // "px" ou sans unité
}

// --- Transform SVG : "translate(...) rotate(...) ..." (composé de gauche à
// droite -- chaque fonction s'applique à l'espace produit par la précédente,
// cf. spec §7.6). skewX/skewY convertis en matrice équivalente (jamais un
// second modèle de transformation séparé).
Affine parse_transform(std::string_view raw) {
    Affine result;
    std::size_t i = 0;
    while (true) {
        skip_ws_and_commas(raw, i);
        if (i >= raw.size()) break;
        const std::size_t nameStart = i;
        while (i < raw.size() && std::isalpha(static_cast<unsigned char>(raw[i])) != 0) ++i;
        const std::string_view name = raw.substr(nameStart, i - nameStart);
        skip_ws_and_commas(raw, i);
        if (i >= raw.size() || raw[i] != '(') break;
        ++i;
        std::vector<double> args;
        while (true) {
            skip_ws_and_commas(raw, i);
            if (i < raw.size() && raw[i] == ')') break;
            const auto num = parse_number(raw, i);
            if (!num) break;
            args.push_back(*num);
        }
        if (i < raw.size() && raw[i] == ')') ++i;

        Affine local;
        if (name == "translate" && !args.empty()) {
            local = Affine{1, 0, 0, 1, args[0], args.size() > 1 ? args[1] : 0.0};
        } else if (name == "scale" && !args.empty()) {
            const double sx = args[0];
            const double sy = args.size() > 1 ? args[1] : sx;
            local = Affine{sx, 0, 0, sy, 0, 0};
        } else if (name == "rotate" && !args.empty()) {
            const double rad = args[0] * std::numbers::pi / 180.0;
            const double cs = std::cos(rad), sn = std::sin(rad);
            if (args.size() >= 3) {
                const Affine toOrigin{1, 0, 0, 1, -args[1], -args[2]};
                const Affine rot{cs, sn, -sn, cs, 0, 0};
                const Affine back{1, 0, 0, 1, args[1], args[2]};
                local = compose(back, compose(rot, toOrigin));
            } else {
                local = Affine{cs, sn, -sn, cs, 0, 0};
            }
        } else if (name == "skewX" && !args.empty()) {
            local = Affine{1, 0, std::tan(args[0] * std::numbers::pi / 180.0), 1, 0, 0};
        } else if (name == "skewY" && !args.empty()) {
            local = Affine{1, std::tan(args[0] * std::numbers::pi / 180.0), 0, 1, 0, 0};
        } else if (name == "matrix" && args.size() >= 6) {
            local = Affine{args[0], args[1], args[2], args[3], args[4], args[5]};
        } else {
            continue;  // fonction inconnue ou arguments insuffisants : ignorée
        }
        result = compose(result, local);
    }
    return result;
}

// --- Grammaire des données de chemin (`d`) ------------------------------
// M/m L/l H/h V/v C/c S/s Q/q T/t A/a Z/z, absolu/relatif, répétition
// implicite (une commande sans lettre répète la précédente -- sauf M/m,
// qui se change en L/l implicite après le premier point, cf. spec §9.3.3).

struct PathCursor {
    V2 current{};
    V2 subpathStart{};
    // Point de contrôle réfléchi pour S/s (cubique) et T/t (quadratique) --
    // seulement valide si la commande précédente était du même type de
    // courbe, sinon on réfléchit le point courant lui-même (spec §9.3.6/7).
    std::optional<V2> lastCubicControl;
    std::optional<V2> lastQuadControl;
};

void end_open_subpath(std::vector<DPath>& out, DPath& subpath) {
    if (subpath.nodes.size() >= 2) out.push_back(std::move(subpath));
    subpath = DPath{};
}

// Convertit un arc quadratique (point de contrôle unique) en tangentes
// cubiques ÉQUIVALENTES EXACTEMENT (formule standard, pas une
// approximation) : cp_cubic1 = start + 2/3*(cp_quad - start), cp_cubic2 =
// end + 2/3*(cp_quad - end).
void append_quadratic(DPath& subpath, V2 start, V2 control, V2 end, bool smooth) {
    const V2 c1 = start + (control - start) * (2.0 / 3.0);
    const V2 c2 = end + (control - end) * (2.0 / 3.0);
    if (!subpath.nodes.empty()) subpath.nodes.back().tan_out = c1 - start;
    DNode node;
    node.pos = end;
    node.smooth = smooth;
    node.tan_in = c2 - end;
    subpath.nodes.push_back(node);
}

void append_cubic(DPath& subpath, V2 start, V2 c1, V2 c2, V2 end, bool smooth) {
    if (!subpath.nodes.empty()) subpath.nodes.back().tan_out = c1 - start;
    DNode node;
    node.pos = end;
    node.smooth = smooth;
    node.tan_in = c2 - end;
    subpath.nodes.push_back(node);
}

void append_line(DPath& subpath, V2 end) {
    DNode node;
    node.pos = end;
    subpath.nodes.push_back(node);
}

// Arc elliptique (commande A), paramétrisation par extrémités (spec
// annexe B.2) -> paramétrisation centrée -> découpage en segments <= 90°
// -> chaque segment approximé par une Bézier cubique standard (constante
// alpha = 4/3 * tan(delta/4), formule usuelle -- jamais un arc natif,
// `geometry::Path` n'en modélise pas).
void append_arc(DPath& subpath, V2 start, double rx, double ry, double xAxisRotDeg, bool largeArc,
                bool sweep, V2 end, std::vector<std::string>& warnings) {
    if (rx == 0.0 || ry == 0.0 || (start.x == end.x && start.y == end.y)) {
        append_line(subpath, end);
        return;
    }
    rx = std::abs(rx);
    ry = std::abs(ry);
    const double phi = xAxisRotDeg * std::numbers::pi / 180.0;
    const double cosPhi = std::cos(phi), sinPhi = std::sin(phi);

    const V2 mid = (start - end) * 0.5;
    const V2 p1{cosPhi * mid.x + sinPhi * mid.y, -sinPhi * mid.x + cosPhi * mid.y};

    double lambda = (p1.x * p1.x) / (rx * rx) + (p1.y * p1.y) / (ry * ry);
    if (lambda > 1.0) {
        const double s = std::sqrt(lambda);
        rx *= s;
        ry *= s;
    }
    const double rx2 = rx * rx, ry2 = ry * ry;
    const double num = rx2 * ry2 - rx2 * p1.y * p1.y - ry2 * p1.x * p1.x;
    const double den = rx2 * p1.y * p1.y + ry2 * p1.x * p1.x;
    double coef = den > 1e-12 ? std::sqrt(std::max(0.0, num / den)) : 0.0;
    if (largeArc == sweep) coef = -coef;
    const V2 c1{coef * (rx * p1.y / ry), coef * (-ry * p1.x / rx)};
    const V2 mid2 = (start + end) * 0.5;
    const V2 center{cosPhi * c1.x - sinPhi * c1.y + mid2.x, sinPhi * c1.x + cosPhi * c1.y + mid2.y};

    const auto angleOf = [](V2 v) { return std::atan2(v.y, v.x); };
    const V2 startVec{(p1.x - c1.x) / rx, (p1.y - c1.y) / ry};
    const V2 endVec{(-p1.x - c1.x) / rx, (-p1.y - c1.y) / ry};
    double theta1 = angleOf(startVec);
    double deltaTheta = angleOf(endVec) - theta1;
    if (!sweep && deltaTheta > 0.0) deltaTheta -= 2.0 * std::numbers::pi;
    if (sweep && deltaTheta < 0.0) deltaTheta += 2.0 * std::numbers::pi;

    const int segments = std::max(1, static_cast<int>(std::ceil(std::abs(deltaTheta) / (std::numbers::pi / 2.0))));
    const double segDelta = deltaTheta / segments;
    const double alpha = (4.0 / 3.0) * std::tan(segDelta / 4.0);

    V2 prevPoint = start;
    double theta = theta1;
    for (int s = 0; s < segments; ++s) {
        const double thetaNext = theta + segDelta;
        const auto ellipsePoint = [&](double t) {
            const V2 local{rx * std::cos(t), ry * std::sin(t)};
            return V2{cosPhi * local.x - sinPhi * local.y + center.x,
                      sinPhi * local.x + cosPhi * local.y + center.y};
        };
        const auto ellipseTangent = [&](double t) {
            const V2 local{-rx * std::sin(t), ry * std::cos(t)};
            return V2{cosPhi * local.x - sinPhi * local.y, sinPhi * local.x + cosPhi * local.y};
        };
        const V2 p0 = prevPoint;
        const V2 p3 = (s == segments - 1) ? end : ellipsePoint(thetaNext);
        const V2 t0 = ellipseTangent(theta);
        const V2 t1 = ellipseTangent(thetaNext);
        const V2 cp1 = p0 + t0 * alpha;
        const V2 cp2 = p3 - t1 * alpha;
        append_cubic(subpath, p0, cp1, cp2, p3, /*smooth=*/true);
        prevPoint = p3;
        theta = thetaNext;
    }
    (void)warnings;  // réservé si un jour un diagnostic de dégénérescence est ajouté ici
}

std::vector<DPath> parse_path_data(std::string_view d, std::vector<std::string>& warnings) {
    std::vector<DPath> result;
    DPath subpath;
    PathCursor cursor;
    std::size_t i = 0;
    char command = '\0';
    bool haveCommand = false;

    const auto readNum = [&]() -> std::optional<double> {
        skip_ws_and_commas(d, i);
        return parse_number(d, i);
    };
    const auto readFlag = [&]() -> std::optional<bool> {
        skip_ws_and_commas(d, i);
        if (i >= d.size() || (d[i] != '0' && d[i] != '1')) return std::nullopt;
        return d[i++] == '1';
    };

    while (true) {
        skip_ws_and_commas(d, i);
        if (i >= d.size()) break;
        if (std::isalpha(static_cast<unsigned char>(d[i])) != 0) {
            command = d[i++];
            haveCommand = true;
        } else if (!haveCommand) {
            warnings.push_back("chemin SVG : données invalides avant la première commande, ignorées");
            break;
        }
        const bool relative = std::islower(static_cast<unsigned char>(command)) != 0;
        const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(command)));

        switch (upper) {
        case 'M': {
            const auto x = readNum();
            const auto y = readNum();
            if (!x || !y) { haveCommand = false; break; }
            end_open_subpath(result, subpath);
            cursor.current = relative ? cursor.current + V2{*x, *y} : V2{*x, *y};
            cursor.subpathStart = cursor.current;
            subpath.nodes.push_back(DNode{cursor.current, false, std::nullopt, std::nullopt});
            cursor.lastCubicControl.reset();
            cursor.lastQuadControl.reset();
            command = relative ? 'l' : 'L';  // répétitions implicites -> L (spec §9.3.3)
            break;
        }
        case 'L': {
            const auto x = readNum();
            const auto y = readNum();
            if (!x || !y) { haveCommand = false; break; }
            cursor.current = relative ? cursor.current + V2{*x, *y} : V2{*x, *y};
            append_line(subpath, cursor.current);
            cursor.lastCubicControl.reset();
            cursor.lastQuadControl.reset();
            break;
        }
        case 'H': {
            const auto x = readNum();
            if (!x) { haveCommand = false; break; }
            cursor.current = {relative ? cursor.current.x + *x : *x, cursor.current.y};
            append_line(subpath, cursor.current);
            cursor.lastCubicControl.reset();
            cursor.lastQuadControl.reset();
            break;
        }
        case 'V': {
            const auto y = readNum();
            if (!y) { haveCommand = false; break; }
            cursor.current = {cursor.current.x, relative ? cursor.current.y + *y : *y};
            append_line(subpath, cursor.current);
            cursor.lastCubicControl.reset();
            cursor.lastQuadControl.reset();
            break;
        }
        case 'C': {
            const auto x1 = readNum(), y1 = readNum(), x2 = readNum(), y2 = readNum(), x = readNum(), y = readNum();
            if (!x1 || !y1 || !x2 || !y2 || !x || !y) { haveCommand = false; break; }
            const V2 start = cursor.current;
            const V2 c1 = relative ? start + V2{*x1, *y1} : V2{*x1, *y1};
            const V2 c2 = relative ? start + V2{*x2, *y2} : V2{*x2, *y2};
            cursor.current = relative ? start + V2{*x, *y} : V2{*x, *y};
            append_cubic(subpath, start, c1, c2, cursor.current, true);
            cursor.lastCubicControl = c2;
            cursor.lastQuadControl.reset();
            break;
        }
        case 'S': {
            const auto x2 = readNum(), y2 = readNum(), x = readNum(), y = readNum();
            if (!x2 || !y2 || !x || !y) { haveCommand = false; break; }
            const V2 start = cursor.current;
            const V2 c1 = cursor.lastCubicControl ? start * 2.0 - *cursor.lastCubicControl : start;
            const V2 c2 = relative ? start + V2{*x2, *y2} : V2{*x2, *y2};
            cursor.current = relative ? start + V2{*x, *y} : V2{*x, *y};
            append_cubic(subpath, start, c1, c2, cursor.current, true);
            cursor.lastCubicControl = c2;
            cursor.lastQuadControl.reset();
            break;
        }
        case 'Q': {
            const auto x1 = readNum(), y1 = readNum(), x = readNum(), y = readNum();
            if (!x1 || !y1 || !x || !y) { haveCommand = false; break; }
            const V2 start = cursor.current;
            const V2 c = relative ? start + V2{*x1, *y1} : V2{*x1, *y1};
            cursor.current = relative ? start + V2{*x, *y} : V2{*x, *y};
            append_quadratic(subpath, start, c, cursor.current, true);
            cursor.lastQuadControl = c;
            cursor.lastCubicControl.reset();
            break;
        }
        case 'T': {
            const auto x = readNum(), y = readNum();
            if (!x || !y) { haveCommand = false; break; }
            const V2 start = cursor.current;
            const V2 c = cursor.lastQuadControl ? start * 2.0 - *cursor.lastQuadControl : start;
            cursor.current = relative ? start + V2{*x, *y} : V2{*x, *y};
            append_quadratic(subpath, start, c, cursor.current, true);
            cursor.lastQuadControl = c;
            cursor.lastCubicControl.reset();
            break;
        }
        case 'A': {
            const auto rx = readNum(), ry = readNum(), rot = readNum();
            const auto large = readFlag(), sweep = readFlag();
            const auto x = readNum(), y = readNum();
            if (!rx || !ry || !rot || !large || !sweep || !x || !y) { haveCommand = false; break; }
            const V2 start = cursor.current;
            cursor.current = relative ? start + V2{*x, *y} : V2{*x, *y};
            append_arc(subpath, start, *rx, *ry, *rot, *large, *sweep, cursor.current, warnings);
            cursor.lastCubicControl.reset();
            cursor.lastQuadControl.reset();
            break;
        }
        case 'Z': {
            subpath.closed = true;
            cursor.current = cursor.subpathStart;
            end_open_subpath(result, subpath);
            cursor.lastCubicControl.reset();
            cursor.lastQuadControl.reset();
            break;
        }
        default:
            warnings.push_back(std::string("chemin SVG : commande non reconnue '") + command + "', ignorée");
            haveCommand = false;
            break;
        }
    }
    end_open_subpath(result, subpath);
    return result;
}

// --- Formes géométriques primitives -> DPath (espace local, avant
// transform) ---------------------------------------------------------

DPath ellipse_shape(V2 center, double rx, double ry) {
    // Même construction que geometry::ellipse_path (4 nœuds lisses, kappa
    // ~0,5523) mais en double précision locale, avant transform. Formule
    // standard vérifiée par calcul direct (tangente à chaque point
    // cardinal, cf. commentaire au point d'appel) -- pas une simple
    // symétrie du nœud précédent, qui donnerait une courbe fausse.
    constexpr double kKappa = 0.5522847498307936;
    const double kx = rx * kKappa, ky = ry * kKappa;
    auto node = [&](V2 pos, std::optional<V2> tanIn, std::optional<V2> tanOut) {
        DNode n;
        n.pos = pos;
        n.smooth = true;
        n.tan_in = tanIn;
        n.tan_out = tanOut;
        return n;
    };
    DPath path;
    path.closed = true;
    path.nodes = {
        node({center.x + rx, center.y}, V2{0, -ky}, V2{0, ky}),   // droite
        node({center.x, center.y + ry}, V2{kx, 0}, V2{-kx, 0}),   // "haut" (+Y local)
        node({center.x - rx, center.y}, V2{0, ky}, V2{0, -ky}),   // gauche
        node({center.x, center.y - ry}, V2{-kx, 0}, V2{kx, 0}),   // "bas" (-Y local)
    };
    return path;
}

DPath rect_shape(double x, double y, double w, double h) {
    DPath path;
    path.closed = true;
    path.nodes = {
        DNode{{x, y}, false, std::nullopt, std::nullopt},
        DNode{{x + w, y}, false, std::nullopt, std::nullopt},
        DNode{{x + w, y + h}, false, std::nullopt, std::nullopt},
        DNode{{x, y + h}, false, std::nullopt, std::nullopt},
    };
    return path;
}

DPath polyline_shape(std::string_view points, bool close, std::vector<std::string>& warnings) {
    DPath path;
    path.closed = close;
    std::size_t i = 0;
    while (true) {
        skip_ws_and_commas(points, i);
        if (i >= points.size()) break;
        const auto x = parse_number(points, i);
        skip_ws_and_commas(points, i);
        const auto y = parse_number(points, i);
        if (!x || !y) {
            if (!path.nodes.empty()) warnings.push_back("points/polyline SVG : coordonnées incomplètes ignorées");
            break;
        }
        path.nodes.push_back(DNode{{*x, *y}, false, std::nullopt, std::nullopt});
    }
    return path;
}

// --- Application du transform accumulé à un DPath (positions ET tangentes,
// cf. Affine::apply_vector pour ces dernières) ---------------------------

DPath transform_path(const DPath& path, const Affine& t) {
    DPath out;
    out.closed = path.closed;
    out.nodes.reserve(path.nodes.size());
    for (const auto& n : path.nodes) {
        DNode tn;
        tn.pos = t.apply_point(n.pos);
        tn.smooth = n.smooth;
        if (n.tan_in) tn.tan_in = t.apply_vector(*n.tan_in);
        if (n.tan_out) tn.tan_out = t.apply_vector(*n.tan_out);
        out.nodes.push_back(tn);
    }
    return out;
}

// --- Contexte de parcours récursif du DOM -------------------------------

struct ParseContext {
    // Un groupe par élément de forme top-level (<path>/<rect>/<circle>/...),
    // DÉJÀ transformé en unités utilisateur globales. Un <path> peut
    // contenir PLUSIEURS sous-tracés dans un même groupe (plusieurs M...Z --
    // c'est exactement le cas où la reconstruction extérieur/trous a un
    // sens) ; toute autre forme ne produit jamais qu'un seul sous-tracé par
    // groupe. Regroupement PAR ÉLÉMENT (jamais global) : deux <path>
    // distincts qui se chevauchent ne sont volontairement PAS analysés pour
    // une relation trou/extérieur entre eux (§ portée "basique", cf. doc de
    // svg_import.hpp).
    std::vector<std::vector<DPath>> shapeGroups;
    std::vector<std::string> warnings;
};

void warn_unsupported(ParseContext& ctx, const pugi::xml_node& node) {
    const std::string_view name = node.name();
    static const std::vector<std::string_view> kNoisy = {"defs", "title", "desc", "metadata", "sodipodi:namedview",
                                                          "style", "namedview"};
    for (const auto& n : kNoisy) {
        if (name == n) return;  // définitions/métadonnées : jamais un manque visible, pas de diagnostic
    }
    std::string message = "élément SVG non pris en charge ignoré : <" + std::string(name) + ">";
    if (const auto id = node.attribute("id"); id) {
        message += " (id=\"" + std::string(id.value()) + "\")";
    }
    ctx.warnings.push_back(std::move(message));
}

void walk(const pugi::xml_node& node, const Affine& parentTransform, ParseContext& ctx);

void walk_children(const pugi::xml_node& node, const Affine& transform, ParseContext& ctx) {
    for (const auto& child : node.children()) {
        if (child.type() == pugi::node_element) walk(child, transform, ctx);
    }
}

double attr_len(const pugi::xml_node& node, const char* name, double fallback = 0.0) {
    const auto a = node.attribute(name);
    if (!a) return fallback;
    return parse_length(a.value(), fallback).user_units;
}

void walk(const pugi::xml_node& node, const Affine& parentTransform, ParseContext& ctx) {
    const std::string_view tag = node.name();
    Affine transform = parentTransform;
    if (const auto t = node.attribute("transform"); t) {
        transform = compose(parentTransform, parse_transform(t.value()));
    }
    // `display="none"` : élément explicitement masqué, jamais importé (mais
    // pas non plus un diagnostic -- c'est une intention normale de l'auteur,
    // pas une fonctionnalité manquante).
    if (const auto display = node.attribute("display"); display && std::string_view(display.value()) == "none") {
        return;
    }

    if (tag == "svg" || tag == "g" || tag == "a") {
        walk_children(node, transform, ctx);
        return;
    }
    if (tag == "path") {
        const auto d = node.attribute("d");
        if (!d) return;
        auto subpaths = parse_path_data(d.value(), ctx.warnings);
        if (subpaths.empty()) return;
        std::vector<DPath> group;
        group.reserve(subpaths.size());
        for (auto& sp : subpaths) group.push_back(transform_path(sp, transform));
        ctx.shapeGroups.push_back(std::move(group));
        return;
    }
    if (tag == "rect") {
        const double x = attr_len(node, "x");
        const double y = attr_len(node, "y");
        const double w = attr_len(node, "width");
        const double h = attr_len(node, "height");
        if (w <= 0.0 || h <= 0.0) return;
        if (node.attribute("rx") || node.attribute("ry")) {
            ctx.warnings.push_back("<rect> : coins arrondis (rx/ry) aplatis en rectangle droit");
        }
        ctx.shapeGroups.push_back({transform_path(rect_shape(x, y, w, h), transform)});
        return;
    }
    if (tag == "circle") {
        const double cx = attr_len(node, "cx");
        const double cy = attr_len(node, "cy");
        const double r = attr_len(node, "r");
        if (r <= 0.0) return;
        ctx.shapeGroups.push_back({transform_path(ellipse_shape({cx, cy}, r, r), transform)});
        return;
    }
    if (tag == "ellipse") {
        const double cx = attr_len(node, "cx");
        const double cy = attr_len(node, "cy");
        const double rx = attr_len(node, "rx");
        const double ry = attr_len(node, "ry");
        if (rx <= 0.0 || ry <= 0.0) return;
        ctx.shapeGroups.push_back({transform_path(ellipse_shape({cx, cy}, rx, ry), transform)});
        return;
    }
    if (tag == "line") {
        DPath path;
        path.closed = false;
        path.nodes = {DNode{{attr_len(node, "x1"), attr_len(node, "y1")}, false, std::nullopt, std::nullopt},
                     DNode{{attr_len(node, "x2"), attr_len(node, "y2")}, false, std::nullopt, std::nullopt}};
        ctx.shapeGroups.push_back({transform_path(path, transform)});
        return;
    }
    if (tag == "polyline" || tag == "polygon") {
        const auto points = node.attribute("points");
        if (!points) return;
        auto path = polyline_shape(points.value(), /*close=*/tag == "polygon", ctx.warnings);
        if (path.nodes.size() < 2) return;
        ctx.shapeGroups.push_back({transform_path(path, transform)});
        return;
    }
    warn_unsupported(ctx, node);
}

geometry::Path to_geometry_path(const DPath& path, double scaleUserUnitsToMm, V2 centerOffsetUserUnits) {
    geometry::Path out;
    out.closed = path.closed;
    out.nodes.reserve(path.nodes.size());
    const auto convert = [&](V2 p) {
        const double xMm = (p.x - centerOffsetUserUnits.x) * scaleUserUnitsToMm;
        const double yMm = (p.y - centerOffsetUserUnits.y) * scaleUserUnitsToMm;
        return Vec2um{to_micrometers(Millimeters{xMm}), to_micrometers(Millimeters{-yMm})};  // Y : SVG bas -> modèle haut
    };
    const auto convertVec = [&](V2 v) {
        const double xMm = v.x * scaleUserUnitsToMm;
        const double yMm = v.y * scaleUserUnitsToMm;
        return Vec2um{to_micrometers(Millimeters{xMm}), to_micrometers(Millimeters{-yMm})};
    };
    for (const auto& n : path.nodes) {
        geometry::PathNode gn;
        gn.pos = convert(n.pos);
        gn.type = n.smooth ? geometry::NodeType::Smooth : geometry::NodeType::Corner;
        if (n.tan_in) gn.tan_in = convertVec(*n.tan_in);
        if (n.tan_out) gn.tan_out = convertVec(*n.tan_out);
        out.nodes.push_back(gn);
    }
    return out;
}

// Point dans polygone (ray casting standard) sur une polyligne APLATIE --
// utilisée UNIQUEMENT pour déterminer la hiérarchie extérieur/trous d'un
// groupe de sous-tracés (jamais pour construire la géométrie finale, qui
// reste la version courbe originale). Cf. justification au point d'appel :
// `geometry::clean_to_path_sets` (Clipper2) ignore purement et simplement
// tan_in/tan_out -- inutilisable ici sans détruire toute courbe.
bool point_in_polygon(const std::vector<Vec2um>& poly, Vec2um p) {
    bool inside = false;
    const std::size_t n = poly.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const double xi = static_cast<double>(poly[i].x.value), yi = static_cast<double>(poly[i].y.value);
        const double xj = static_cast<double>(poly[j].x.value), yj = static_cast<double>(poly[j].y.value);
        const double px = static_cast<double>(p.x.value), py = static_cast<double>(p.y.value);
        if (((yi > py) != (yj > py)) && (px < (xj - xi) * (py - yi) / (yj - yi) + xi)) inside = !inside;
    }
    return inside;
}

// Regroupe les sous-tracés FERMÉS d'un même élément SVG en PathSet
// (extérieur + trous) par profondeur d'imbrication (pair = extérieur,
// impair = trou de son ancêtre immédiat) -- même principe que la règle
// pair-impair SVG, mais calculé ICI sur la géométrie COURBE d'origine
// (seule la version aplatie, temporaire, sert au test d'appartenance).
// Déterministe : ordre de sortie = ordre d'apparition des extérieurs dans
// `closedPaths`, jamais un ordre dépendant d'un hash.
std::vector<geometry::PathSet> group_by_nesting(const std::vector<geometry::Path>& closedPaths) {
    const std::size_t n = closedPaths.size();
    constexpr Micrometers kFlattenTolerance{20};  // 0,02 mm : bien sous toute résolution utile
    std::vector<geometry::Polyline> flat(n);
    for (std::size_t i = 0; i < n; ++i) flat[i] = geometry::flatten(closedPaths[i], kFlattenTolerance);

    // Profondeur d'imbrication de chaque sous-tracé (nombre d'AUTRES
    // sous-tracés du même groupe qui le contiennent). L'ancêtre immédiat
    // (le plus imbriqué des conteneurs) n'est déterminé qu'à la passe
    // suivante, une fois toutes les profondeurs connues.
    std::vector<int> depth(n, 0);
    std::vector<std::optional<std::size_t>> parent(n);
    for (std::size_t j = 0; j < n; ++j) {
        if (flat[j].points.empty()) continue;
        const Vec2um sample = flat[j].points.front();
        for (std::size_t i = 0; i < n; ++i) {
            if (i == j || flat[i].points.empty()) continue;
            if (point_in_polygon(flat[i].points, sample)) ++depth[j];
        }
    }
    for (std::size_t j = 0; j < n; ++j) {
        if (depth[j] % 2 == 0) continue;  // extérieur : pas de parent à trouver
        std::optional<std::size_t> best;
        for (std::size_t i = 0; i < n; ++i) {
            if (i == j || flat[i].points.empty() || flat[j].points.empty()) continue;
            if (!point_in_polygon(flat[i].points, flat[j].points.front())) continue;
            if (!best || depth[i] > depth[*best]) best = i;
        }
        parent[j] = best;
    }

    std::vector<geometry::PathSet> result;
    std::vector<std::optional<std::size_t>> outerToResultIndex(n);
    for (std::size_t j = 0; j < n; ++j) {
        if (depth[j] % 2 != 0) continue;
        outerToResultIndex[j] = result.size();
        result.push_back(geometry::PathSet{closedPaths[j], {}});
    }
    for (std::size_t j = 0; j < n; ++j) {
        if (depth[j] % 2 == 0 || !parent[j]) continue;
        const auto resultIdx = outerToResultIndex[*parent[j]];
        if (!resultIdx) continue;  // parent introuvable (dégénéré) : trou orphelin ignoré, jamais un crash
        result[*resultIdx].holes.push_back(closedPaths[j]);
    }
    return result;
}

}  // namespace

Result<SvgImportResult> decode_svg(std::span<const std::uint8_t> bytes) {
    pugi::xml_document doc;
    const auto parseResult =
        doc.load_buffer(bytes.data(), bytes.size(), pugi::parse_default, pugi::encoding_utf8);
    if (!parseResult) {
        return fail(ErrorCategory::InvalidFile, "SVG invalide (XML mal formé)", parseResult.description());
    }
    const pugi::xml_node root = doc.child("svg");
    if (!root) {
        return fail(ErrorCategory::InvalidFile, "Aucun élément racine <svg> trouvé");
    }

    // --- Taille physique et système d'unités utilisateur (cf. doc de
    // svg_import.hpp pour les 3 cas couverts) --------------------------
    const auto widthAttr = root.attribute("width");
    const auto heightAttr = root.attribute("height");
    const auto viewBoxAttr = root.attribute("viewBox");

    double vbMinX = 0.0, vbMinY = 0.0, vbWidth = 0.0, vbHeight = 0.0;
    bool haveViewBox = false;
    if (viewBoxAttr) {
        const std::string_view vb = viewBoxAttr.value();
        std::size_t i = 0;
        const auto a = [&] { skip_ws_and_commas(vb, i); return parse_number(vb, i); };
        const auto x0 = a(), y0 = a(), w0 = a(), h0 = a();
        if (x0 && y0 && w0 && h0 && *w0 > 0.0 && *h0 > 0.0) {
            vbMinX = *x0;
            vbMinY = *y0;
            vbWidth = *w0;
            vbHeight = *h0;
            haveViewBox = true;
        }
    }

    const ParsedLength widthLen = widthAttr ? parse_length(widthAttr.value()) : ParsedLength{};
    const ParsedLength heightLen = heightAttr ? parse_length(heightAttr.value()) : ParsedLength{};
    const bool haveWidth = widthAttr && !widthLen.had_percent && widthLen.user_units > 0.0;
    const bool haveHeight = heightAttr && !heightLen.had_percent && heightLen.user_units > 0.0;

    double userUnitsWidth, userUnitsHeight;   // étendue du contenu, en unités utilisateur (== espace des coordonnées de `d`)
    double physicalWidthUserUnits;            // largeur physique CIBLE, dans la MÊME base (userUnits == px @96dpi)
    if (haveViewBox) {
        userUnitsWidth = vbWidth;
        userUnitsHeight = vbHeight;
        physicalWidthUserUnits = haveWidth ? widthLen.user_units : (haveHeight ? heightLen.user_units * (vbWidth / vbHeight) : vbWidth);
    } else if (haveWidth && haveHeight) {
        // Pas de viewBox : les coordonnées du fichier sont directement dans
        // l'unité déclarée par width/height (cas courant des SVG générés
        // simplement, ex. exports de script) -- userUnits == cette unité.
        userUnitsWidth = widthLen.user_units;
        userUnitsHeight = heightLen.user_units;
        physicalWidthUserUnits = userUnitsWidth;
    } else {
        // Ni viewBox ni taille exploitable : repli sur la boîte englobante
        // du contenu, calculée après le parcours (cf. plus bas). 96 px/pouce
        // par défaut (valeur normative), 1 unité utilisateur == 1 px.
        userUnitsWidth = 0.0;   // recalculé après coup
        userUnitsHeight = 0.0;
        physicalWidthUserUnits = 0.0;
    }

    // --- Parcours du DOM : accumule tous les sous-tracés en unités
    // utilisateur (espace du viewBox, ou espace direct de `d` si pas de
    // viewBox -- la Affine identité initiale ne fait aucune distinction,
    // c'est la conversion finale ci-dessous qui applique l'échelle et le
    // centrage). ----------------------------------------------------------
    ParseContext ctx;
    walk_children(root, Affine{}, ctx);

    if (userUnitsWidth <= 0.0 || userUnitsHeight <= 0.0) {
        double minX = std::numeric_limits<double>::max(), minY = std::numeric_limits<double>::max();
        double maxX = std::numeric_limits<double>::lowest(), maxY = std::numeric_limits<double>::lowest();
        for (const auto& group : ctx.shapeGroups) {
            for (const auto& p : group) {
                for (const auto& n : p.nodes) {
                    minX = std::min(minX, n.pos.x);
                    maxX = std::max(maxX, n.pos.x);
                    minY = std::min(minY, n.pos.y);
                    maxY = std::max(maxY, n.pos.y);
                }
            }
        }
        if (minX > maxX) {
            return fail(ErrorCategory::UserInput, "Aucune géométrie exploitable dans ce SVG");
        }
        vbMinX = minX;
        vbMinY = minY;
        userUnitsWidth = maxX - minX;
        userUnitsHeight = maxY - minY;
        physicalWidthUserUnits = userUnitsWidth;
        haveViewBox = true;  // réutilise le même chemin de centrage ci-dessous
    }

    // mm par unité utilisateur : physicalWidthUserUnits est déjà en "unités
    // utilisateur == px @96dpi" (cf. parse_length) -- convertit vers mm.
    constexpr double kMmPerUserUnit96dpi = 25.4 / 96.0;
    const double scaleUserUnitsToMm = (physicalWidthUserUnits / userUnitsWidth) * kMmPerUserUnit96dpi;

    const V2 center{vbMinX + userUnitsWidth * 0.5, vbMinY + userUnitsHeight * 0.5};

    // Par groupe (== par élément SVG top-level) : les sous-tracés FERMÉS
    // sont reconstruits en hiérarchie extérieur/trous (`group_by_nesting`,
    // qui préserve les tangentes -- jamais `geometry::clean_to_path_sets`,
    // qui les détruit purement et simplement en passant par Clipper2). Les
    // sous-tracés OUVERTS (une simple ligne/courbe, jamais une surface)
    // deviennent chacun leur propre PathSet sans trou, sans passer par
    // aucune reconstruction de hiérarchie (qui ne fait sens que pour une
    // surface fermée).
    std::vector<geometry::PathSet> objects;
    for (const auto& group : ctx.shapeGroups) {
        std::vector<geometry::Path> closed;
        for (const auto& p : group) {
            if (p.nodes.size() < 2) continue;
            geometry::Path converted = to_geometry_path(p, scaleUserUnitsToMm, center);
            if (converted.closed) {
                closed.push_back(std::move(converted));
            } else {
                objects.push_back(geometry::PathSet{std::move(converted), {}});
            }
        }
        if (!closed.empty()) {
            auto nested = group_by_nesting(closed);
            for (auto& ps : nested) objects.push_back(std::move(ps));
        }
    }
    if (objects.empty()) {
        return fail(ErrorCategory::UserInput, "Aucune géométrie exploitable dans ce SVG");
    }

    SvgImportResult result;
    result.objects = std::move(objects);
    result.warnings = std::move(ctx.warnings);
    return result;
}

Result<SvgImportResult> read_svg_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return fail(ErrorCategory::UserInput, "Fichier introuvable ou illisible : " + path.string());
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return decode_svg(bytes);
}

}  // namespace openstitch::formats
