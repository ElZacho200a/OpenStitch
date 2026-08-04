// SPDX-License-Identifier: Apache-2.0
#include "openstitch/auto_satin/satin_column.hpp"

#include "openstitch/geometry/polyline.hpp"
#include "openstitch/geometry/simplify.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <expected>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <string>

namespace openstitch::auto_satin {

namespace {

// Travail en µm double, repère modèle (Y vers le haut).
struct P2 {
    double x{0.0};
    double y{0.0};
};

P2 operator+(P2 a, P2 b) {
    return {a.x + b.x, a.y + b.y};
}
P2 operator-(P2 a, P2 b) {
    return {a.x - b.x, a.y - b.y};
}
P2 operator*(P2 a, double s) {
    return {a.x * s, a.y * s};
}
double dot(P2 a, P2 b) {
    return a.x * b.x + a.y * b.y;
}
double cross(P2 a, P2 b) {
    return a.x * b.y - a.y * b.x;
}
double norm(P2 a) {
    return std::sqrt(dot(a, a));
}
P2 unit(P2 a) {
    const double n = norm(a);
    return n > 1e-9 ? P2{a.x / n, a.y / n} : P2{0.0, 0.0};
}

using Poly = std::vector<P2>;

std::vector<Poly> region_polys(const geometry::PathSet& region) {
    std::vector<Poly> polys;
    const auto add = [&](const geometry::Path& path) {
        Poly poly;
        poly.reserve(path.nodes.size());
        for (const auto& n : path.nodes) {
            poly.push_back(
                {static_cast<double>(n.pos.x.value), static_cast<double>(n.pos.y.value)});
        }
        if (poly.size() >= 3) {
            polys.push_back(std::move(poly));
        }
    };
    add(region.outer);
    for (const auto& h : region.holes) {
        add(h);
    }
    return polys;
}

bool point_in_poly(const Poly& poly, P2 p) {
    bool inside = false;
    const std::size_t n = poly.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const P2 a = poly[i];
        const P2 b = poly[j];
        if (((a.y > p.y) != (b.y > p.y)) && (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)) {
            inside = !inside;
        }
    }
    return inside;
}

bool in_region(const std::vector<Poly>& polys, P2 p) {
    if (polys.empty() || !point_in_poly(polys[0], p)) {
        return false;
    }
    for (std::size_t i = 1; i < polys.size(); ++i) {
        if (point_in_poly(polys[i], p)) {
            return false;
        }
    }
    return true;
}

// Distance de `p` au bord le plus proche (tous polygones confondus) — utilisé
// pour tolérer un point de rail exactement SUR le contour (§ validation
// paramétrique, étape 12) : `in_region` seul est ambigu pile sur une arête.
double distance_to_polys(const std::vector<Poly>& polys, P2 p) {
    double best = std::numeric_limits<double>::max();
    for (const auto& poly : polys) {
        const std::size_t n = poly.size();
        for (std::size_t i = 0; i < n; ++i) {
            const P2 a = poly[i];
            const P2 b = poly[(i + 1) % n];
            const P2 ab = b - a;
            const double len2 = dot(ab, ab);
            const double t = len2 > 1e-12 ? std::clamp(dot(p - a, ab) / len2, 0.0, 1.0) : 0.0;
            const P2 proj = a + ab * t;
            best = std::min(best, norm(p - proj));
        }
    }
    return best;
}

// Raison d'échec explicite d'une section transversale (§ audit génération
// partielle, formes concaves/larges) : `cross_section` renvoyait auparavant un
// simple `nullopt` indifférencié, que l'appelant se contentait d'ignorer via
// `continue` — la station disparaissait silencieusement de l'axe et les deux
// stations valides encadrant le trou se retrouvaient reconnectées directement,
// produisant de larges zones non couvertes, des rails discontinus ou des
// éventails/pointes artificielles sur les formes concaves. Distinguer la
// raison permet à l'appelant de décider (interpoler un échec isolé, ou refuser
// proprement la colonne) au lieu de recoller silencieusement le trou, et donne
// un diagnostic exploitable dans `SatinColumnsResult::warnings`.
enum class CrossSectionFailure {
    AxisOutsideRegion,           // le point d'axe A lui-même n'est pas strictement intérieur
    MissingNegativeIntersection, // aucune intersection trouvée côté -N
    MissingPositiveIntersection, // aucune intersection trouvée côté +N
    TooWide,               // intervalle plus large que max_width (normale quasi parallèle au bord)
    IntervalOutsideRegion, // intervalle trouvé mais son milieu retombe hors région
};

const char* describe(CrossSectionFailure f) {
    switch (f) {
    case CrossSectionFailure::AxisOutsideRegion:
        return "axe hors region";
    case CrossSectionFailure::MissingNegativeIntersection:
        return "intersection negative manquante";
    case CrossSectionFailure::MissingPositiveIntersection:
        return "intersection positive manquante";
    case CrossSectionFailure::TooWide:
        return "largeur superieure a max_width";
    case CrossSectionFailure::IntervalOutsideRegion:
        return "intervalle invalide (milieu hors region)";
    }
    return "echec inconnu";
}

// Section transversale : intersecte la droite (A, direction N unitaire) avec le
// contour de la région et retourne l'intervalle intérieur encadrant A :
// t_lo < 0 < t_hi (extrémités du côté -N et +N). Échoue (raison explicite,
// voir `CrossSectionFailure`) si A n'est pas strictement intérieur, si
// l'intervalle n'encadre pas A, ou s'il est plus large que `max_width`
// (normale quasi parallèle au bord -> section aberrante).
std::expected<std::pair<double, double>, CrossSectionFailure>
cross_section(const std::vector<Poly>& polys, P2 A, P2 N, double max_width) {
    if (!in_region(polys, A)) {
        return std::unexpected(CrossSectionFailure::AxisOutsideRegion);
    }
    std::vector<double> ts;
    for (const auto& poly : polys) {
        const std::size_t n = poly.size();
        for (std::size_t i = 0; i < n; ++i) {
            const P2 p = poly[i];
            const P2 q = poly[(i + 1) % n];
            const P2 d = q - p;
            const double det = -N.x * d.y + d.x * N.y;
            if (std::abs(det) < 1e-9) {
                continue; // parallèle
            }
            const P2 r = p - A;
            const double t = (-r.x * d.y + d.x * r.y) / det;
            const double s = (N.x * r.y - N.y * r.x) / det;
            if (s >= -1e-9 && s <= 1.0 + 1e-9) {
                ts.push_back(t);
            }
        }
    }
    double lo = -std::numeric_limits<double>::infinity();
    double hi = std::numeric_limits<double>::infinity();
    for (const double t : ts) {
        if (t < -1e-6 && t > lo) {
            lo = t;
        }
        if (t > 1e-6 && t < hi) {
            hi = t;
        }
    }
    if (!std::isfinite(lo)) {
        return std::unexpected(CrossSectionFailure::MissingNegativeIntersection);
    }
    if (!std::isfinite(hi)) {
        return std::unexpected(CrossSectionFailure::MissingPositiveIntersection);
    }
    if (hi - lo > max_width) {
        return std::unexpected(CrossSectionFailure::TooWide);
    }
    if (!in_region(polys, A + N * ((lo + hi) * 0.5))) {
        return std::unexpected(CrossSectionFailure::IntervalOutsideRegion);
    }
    return std::pair{lo, hi};
}

// Lissage Chaikin (coins coupés), extrémités conservées.
std::vector<P2> chaikin(const std::vector<P2>& pts, int iterations) {
    std::vector<P2> cur = pts;
    for (int it = 0; it < iterations && cur.size() >= 3; ++it) {
        std::vector<P2> out;
        out.reserve(cur.size() * 2);
        out.push_back(cur.front());
        for (std::size_t i = 0; i + 1 < cur.size(); ++i) {
            out.push_back(cur[i] * 0.75 + cur[i + 1] * 0.25);
            out.push_back(cur[i] * 0.25 + cur[i + 1] * 0.75);
        }
        out.push_back(cur.back());
        cur = std::move(out);
    }
    return cur;
}

std::vector<P2> resample_arc(const std::vector<P2>& pts, double spacing) {
    std::vector<double> cum{0.0};
    for (std::size_t i = 1; i < pts.size(); ++i) {
        cum.push_back(cum.back() + norm(pts[i] - pts[i - 1]));
    }
    const double total = cum.back();
    if (total < 1e-6) {
        return pts;
    }
    const int n = std::max(2, static_cast<int>(std::lround(total / spacing)) + 1);
    const double step = total / (n - 1);
    std::vector<P2> out;
    out.reserve(static_cast<std::size_t>(n));
    std::size_t seg = 1;
    for (int k = 0; k < n; ++k) {
        const double target = std::min(total, k * step);
        while (seg < cum.size() && cum[seg] < target) {
            ++seg;
        }
        const std::size_t j = std::min(seg, pts.size() - 1);
        const double segLen = cum[j] - cum[j - 1];
        const double f = segLen > 1e-9 ? (target - cum[j - 1]) / segLen : 0.0;
        out.push_back(pts[j - 1] + (pts[j] - pts[j - 1]) * f);
    }
    return out;
}

Vec2um to_um(P2 p) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(p.x))},
                  Micrometers{static_cast<std::int32_t>(std::lround(p.y))}};
}

geometry::Path rail_path(const std::vector<P2>& pts) {
    geometry::Path path;
    path.closed = false;
    path.nodes.reserve(pts.size());
    for (const P2& p : pts) {
        path.nodes.push_back({to_um(p), geometry::NodeType::Corner, std::nullopt, std::nullopt});
    }
    return path;
}

// Intersection propre de deux segments (croisement transversal).
bool segments_cross(P2 a, P2 b, P2 c, P2 d) {
    const auto o = [](P2 p, P2 q, P2 r) { return cross(q - p, r - p); };
    const double o1 = o(a, b, c), o2 = o(a, b, d), o3 = o(c, d, a), o4 = o(c, d, b);
    return (o1 > 0) != (o2 > 0) && (o3 > 0) != (o4 > 0) && o1 != 0 && o2 != 0 && o3 != 0 && o4 != 0;
}

double signed_area(const Poly& poly) {
    double area = 0.0;
    for (std::size_t i = 0; i < poly.size(); ++i) {
        area += cross(poly[i], poly[(i + 1) % poly.size()]);
    }
    return area * 0.5;
}

// --- Représentation paramétrique du contour extérieur -----------------------
//
// Utilisée UNIQUEMENT en diagnostic (calcul de `JunctionCore`, rendu SVG) :
// jamais pour construire ou déplacer un rail. Un polygone fermé + longueur
// curviligne cumulée à chaque sommet, permettant de projeter un point
// quelconque sur le contour et d'en extraire un arc entre deux abscisses.
struct ContourPolyline {
    Poly points;                  // sommets du polygone, dans leur ordre d'origine
    std::vector<double> cumulative;  // longueur cumulée jusqu'au sommet i (cumulative[0] = 0)
    double total_length{0.0};
};

ContourPolyline make_contour_polyline(const Poly& poly) {
    ContourPolyline c;
    c.points = poly;
    c.cumulative.assign(poly.size(), 0.0);
    double acc = 0.0;
    for (std::size_t i = 0; i < poly.size(); ++i) {
        c.cumulative[i] = acc;
        acc += norm(poly[(i + 1) % poly.size()] - poly[i]);
    }
    c.total_length = acc;
    return c;
}

struct ContourProjection {
    std::size_t segment_index{0};  // arête [i, i+1) du polygone
    double segment_t{0.0};         // 0..1 le long de cette arête
    double arc_length{0.0};        // abscisse curviligne du point projeté
    P2 point{};
    double distance{0.0};          // distance point -> contour
};

// Projette `p` sur le contour : plus proche point parmi tous les segments.
ContourProjection project_to_contour(const ContourPolyline& contour, P2 p) {
    ContourProjection best;
    best.distance = std::numeric_limits<double>::max();
    const std::size_t n = contour.points.size();
    for (std::size_t i = 0; i < n; ++i) {
        const P2 a = contour.points[i];
        const P2 b = contour.points[(i + 1) % n];
        const P2 ab = b - a;
        const double len2 = dot(ab, ab);
        const double t = len2 > 1e-12 ? std::clamp(dot(p - a, ab) / len2, 0.0, 1.0) : 0.0;
        const P2 proj = a + ab * t;
        const double d = norm(p - proj);
        if (d < best.distance) {
            best.distance = d;
            best.segment_index = i;
            best.segment_t = t;
            best.point = proj;
            const double segLen = norm(ab);
            best.arc_length = contour.cumulative[i] + t * segLen;
        }
    }
    return best;
}

enum class ContourDirection { Forward, Backward, Shortest };

// Longueur de l'arc de `from` à `to` (abscisses curvilignes) dans le sens
// donné, avec rebouclage sur `total_length` pour Forward/Backward.
double contour_arc_span(double fromArc, double toArc, double totalLength, ContourDirection dir) {
    double fwd = toArc - fromArc;
    while (fwd < 0.0) fwd += totalLength;
    while (fwd > totalLength) fwd -= totalLength;
    const double bwd = totalLength - fwd;
    switch (dir) {
    case ContourDirection::Forward:
        return fwd;
    case ContourDirection::Backward:
        return bwd;
    case ContourDirection::Shortest:
        return std::min(fwd, bwd);
    }
    return fwd;
}

// Segment [i, i+1) contenant l'abscisse `arc` (déjà ramenée dans [0, total)),
// et le point interpolé correspondant.
std::pair<std::size_t, P2> locate_arc(const ContourPolyline& contour, double arc) {
    const std::size_t n = contour.points.size();
    const double total = contour.total_length;
    for (std::size_t i = 0; i < n; ++i) {
        const double segStart = contour.cumulative[i];
        const double segEnd = (i + 1 < n) ? contour.cumulative[i + 1] : total;
        if (arc <= segEnd + 1e-9 || i + 1 == n) {
            const double segLen = segEnd - segStart;
            const double t = segLen > 1e-9 ? std::clamp((arc - segStart) / segLen, 0.0, 1.0) : 0.0;
            return {i, contour.points[i] + (contour.points[(i + 1) % n] - contour.points[i]) * t};
        }
    }
    return {0, contour.points.front()};
}

// Extrait la polyligne du contour entre deux abscisses curvilignes, dans le
// sens indiqué (`Shortest` choisit l'arc le plus court des deux possibles sur
// un contour fermé). Les deux extrémités sont incluses, dans l'ordre de
// parcours choisi (donc éventuellement inversé par rapport à l'indexation du
// polygone d'origine).
Poly extract_contour_arc(const ContourPolyline& contour, double fromArc, double toArc,
                         ContourDirection dir) {
    const double total = contour.total_length;
    const std::size_t n = contour.points.size();
    if (total <= 1e-9 || n == 0) {
        return {};
    }
    double a = std::fmod(fromArc, total);
    if (a < 0.0) a += total;
    double b = std::fmod(toArc, total);
    if (b < 0.0) b += total;

    bool forward = dir != ContourDirection::Backward;
    if (dir == ContourDirection::Shortest) {
        double fwd = b - a;
        while (fwd < 0.0) fwd += total;
        forward = fwd <= total - fwd;
    }
    if (!forward) {
        std::swap(a, b);
    }

    const auto [segA, pointA] = locate_arc(contour, a);
    const auto [segB, pointB] = locate_arc(contour, b);

    Poly out;
    out.push_back(pointA);
    // Sommets du polygone strictement entre `a` et `b`, dans l'ordre croissant
    // d'indexation (= ordre d'abscisse croissante puisqu'on parcourt vers l'avant).
    for (std::size_t k = segA + 1; k <= segB; ++k) {
        out.push_back(contour.points[k % n]);
    }
    out.push_back(pointB);
    if (!forward) {
        std::reverse(out.begin(), out.end());
    }
    return out;
}

Poly open_at_rightmost(Poly poly, double seamY) {
    if (poly.empty())
        return {};
    std::size_t seamEdge = 0;
    P2 seam = poly.front();
    double rightmostX = -std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const P2 a = poly[i];
        const P2 b = poly[(i + 1) % poly.size()];
        if ((a.y <= seamY && seamY <= b.y) || (b.y <= seamY && seamY <= a.y)) {
            double x = std::max(a.x, b.x);
            if (std::abs(b.y - a.y) > 1e-9) {
                const double t = (seamY - a.y) / (b.y - a.y);
                x = a.x + t * (b.x - a.x);
            }
            if (x > rightmostX) {
                rightmostX = x;
                seamEdge = i;
                seam = {x, seamY};
            }
        }
    }
    if (!std::isfinite(rightmostX)) {
        const auto vertex =
            std::max_element(poly.begin(), poly.end(), [](P2 a, P2 b) { return a.x < b.x; });
        seamEdge = static_cast<std::size_t>(vertex - poly.begin());
        seam = *vertex;
    }
    Poly open;
    open.reserve(poly.size() + 2);
    open.push_back(seam);
    for (std::size_t i = 1; i <= poly.size(); ++i) {
        const P2 point = poly[(seamEdge + i) % poly.size()];
        if (norm(point - open.back()) > 1e-9)
            open.push_back(point);
    }
    if (norm(seam - open.back()) > 1e-9)
        open.push_back(seam);
    return open;
}

double polyline_length(const Poly& points) {
    double length = 0.0;
    for (std::size_t i = 1; i < points.size(); ++i)
        length += norm(points[i] - points[i - 1]);
    return length;
}

// Un anneau est un satin ferme, donc un petit reseau cyclique et non une
// colonne ouverte. On ouvre deterministement les deux contours au point le
// plus a droite, les oriente dans le meme sens, puis les decompose en quatre
// sections ouvertes raccordees bout a bout. Les validations ci-dessous
// interdisent de recreer une gerbe ou un noeud papillon sur un anneau trop
// irregulier pour cet appariement automatique.
std::optional<std::vector<SatinColumnGeometry>>
build_annular_sections(const geometry::PathSet& region, const SatinColumnsParameters& params,
                       std::string& refusal) {
    if (region.holes.size() != 1 || region.outer.nodes.size() < 4 ||
        region.holes.front().nodes.size() < 4) {
        refusal = "contours incomplets";
        return std::nullopt;
    }
    const auto polys = region_polys(region);
    Poly outer = polys[0];
    Poly inner = polys[1];
    const double outerArea = signed_area(outer);
    const double innerArea = signed_area(inner);
    if (outerArea * innerArea < 0.0)
        std::reverse(inner.begin(), inner.end());
    const auto [innerMinY, innerMaxY] =
        std::minmax_element(inner.begin(), inner.end(), [](P2 a, P2 b) { return a.y < b.y; });
    const double seamY = (innerMinY->y + innerMaxY->y) * 0.5;
    outer = open_at_rightmost(std::move(outer), seamY);
    inner = open_at_rightmost(std::move(inner), seamY);

    const double outerLength = polyline_length(outer);
    const double innerLength = polyline_length(inner);
    if (outerLength <= 1e-6 || innerLength <= 1e-6) {
        refusal = "perimetre nul";
        return std::nullopt;
    }
    const double stationSpacing =
        static_cast<double>(std::max<std::int32_t>(1, params.station_spacing.value));
    int intervals = std::max(
        4, static_cast<int>(std::ceil(std::max(outerLength, innerLength) / stationSpacing)));
    intervals = ((intervals + 3) / 4) * 4;
    outer = resample_arc(outer, outerLength / intervals);
    inner = resample_arc(inner, innerLength / intervals);
    if (outer.size() != static_cast<std::size_t>(intervals + 1) || outer.size() != inner.size()) {
        refusal = "reechantillonnage incomplet";
        return std::nullopt;
    }

    const double minWidth = static_cast<double>(params.analysis.thresholds.min_satin_width.value);
    const double maxWidth = static_cast<double>(params.analysis.thresholds.max_satin_width.value);
    for (std::size_t i = 0; i < outer.size(); ++i) {
        const double width = norm(outer[i] - inner[i]);
        if (width < minWidth || width > maxWidth) {
            refusal = "largeur hors plage a la station " + std::to_string(i) + " (" +
                      std::to_string(width) + " um)";
            return std::nullopt;
        }
        for (double t : {0.2, 0.4, 0.6, 0.8}) {
            const P2 sample = outer[i] + (inner[i] - outer[i]) * t;
            if (!in_region(polys, sample)) {
                refusal = "barreau hors region a la station " + std::to_string(i);
                return std::nullopt;
            }
        }
        if (i > 0 && (segments_cross(outer[i - 1], inner[i - 1], outer[i], inner[i]) ||
                      segments_cross(outer[i - 1], outer[i], inner[i - 1], inner[i]))) {
            refusal = "croisement local a la station " + std::to_string(i);
            return std::nullopt;
        }
    }
    for (std::size_t i = 0; i + 2 < outer.size(); ++i) {
        for (std::size_t j = i + 2; j < outer.size(); ++j) {
            if (i == 0 && j + 1 == outer.size())
                continue;
            if (segments_cross(outer[i], inner[i], outer[j], inner[j])) {
                refusal =
                    "croisement global des barreaux " + std::to_string(i) + "/" + std::to_string(j);
                return std::nullopt;
            }
        }
    }

    std::vector<SatinColumnGeometry> columns;
    columns.reserve(4);
    const int perSection = intervals / 4;
    const double actualSpacing = std::max(outerLength, innerLength) / intervals;
    const int rungStride =
        std::max(1, static_cast<int>(std::lround(params.rung_max_spacing.value / actualSpacing)));
    for (int section = 0; section < 4; ++section) {
        const int begin = section * perSection;
        const int end = begin + perSection;
        std::vector<P2> railA;
        std::vector<P2> railB;
        railA.reserve(static_cast<std::size_t>(perSection + 1));
        railB.reserve(static_cast<std::size_t>(perSection + 1));
        SatinColumnGeometry column;
        column.section_index = static_cast<std::uint32_t>(section);
        column.section_count = 4;
        column.start_junction = static_cast<std::uint32_t>(section);
        column.end_junction = static_cast<std::uint32_t>((section + 1) % 4);
        column.min_width_um = std::numeric_limits<double>::max();
        for (int i = begin; i <= end; ++i) {
            const auto index = static_cast<std::size_t>(i);
            railA.push_back(outer[index]);
            railB.push_back(inner[index]);
            const double width = norm(outer[index] - inner[index]);
            column.mean_width_um += width;
            column.min_width_um = std::min(column.min_width_um, width);
            column.max_width_um = std::max(column.max_width_um, width);
            if (i == begin || i == end || (i - begin) % rungStride == 0) {
                column.rungs.push_back({to_um(outer[index]), to_um(inner[index])});
            }
            if (i > begin) {
                const P2 previousMid = (outer[index - 1] + inner[index - 1]) * 0.5;
                const P2 currentMid = (outer[index] + inner[index]) * 0.5;
                column.length_um += norm(currentMid - previousMid);
            }
        }
        column.mean_width_um /= static_cast<double>(perSection + 1);
        column.rail_a = rail_path(railA);
        column.rail_b = rail_path(railB);
        columns.push_back(std::move(column));
    }
    return columns;
}

struct Station {
    P2 axis;
    P2 railA; // côté +N (gauche)
    P2 railB; // côté -N (droite)
    P2 tangent;
    double width{0.0};
};

// Comble un échec ISOLÉ de `cross_section` (une seule station manquante entre
// deux stations valides) par interpolation linéaire des rails, plutôt que de
// simplement sauter l'échantillon d'axe et reconnecter directement les deux
// voisines (§ audit génération partielle — c'est cette reconnexion directe,
// silencieuse, qui produisait des rails discontinus / éventails sur les
// formes concaves). Le résultat n'est utilisé que si `interpolated_station_valid`
// le confirme géométriquement plausible.
Station interpolate_station(const Station& prev, const Station& next, P2 axisPt) {
    const double total = norm(next.axis - prev.axis);
    const double t = total > 1e-9 ? std::clamp(norm(axisPt - prev.axis) / total, 0.0, 1.0) : 0.5;
    Station s;
    s.axis = axisPt;
    s.tangent = unit(next.axis - prev.axis);
    s.railA = prev.railA + (next.railA - prev.railA) * t;
    s.railB = prev.railB + (next.railB - prev.railB) * t;
    s.width = norm(s.railA - s.railB);
    return s;
}

// Une station interpolée n'est retenue que si elle reste une géométrie
// plausible : intérieure à la région, largeur non nulle ni aberrante, et ne
// créant aucun croisement avec ses deux voisines directes (rails ou barreau).
// Sinon, le trou n'est pas résoluble par une simple interpolation linéaire —
// la région est trop irrégulière entre les deux stations valides — et
// l'appelant doit refuser la colonne plutôt que de conserver un pont
// géométriquement invalide.
bool interpolated_station_valid(const std::vector<Poly>& polys, const Station& prev,
                                const Station& candidate, const Station& next, double maxWidth) {
    if (!(candidate.width > 0.0) || candidate.width > maxWidth) {
        return false;
    }
    if (!in_region(polys, candidate.axis) ||
        !in_region(polys, (candidate.railA + candidate.railB) * 0.5)) {
        return false;
    }
    if (segments_cross(prev.railA, candidate.railA, prev.railB, candidate.railB) ||
        segments_cross(prev.railA, prev.railB, candidate.railA, candidate.railB)) {
        return false;
    }
    if (segments_cross(candidate.railA, next.railA, candidate.railB, next.railB) ||
        segments_cross(candidate.railA, candidate.railB, next.railA, next.railB)) {
        return false;
    }
    return true;
}

// Étend un bout OUVERT de colonne jusqu'au bord réel de la région (§ audit
// auto-satin : le squelette aminci s'arrête avant un embout arrondi/pointu,
// laissant l'embout entier hors couverture). Marche depuis `base` le long de
// `marchDir` (tangente SORTANTE, unitaire) par pas de `stepLen`, ré-évalue une
// section transversale à chaque pas avec la normale dérivée de `orientTan`
// (tangente d'ORIENTATION, PAS la direction de marche — cette distinction
// compte côté « début » où l'on marche en arrière tout en gardant le même
// repère gauche/droite que le reste de la colonne, sous peine de croisement à
// la jonction entre stations réelles et étendues). S'arrête dès que la section
// échoue, sort de la région, ou cesse de rétrécir (tolérance 5 % — protège
// contre une marche qui déborderait dans une tout autre partie de la forme).
// Referme ensuite par bissection sur `in_region` seul (robuste même quand la
// section transversale devient numériquement dégénérée tout près du bord) :
// le point de fermeture reçoit une largeur PLANCHER non nulle (`tipMinWidth`)
// plutôt qu'un barreau littéralement nul, pour rester exploitable telle
// quelle par `fill_satin_columns` (qui suppose toujours un barreau non
// dégénéré). Bornée (200 pas de marche, 24 de bissection) ; jamais de boucle
// infinie même sur une géométrie pathologique.
std::vector<Station> extend_tip(const std::vector<Poly>& polys, P2 base, P2 marchDir, P2 orientTan,
                                double lastWidth, double maxWidth, double stepLen,
                                double tipMinWidth) {
    std::vector<Station> ext;
    if (stepLen <= 0.0) {
        return ext;
    }
    const P2 nrm{-orientTan.y, orientTan.x};
    double prevWidth = lastWidth;
    P2 prevPoint = base;
    constexpr int kMaxSteps = 200;
    int stepsTaken = 0;
    for (int k = 1; k <= kMaxSteps; ++k) {
        const P2 p = base + marchDir * (static_cast<double>(k) * stepLen);
        if (!in_region(polys, p)) {
            break;
        }
        const auto sec = cross_section(polys, p, nrm, maxWidth);
        if (!sec) {
            break;
        }
        const double width = sec->second - sec->first;
        if (width > prevWidth * 1.05 + 1.0) {
            break; // ne rétrécit plus : on a dépassé la pointe (forme non convexe)
        }
        Station st;
        st.axis = p;
        st.tangent = orientTan;
        st.railA = p + nrm * sec->second;
        st.railB = p + nrm * sec->first;
        st.width = width;
        ext.push_back(st);
        prevPoint = p;
        prevWidth = width;
        stepsTaken = k;
        if (width <= tipMinWidth) {
            return ext; // déjà assez fin : la fermeture par bissection n'apporterait rien
        }
    }
    // Bissection entre le dernier point intérieur connu et un point hors-région
    // pour localiser le bord réel le long de la tangente.
    double sGood = static_cast<double>(stepsTaken) * stepLen;
    double sBad = sGood + stepLen;
    for (int guard = 0; guard < 8 && in_region(polys, base + marchDir * sBad); ++guard) {
        sBad += stepLen;
    }
    for (int it = 0; it < 24; ++it) {
        const double sMid = (sGood + sBad) * 0.5;
        if (in_region(polys, base + marchDir * sMid)) {
            sGood = sMid;
        } else {
            sBad = sMid;
        }
    }
    // Marge de sûreté : la bissection converge à une précision sub-µm du bord
    // ANALYTIQUE, mais `to_um` arrondit au µm le plus proche — sans recul, ce
    // dernier arrondi peut faire retomber le point de l'autre côté du polygone
    // DISCRÉTISÉ (approximation à segments d'un contour courbe). Un recul de
    // quelques µm vers l'intérieur reste invisible à l'échelle broderie (pas
    // DST = 100 µm) et garantit un point robustement intérieur après arrondi.
    const double margin = std::max(2.0, tipMinWidth * 0.1);
    const P2 tip = base + marchDir * std::max(0.0, sGood - margin);
    if (norm(tip - prevPoint) > 1.0) { // évite un point quasi dupliqué (< 1 µm)
        Station closing;
        closing.axis = tip;
        closing.tangent = orientTan;
        const double half = std::max(tipMinWidth * 0.5, 1.0);
        closing.railA = tip + nrm * half;
        closing.railB = tip - nrm * half;
        closing.width = half * 2.0;
        ext.push_back(closing);
    }
    return ext;
}

// Largeur typique (stable) d'une branche : médiane du TIERS MÉDIAN de ses
// stations, en excluant délibérément le premier et le dernier tiers.
//
// Un bourrelet de confluence ne peut que GONFLER la largeur mesurée près
// d'un bout de JONCTION (la normale locale s'écarte alors de la ceinture
// réelle de la branche pour balayer la confluence) — un simple minimum
// global y résiste bien (démontré sur un réseau en T issu d'une image
// segmentée : la médiane globale d'une moitié de barre coupée court par le
// pied vertical était déjà gonflée par la confluence, contrairement au
// minimum). MAIS un bout OUVERT peut à l'inverse RÉTRÉCIR légitimement
// jusqu'à un point (branche effilée en triangle, fixture "trident" :
// largeur ~1200 µm à la base, tendant vers 0 à la pointe) — un minimum
// global capte alors cette pointe et la référence devient quasi nulle,
// faisant paradoxalement tout retirer côté jonction (`limit` minuscule,
// jamais atteint par une largeur de base normale). Exclure le premier et le
// dernier tiers retire à la fois la queue de bourrelet ÉVENTUELLE côté
// jonction et la pointe ÉVENTUELLE côté bout ouvert, quel que soit lequel
// des deux bouts (ou les deux) est concerné, sans avoir à le savoir ici.
double representative_station_width(const std::vector<Station>& st) {
    const std::size_t n = st.size();
    if (n == 0) {
        return 0.0;
    }
    const std::size_t lo = n / 3;
    const std::size_t hi = n - n / 3;
    std::vector<double> widths;
    widths.reserve(n);
    for (std::size_t i = (lo < hi ? lo : 0); i < (lo < hi ? hi : n); ++i) {
        widths.push_back(st[i].width);
    }
    if (widths.empty()) {
        for (const Station& s : st) {
            widths.push_back(s.width);
        }
    }
    std::nth_element(widths.begin(), widths.begin() + widths.size() / 2, widths.end());
    return widths[widths.size() / 2];
}

// Ampute la queue d'une extrémité de JONCTION dont la largeur mesurée dérive
// (§ ancrage des jonctions : la section transversale d'une branche, calculée
// depuis sa seule tangente locale, balaie le bourrelet de la confluence — pas
// la ceinture réelle de CETTE branche — dès qu'on approche du nœud du
// squelette). On retire les stations terminales tant que leur largeur dépasse
// nettement `referenceWidth` (la largeur typique de LA BRANCHE ELLE-MÊME,
// loin de toute jonction — cf. `representative_station_width`), pas seulement tant
// qu'elle décroît d'une station à la suivante : un bourrelet peut retomber en
// PALIER (plusieurs stations consécutives à une largeur quasi identique,
// démontré sur la fixture "trident" — branche latérale : ...1200,1200,26200,
// 26200 — les deux dernières stations sont à peine différentes l'une de
// l'autre bien qu'ÉNORMES par rapport à la branche) ; un critère qui ne
// compare qu'au voisin immédiat s'arrête dès la première paire quasi égale,
// laissant tout le palier en place. Comparer à la largeur typique de la
// branche entière détecte ce palier comme un seul bloc instable.
//
// N'ANCRE PLUS le rail terminal sur un sommet reflex ni sur aucune ancre
// partagée (§ remplacement de l'ancrage par bridges) : le rail terminal
// (post-amputation) reste à sa position brute calculée par `cross_section`,
// verrouillée telle quelle comme `StableBranchEnd` de la colonne — jamais
// déplacée. Cette fonction se limite à trouver la dernière section
// RÉELLEMENT stable. Pas de budget de distance fixe non plus : un ancien
// plafond (lié à `junction_anchor_radius`, qui ne bornait à l'origine que le
// RAYON DE RECHERCHE d'une ancre — une notion qui n'existe plus) arrêtait
// parfois l'amputation alors que la largeur mesurée était encore en pleine
// dérive. Le plancher `st.size() >= 3` reste l'unique garde-fou ; les
// validations de couverture d'axe en aval (`min_axis_coverage_ratio`)
// refusent la colonne si la portion retirée devient excessive.
void trim_unstable_junction_tail(std::vector<Station>& st, bool atEnd, double referenceWidth) {
    constexpr double kToleranceRatio = 1.3;  // 30% au-dessus du gabarit habituel : encore le bourrelet

    if (st.size() < 3 || referenceWidth <= 0.0) {
        return;
    }
    const double limit = referenceWidth * kToleranceRatio;

    while (st.size() >= 3) {
        const Station& terminal = atEnd ? st.back() : st.front();
        if (terminal.width <= limit) {
            break;
        }
        if (atEnd) {
            st.pop_back();
        } else {
            st.erase(st.begin());
        }
    }
}

// Calcule la série DENSE de stations candidates d'une branche (§ étape 3 :
// « conserver les sections comme données d'analyse ») depuis une centerline
// (axe) et les polygones de région : lissage, rééchantillonnage, section
// transversale par station (échec conservé avec sa raison explicite,
// interpolation des échecs isolés sûrs, refus des trous internes), nettoyage
// anti-croisement, amputation de la queue instable de confluence
// (`trim_unstable_junction_tail`), validations de continuité/couverture,
// extension des bouts ouverts jusqu'au bord réel (`extend_tip`).
//
// Partagée par LES DEUX finalisations (`build_column` legacy et
// `build_parametric_object`) : c'est la même observation géométrique dense,
// seule la façon d'en tirer une représentation finale diffère. `extendStart`/
// `extendEnd` : true si ce bout est OUVERT (pas de jonction) et doit donc
// être étendu jusqu'au bord réel ; un bout de jonction est amputé seulement
// (son traitement ultérieur — ancrage legacy ou recouvrement paramétrique —
// est de la responsabilité de l'appelant). `warnings` reçoit un diagnostic
// explicite à chaque station corrigée ou refus (§ audit génération partielle
// sur formes concaves/larges : plus aucune station ni aucun trou n'est
// ignoré en silence — voir les commentaires ci-dessous pour chaque étape).
std::optional<std::vector<Station>> compute_column_stations(const std::vector<Vec2um>& centerline,
                                                             const std::vector<Poly>& polys,
                                                             const SatinColumnsParameters& params,
                                                             bool extendStart, bool extendEnd,
                                                             std::vector<std::string>& warnings) {
    std::vector<P2> axis;
    axis.reserve(centerline.size());
    for (const Vec2um& v : centerline) {
        const P2 p{static_cast<double>(v.x.value), static_cast<double>(v.y.value)};
        if (axis.empty() || norm(p - axis.back()) > 1e-6) {
            axis.push_back(p);
        }
    }
    if (axis.size() < 2) {
        return std::nullopt;
    }
    axis = chaikin(axis, params.axis_smoothing_iterations);
    axis = resample_arc(axis, static_cast<double>(params.station_spacing.value));
    if (axis.size() < 2) {
        return std::nullopt;
    }

    const double maxWidth = static_cast<double>(params.analysis.thresholds.max_satin_width.value);
    const double stationSpacingUm = static_cast<double>(params.station_spacing.value);

    // Section transversale à chaque échantillon d'axe : l'échec est conservé
    // avec sa raison (`CrossSectionFailure`) au lieu d'être ignoré par un
    // simple `continue` (§ audit génération partielle, défaut d'origine).
    struct AxisEntry {
        std::optional<Station> station;
        CrossSectionFailure failure{};
    };
    std::vector<Station> raw;
    // Étendue (en indices d'axe) réellement couverte par `raw`, utilisée pour
    // la validation de couverture ci-dessous : les échecs en tête/queue de
    // l'axe sont un bord légitime (cf. commentaire plus bas) et ne doivent
    // donc PAS compter dans le dénominateur de couverture, seul l'intérieur
    // [firstIdx, lastIdx] importe.
    std::size_t firstIdx = 0;
    std::size_t lastIdx = 0;
    {
        std::vector<AxisEntry> entries(axis.size());
        std::size_t successCount = 0;
        for (std::size_t i = 0; i < axis.size(); ++i) {
            P2 tan;
            if (i == 0) {
                tan = axis[1] - axis[0];
            } else if (i + 1 == axis.size()) {
                tan = axis[i] - axis[i - 1];
            } else {
                tan = axis[i + 1] - axis[i - 1];
            }
            tan = unit(tan);
            const P2 nrm{-tan.y, tan.x}; // +90° : +N = gauche
            const auto sec = cross_section(polys, axis[i], nrm, maxWidth);
            if (sec) {
                Station st;
                st.axis = axis[i];
                st.tangent = tan;
                st.railA = axis[i] + nrm * sec->second; // t_hi > 0
                st.railB = axis[i] + nrm * sec->first;  // t_lo < 0
                st.width = sec->second - sec->first;
                entries[i].station = std::move(st);
                ++successCount;
            } else {
                entries[i].failure = sec.error();
            }
        }
        if (successCount == 0) {
            warnings.push_back("colonne refusee : aucune section transversale valide sur l'axe");
            return std::nullopt;
        }

        // Assemblage tolérant aux trous (points 1 à 5 de l'audit) : les échecs en
        // tête/queue sont un bord légitime (triés, comportement inchangé) ; un
        // échec ISOLÉ à l'intérieur est comblé par interpolation si le résultat
        // reste géométriquement plausible ; toute autre situation (échecs
        // consécutifs, trou trop large, interpolation invalide) refuse la colonne
        // ENTIÈRE plutôt que de reconnecter silencieusement deux stations
        // lointaines à travers le trou — c'est cette reconnexion silencieuse qui
        // produisait rails discontinus, éventails et morceaux partiels sur les
        // formes concaves. Refuser entièrement (plutôt que de conserver le
        // fragment le plus long) garantit aussi qu'`extend_tip` n'est jamais
        // appliqué à une extrémité créée par une coupure interne : `st.front()` /
        // `st.back()` ne sont jamais atteints avec un trou interne non résolu,
        // seulement de vrais bouts d'axe.
        constexpr double kMaxIsolatedGapFactor = 2.2; // marge sur 2x station_spacing
        raw.reserve(entries.size());
        std::size_t i = 0;
        while (!entries[i].station) {
            ++i; // échecs en tête : bord légitime, jamais un trou interne.
        }
        firstIdx = i;
        lastIdx = i;
        raw.push_back(*entries[i].station);
        ++i;
        while (i < entries.size()) {
            if (entries[i].station) {
                raw.push_back(*entries[i].station);
                lastIdx = i;
                ++i;
                continue;
            }
            std::size_t j = i;
            const CrossSectionFailure firstFailure = entries[i].failure;
            while (j < entries.size() && !entries[j].station) {
                ++j;
            }
            if (j == entries.size()) {
                break; // échecs en queue : bord légitime, pas un trou interne.
            }
            const std::size_t failLen = j - i;
            const Station& prevSt = raw.back();
            const Station& nextSt = *entries[j].station;
            const double gap = norm(nextSt.axis - prevSt.axis);
            bool bridged = false;
            if (failLen == 1 && gap <= kMaxIsolatedGapFactor * stationSpacingUm) {
                const Station candidate = interpolate_station(prevSt, nextSt, axis[i]);
                if (interpolated_station_valid(polys, prevSt, candidate, nextSt, maxWidth)) {
                    raw.push_back(candidate);
                    warnings.push_back("station axe #" + std::to_string(i) +
                                       " interpolee (echec isole : " + describe(firstFailure) +
                                       ")");
                    bridged = true;
                }
            }
            if (!bridged) {
                // Un groupe d'échecs situé tout près d'un bout de jonction n'est pas
                // nécessairement un vrai trou interne. La normale locale peut traverser
                // tout le bourrelet de confluence et dépasser max_width, puis redevenir
                // artificiellement valide au niveau du centre du nœud.
                //
                // Dans ce cas, on arrête la branche à la dernière station valide avant
                // le bourrelet. L'extrémité sera ensuite traitée par l'amputation et
                // l'ancrage global de la jonction.
                const double junctionRadius =
                    static_cast<double>(params.junction_anchor_radius.value);

                const bool nearEndJunction =
                    !extendEnd && norm(axis[i] - axis.back()) <= junctionRadius;

                if (nearEndJunction) {
                    warnings.push_back("queue de jonction amputee avant stations axe #" +
                                       std::to_string(i) + ".." + std::to_string(j - 1) + " (" +
                                       describe(firstFailure) + ")");

                    lastIdx = i - 1;
                    break;
                }

                warnings.push_back("colonne refusee : trou de " +
                                   std::to_string(static_cast<long>(gap)) +
                                   " um entre stations axe #" + std::to_string(i - 1) + " et #" +
                                   std::to_string(j) + " (" + describe(firstFailure) + ")");

                return std::nullopt;
            }
            i = j;
        }
        if (raw.size() < 2) {
            warnings.push_back("colonne refusee : moins de 2 stations valides");
            return std::nullopt;
        }
    } // fin de portée de `entries`/`i`/`j` (assemblage de `raw`)

    // Nettoyage anti-croisement : on retire une station dont le quadrilatère
    // avec la précédente s'auto-intersecte (rails ou barreaux croisés). Sur
    // une courbe légitime (viragé serré), retirer PLUSIEURS stations d'affilée
    // avant que le quadrilatère redevienne valide est un comportement normal,
    // déjà présent avant cet audit — ce n'est PAS en soi le trou signalé par
    // le défaut d'origine. Ce qui compte, c'est la distance d'axe réellement
    // pontée quand une station est enfin regardée à nouveau valide : si elle
    // dépasse `max_station_gap_ratio * station_spacing`, le nettoyage a
    // reconnecté deux stations trop lointaines (seconde source du même défaut,
    // signalée dans l'audit) — refus de la colonne entière plutôt qu'un
    // fragment. Un nettoyage qui ne se résout jamais (toutes les stations
    // restantes croisent) laisse `st` avec moins de 2 éléments, détecté
    // ci-dessous.
    std::vector<Station> st;
    st.push_back(raw.front());
    std::size_t droppedSinceKept = 0;
    for (std::size_t k = 1; k < raw.size(); ++k) {
        const Station& prev = st.back();
        const Station& cur = raw[k];
        const bool crosses = segments_cross(prev.railA, cur.railA, prev.railB, cur.railB) ||
                             segments_cross(prev.railA, prev.railB, cur.railA, cur.railB);
        if (crosses) {
            ++droppedSinceKept;
            continue;
        }
        if (droppedSinceKept > 0) {
            const double gap = norm(cur.axis - prev.axis);
            if (gap > params.max_station_gap_ratio * stationSpacingUm) {
                warnings.push_back(
                    "colonne refusee : trou de " + std::to_string(static_cast<long>(gap)) +
                    " um apres nettoyage anti-croisement (" + std::to_string(droppedSinceKept) +
                    " station(s) ignorees avant station axe #" + std::to_string(k) + ")");
                return std::nullopt;
            }
            warnings.push_back(std::to_string(droppedSinceKept) +
                               " station(s) ignorees (croisement local) avant station axe #" +
                               std::to_string(k));
        }
        st.push_back(cur);
        droppedSinceKept = 0;
    }
    // Retire la zone instable de confluence AVANT les validations génériques.
    // Les dernières sections proches d'une jonction balayent souvent le bourrelet
    // commun à plusieurs branches : leur largeur peut donc dériver fortement sans
    // que la branche elle-même soit invalide. Si on valide avant cette amputation,
    // on refuse la branche sur un faux saut de largeur et la jonction devient
    // artificiellement incomplète.
    if (params.anchor_junction_ends) {
        // Référence calculée UNE FOIS, avant toute amputation, pour que
        // trimmer un bout n'affecte pas la référence utilisée par l'autre.
        const double referenceWidth = representative_station_width(st);
        if (!extendStart) {
            trim_unstable_junction_tail(st, /*atEnd=*/false, referenceWidth);
        }
        if (!extendEnd) {
            trim_unstable_junction_tail(st, /*atEnd=*/true, referenceWidth);
        }
    }

    if (st.size() < 2) {
        warnings.push_back("colonne refusee : moins de 2 stations apres amputation de jonction");
        return std::nullopt;
    }

    // Validation de continuité et de couverture du CŒUR de l'axe (point 6),
    // c'est-à-dire AVANT extension des bouts / ancrage de jonction : ces deux
    // étapes ont leurs propres tolérances dédiées et déjà testées (`extend_tip`
    // impose 5 % de rétrécissement maximum par pas mais introduit
    // délibérément un grand saut de largeur au tout dernier point de fermeture
    // — largeur PLANCHER `tip_min_width` contre la largeur réelle voisine —
    // et `trim_unstable_junction_tail` ampute justement la queue dont la
    // largeur dérive) ; les appliquer après leur passage produirait de faux
    // refus sur des formes parfaitement valides. Ici, sur le cœur seul :
    // aucun grand trou résiduel, aucun saut de largeur incohérent entre
    // stations adjacentes (signature d'une reconnexion à travers un trou
    // masqué en amont), et une couverture suffisante de la longueur d'axe
    // rééchantillonnée (si plusieurs trous isolés se sont accumulés, chacun
    // individuellement sous le seuil de refus ci-dessus, la longueur
    // réellement convertie en stations peut quand même s'effondrer). La
    // référence est la longueur d'axe entre `firstIdx` et `lastIdx` (le cœur
    // effectivement tenté), PAS la longueur totale de l'axe rééchantillonné :
    // des échecs en tête/queue (bord légitime, avant `firstIdx`/après
    // `lastIdx` — p. ex. le bourrelet d'une confluence juste au nœud du
    // squelette) ne doivent pas compter contre la couverture.
    // Après l’amputation d’une queue de jonction, la référence de couverture
    // doit être la portion réellement conservée, et non l’axe complet avant trim.
    double coreAxisLength = 0.0;
    for (std::size_t k = 1; k < st.size(); ++k) {
        coreAxisLength += norm(st[k].axis - st[k - 1].axis);
    }
    double coreCoveredLength = 0.0;
    for (std::size_t k = 1; k < st.size(); ++k) {
        const double gap = norm(st[k].axis - st[k - 1].axis);
        coreCoveredLength += gap;
        if (gap > params.max_station_gap_ratio * stationSpacingUm) {
            warnings.push_back(
                "colonne refusee : trou residuel de " + std::to_string(static_cast<long>(gap)) +
                " um entre stations finales #" + std::to_string(k - 1) + "/" + std::to_string(k));
            return std::nullopt;
        }
        const double w0 = st[k - 1].width;
        const double w1 = st[k].width;
        const double ref = std::max(w0, w1);
        if (ref > 1e-6 && std::abs(w1 - w0) > params.max_adjacent_width_jump_ratio * ref) {
            warnings.push_back(
                "colonne refusee : saut de largeur incoherent entre stations finales #" +
                std::to_string(k - 1) + "/" + std::to_string(k));
            return std::nullopt;
        }
    }
    if (coreAxisLength > 1e-6 &&
        coreCoveredLength / coreAxisLength < params.min_axis_coverage_ratio) {
        warnings.push_back(
            "colonne refusee : couverture d'axe insuffisante (" +
            std::to_string(static_cast<int>(coreCoveredLength / coreAxisLength * 100.0)) + "% < " +
            std::to_string(static_cast<int>(params.min_axis_coverage_ratio * 100.0)) + "%)");
        return std::nullopt;
    }

    // Étend les bouts OUVERTS (sans jonction) jusqu'au bord réel de la région
    // (embout arrondi/pointu) — cf. `extend_tip`. Un bout de JONCTION est
    // traité à l'inverse : sa queue instable est amputée (`trim_unstable_junction_tail`) ;
    // l'ancrage sur une encoche partagée avec la branche voisine se fait
    // séparément, globalement par jonction (`resolve_junction_anchors`), une
    // fois toutes les colonnes construites. Les deux traitements (extension /
    // amputation) sont mutuellement exclusifs par bout (un bout est soit
    // ouvert, soit de jonction). `st` est garanti continu à ce stade (aucune
    // coupure interne n'a survécu aux refus ci-dessus) : `st.front()`/
    // `st.back()` sont donc toujours de véritables bouts d'axe, jamais une
    // extrémité artificielle créée par des stations rejetées (point 5).
    {
        const double stepLen = static_cast<double>(params.station_spacing.value);
        const double tipMin =
            static_cast<double>(std::max<std::int32_t>(1, params.tip_min_width.value));
        if (extendEnd && params.extend_open_ends) {
            const Station& last = st.back();
            auto tail = extend_tip(polys, last.axis, last.tangent, last.tangent, last.width,
                                   maxWidth, stepLen, tipMin);
            for (auto& s : tail) {
                st.push_back(std::move(s));
            }
        }

        if (extendStart && params.extend_open_ends) {
            const Station& first = st.front();
            auto head = extend_tip(polys, first.axis, first.tangent * -1.0, first.tangent,
                                   first.width, maxWidth, stepLen, tipMin);
            std::reverse(head.begin(), head.end());
            st.insert(st.begin(), head.begin(), head.end());
        }
    }

    if (st.size() < 2) {
        warnings.push_back("colonne refusee : moins de 2 stations valides apres extension/ancrage");
        return std::nullopt;
    }

    return st;
}

// Finalisation LEGACY (§ pipeline historique, inchangée) : une station dense
// devient directement un nœud de rail et, sous condition de virage/saut de
// largeur/espacement max, un barreau. Produit des rails à polyligne dense —
// c'est cette représentation que le mode `Parametric` remplace (§ étape 2).
std::optional<SatinColumnGeometry> build_column(const std::vector<Vec2um>& centerline,
                                                const std::vector<Poly>& polys,
                                                const SatinColumnsParameters& params,
                                                bool extendStart, bool extendEnd,
                                                std::vector<std::string>& warnings) {
    auto stationsOpt =
        compute_column_stations(centerline, polys, params, extendStart, extendEnd, warnings);
    if (!stationsOpt) {
        return std::nullopt;
    }
    std::vector<Station>& st = *stationsOpt;

    // Rails.
    std::vector<P2> ra;
    std::vector<P2> rb;
    ra.reserve(st.size());
    rb.reserve(st.size());
    for (const Station& s : st) {
        ra.push_back(s.railA);
        rb.push_back(s.railB);
    }

    SatinColumnGeometry col;
    col.rail_a = rail_path(ra);
    col.rail_b = rail_path(rb);

    // Barreaux : extrémités + virage / variation de largeur / intervalle max.
    const double angleThr = params.rung_angle_threshold_deg * std::acos(-1.0) / 180.0;
    const double maxSpacing = static_cast<double>(params.rung_max_spacing.value);
    std::size_t last = 0;
    double distSince = 0.0;
    double wMean = 0.0, wMin = std::numeric_limits<double>::max(), wMax = 0.0, length = 0.0;
    const auto addRung = [&](std::size_t i) {
        col.rungs.push_back({to_um(st[i].railA), to_um(st[i].railB)});
        last = i;
        distSince = 0.0;
    };
    addRung(0);
    for (std::size_t i = 1; i < st.size(); ++i) {
        distSince += norm(st[i].axis - st[i - 1].axis);
        length += norm(st[i].axis - st[i - 1].axis);
        const bool turn = std::abs(cross(st[last].tangent, st[i].tangent)) > std::sin(angleThr);
        const bool widthJump =
            std::abs(st[i].width - st[last].width) > params.rung_width_ratio * st[last].width;
        if (i + 1 == st.size() || distSince >= maxSpacing || turn || widthJump) {
            addRung(i);
        }
    }
    for (const Station& s : st) {
        wMean += s.width;
        wMin = std::min(wMin, s.width);
        wMax = std::max(wMax, s.width);
    }
    col.mean_width_um = wMean / st.size();
    col.min_width_um = wMin;
    col.max_width_um = wMax;
    col.length_um = length;

    // Validation finale de région/croisement (point 6) : aucun barreau ne
    // sort de la région, et aucune paire de barreaux (voisins ou non) ne se
    // croise — dernier filet contre une géométrie reconnectée à travers un
    // trou qui aurait échappé aux vérifications précédentes (même principe
    // que la validation d'un anneau dans `build_annular_sections`).
    for (const auto& rung : col.rungs) {
        const P2 a{static_cast<double>(rung.a.x.value), static_cast<double>(rung.a.y.value)};
        const P2 b{static_cast<double>(rung.b.x.value), static_cast<double>(rung.b.y.value)};
        if (!in_region(polys, (a + b) * 0.5)) {
            warnings.push_back("colonne refusee : barreau hors region");
            return std::nullopt;
        }
    }
    for (std::size_t ri = 0; ri + 1 < col.rungs.size(); ++ri) {
        const P2 a0{static_cast<double>(col.rungs[ri].a.x.value),
                    static_cast<double>(col.rungs[ri].a.y.value)};
        const P2 b0{static_cast<double>(col.rungs[ri].b.x.value),
                    static_cast<double>(col.rungs[ri].b.y.value)};
        for (std::size_t rj = ri + 1; rj < col.rungs.size(); ++rj) {
            const P2 a1{static_cast<double>(col.rungs[rj].a.x.value),
                        static_cast<double>(col.rungs[rj].a.y.value)};
            const P2 b1{static_cast<double>(col.rungs[rj].b.x.value),
                        static_cast<double>(col.rungs[rj].b.y.value)};
            if (segments_cross(a0, b0, a1, b1)) {
                warnings.push_back("colonne refusee : croisement entre barreaux #" +
                                   std::to_string(ri) + "/" + std::to_string(rj));
                return std::nullopt;
            }
        }
    }

    return col;
}

// =============================================================================
// § objets satin paramétriques (mode `Parametric`) — refonte auto-satin.
//
// La couche d'analyse (`compute_column_stations`) est INCHANGÉE et PARTAGÉE
// avec le mode legacy : ce qui suit ne fait que la CONSOMMER différemment.
// Au lieu de convertir chaque station en nœud de rail (une station = un
// nœud), on sélectionne un petit nombre de stations « structurantes »
// (§ étape 4), on ajuste une courbe de Bézier par rail entre elles
// (§ étape 5, moindres carrés à tangentes fixées + subdivision adaptative
// sur erreur), et les stations structurantes deviennent les
// `SatinControlPair`/`SatinAngleGuide` qui pilotent ensuite
// `stitch_generation::fill_satin_columns` (déjà écrit pour avancer par
// longueur d'arc entre guides, § étape 11 — aucune modification requise
// côté génération de points au-delà d'aplatir les rails, cf. `satin.cpp`).
// =============================================================================

// --- Évaluation Bézier cubique (De Casteljau) et distance point->courbe ---

P2 cubic_eval(P2 p0, P2 p1, P2 p2, P2 p3, double t) {
    const double u = 1.0 - t;
    const double b0 = u * u * u;
    const double b1 = 3.0 * u * u * t;
    const double b2 = 3.0 * u * t * t;
    const double b3 = t * t * t;
    return p0 * b0 + p1 * b1 + p2 * b2 + p3 * b3;
}

// Distance (approx.) d'un point à la courbe cubique, par échantillonnage
// dense en t — suffisant ici (l'appelant ne cherche que l'erreur MAXIMALE
// sur un petit nombre de stations denses, pas une projection exacte).
double cubic_point_distance(P2 p0, P2 p1, P2 p2, P2 p3, P2 pt) {
    double best = std::numeric_limits<double>::max();
    constexpr int kSamples = 24;
    for (int i = 0; i <= kSamples; ++i) {
        const double t = static_cast<double>(i) / kSamples;
        best = std::min(best, norm(cubic_eval(p0, p1, p2, p3, t) - pt));
    }
    return best;
}

// Résultat de l'ajustement d'un segment cubique entre deux stations
// d'ancrage (§ étape 5) : les deux poignées et l'erreur maximale mesurée sur
// les stations denses de l'intervalle.
struct CubicFit {
    P2 p1;  // poignée sortante de l'ancrage de départ
    P2 p2;  // poignée entrante de l'ancrage d'arrivée
    double maxError{0.0};
};

// Ajuste UN segment cubique entre `p0` (tangente sortante `t0`, unitaire) et
// `p3` (tangente sortante `t1`, ATTENTION : orientée vers l'AVANT du rail
// comme `t0`, donc la poignée entrante utilise `-t1`) aux points denses
// `pts` (incluant p0 et p3), par moindres carrés à directions FIXÉES —
// méthode classique (Schneider, « Graphics Gems », single-pass, sans
// reparamétrisation itérative : suffisant ici car les points d'ancrage sont
// déjà choisis pour borner l'erreur, cf. `select_structural_indices`).
// Repli sur des poignées à 1/3 de la corde si le système est dégénéré
// (points quasi confondus, tangentes nulles) ou si la solution donnerait une
// longueur de poignée négative (courbe qui rebrousserait chemin).
CubicFit fit_cubic_segment(const std::vector<P2>& pts, P2 t0, P2 t1) {
    const P2 p0 = pts.front();
    const P2 p3 = pts.back();
    const double chord = norm(p3 - p0);
    const P2 fallbackT0 = norm(t0) > 1e-9 ? t0 : unit(p3 - p0);
    const P2 fallbackT1 = norm(t1) > 1e-9 ? t1 : unit(p3 - p0);
    // Longueur d'échelle des poignées = PROJECTION de la corde sur chaque
    // tangente, jamais la longueur de corde brute. Sur un « coin structurel »
    // (§ étape 5) où la tangente fixée est quasi PERPENDICULAIRE à la corde —
    // p. ex. la station de fermeture d'un bout OUVERT à plat, dont le rail
    // saute presque uniquement en LARGEUR sur une distance d'axe minime —
    // utiliser la corde brute comme longueur de poignée produit une poignée
    // qui pointe très loin dans la direction de la tangente avant de devoir
    // rebrousser vers `p3`, débordant largement hors de la forme. La
    // projection ramène naturellement la poignée à une longueur courte dans
    // ce cas (rendu proche d'un coin, cf. étape 5 : « discontinuité de
    // tangente sur un coin structurel »), sans code spécial pour détecter
    // « est-ce une pointe » : la géométrie seule tranche.
    const double scale0 = std::max(1.0, std::abs(dot(p3 - p0, fallbackT0)));
    const double scale1 = std::max(1.0, std::abs(dot(p3 - p0, fallbackT1)));
    const P2 fallbackP1 = p0 + fallbackT0 * (scale0 / 3.0);
    const P2 fallbackP2 = p3 - fallbackT1 * (scale1 / 3.0);

    if (pts.size() < 3 || chord < 1e-6) {
        CubicFit fit;
        fit.p1 = fallbackP1;
        fit.p2 = fallbackP2;
        for (const P2& p : pts) {
            fit.maxError = std::max(fit.maxError, cubic_point_distance(p0, fit.p1, fit.p2, p3, p));
        }
        return fit;
    }

    // Paramétrisation par longueur de corde cumulée, normalisée sur [0, 1].
    std::vector<double> u(pts.size(), 0.0);
    {
        double acc = 0.0;
        for (std::size_t i = 1; i < pts.size(); ++i) {
            acc += norm(pts[i] - pts[i - 1]);
            u[i] = acc;
        }
        const double total = u.back();
        if (total > 1e-9) {
            for (double& v : u) {
                v /= total;
            }
        }
    }

    // Système normal 2x2 pour les longueurs de poignée (alpha0, alpha1) —
    // cf. commentaire de la fonction.
    double a11 = 0.0, a12 = 0.0, a22 = 0.0, b1 = 0.0, b2 = 0.0;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const double ui = u[i];
        const double uu = 1.0 - ui;
        const double b1c = 3.0 * uu * uu * ui;
        const double b2c = 3.0 * uu * ui * ui;
        const P2 x0 = fallbackT0 * b1c;
        const P2 x1 = fallbackT1 * (-b2c);
        const P2 known = p0 * (uu * uu * uu + b1c) + p3 * (b2c + ui * ui * ui);
        const P2 c = pts[i] - known;
        a11 += dot(x0, x0);
        a12 += dot(x0, x1);
        a22 += dot(x1, x1);
        b1 += dot(x0, c);
        b2 += dot(x1, c);
    }
    const double det = a11 * a22 - a12 * a12;
    double alpha0 = scale0 / 3.0;
    double alpha1 = scale1 / 3.0;
    if (std::abs(det) > 1e-9) {
        const double cand0 = (b1 * a22 - b2 * a12) / det;
        const double cand1 = (a11 * b2 - a12 * b1) / det;
        // Poignée négative ou disproportionnée (> 2x sa longueur d'échelle
        // PROJETÉE, pas la corde brute — cf. `scale0`/`scale1` ci-dessus) :
        // repli — une solution moindres-carrés dégénérée produirait une
        // boucle locale ou un débordement hors forme, exactement ce que
        // l'énoncé interdit (§ étape 5 : « la convergence ne doit pas créer
        // de boucle ni d'inversion »).
        if (cand0 > 1e-6 && cand0 < scale0 * 2.0) {
            alpha0 = cand0;
        }
        if (cand1 > 1e-6 && cand1 < scale1 * 2.0) {
            alpha1 = cand1;
        }
    }

    CubicFit fit;
    fit.p1 = p0 + fallbackT0 * alpha0;
    fit.p2 = p3 - fallbackT1 * alpha1;
    for (const P2& p : pts) {
        fit.maxError = std::max(fit.maxError, cubic_point_distance(p0, fit.p1, fit.p2, p3, p));
    }
    return fit;
}

// Indices REFLEX de la séquence de largeurs (§ étape 4 : « ajouter les
// extrema de largeur ») : un indice i (0 < i < n-1) est retenu si sa largeur
// est un maximum ou minimum local dépassant `minRelChange` par rapport à ses
// deux voisins immédiats.
std::vector<std::size_t> width_extrema_indices(const std::vector<Station>& st,
                                               double minRelChange) {
    std::vector<std::size_t> out;
    for (std::size_t i = 1; i + 1 < st.size(); ++i) {
        const double w0 = st[i - 1].width;
        const double w1 = st[i].width;
        const double w2 = st[i + 1].width;
        const bool isMax = w1 > w0 && w1 > w2;
        const bool isMin = w1 < w0 && w1 < w2;
        if (!isMax && !isMin) {
            continue;
        }
        const double ref = std::max({w0, w1, w2, 1.0});
        if (std::abs(w1 - w0) / ref > minRelChange || std::abs(w1 - w2) / ref > minRelChange) {
            out.push_back(i);
        }
    }
    return out;
}

// Indices de SAUT de largeur entre deux stations ADJACENTES (§ étape 4 :
// « changement important de largeur »), complémentaire de
// `width_extrema_indices` : un saut brusque suivi d'un PALIER (pas un
// pic/creux local) n'est pas un extremum — c'est pourtant exactement la
// signature d'une station de fermeture (`extend_tip`, plancher
// `tip_min_width`) collée à la première vraie station de corps, ou d'une
// queue de jonction juste avant amputation. Sans ce détecteur, un tel saut
// est absorbé dans un grand intervalle et force une poignée Bézier à
// « rattraper » une largeur qui change de 100x sur une seule station,
// produisant un débordement hors forme (cf. commentaire de
// `fit_cubic_segment`). Retient LES DEUX indices de la paire en saut : le
// segment [i-1, i] qui en résulte est court et quasi dégénéré PAR
// CONSTRUCTION, exactement le comportement voulu pour un coin structurel.
std::vector<std::size_t> adjacent_width_jump_indices(const std::vector<Station>& st,
                                                      double minRelChange) {
    std::vector<std::size_t> out;
    for (std::size_t i = 1; i < st.size(); ++i) {
        const double w0 = st[i - 1].width;
        const double w1 = st[i].width;
        const double ref = std::max({w0, w1, 1.0});
        if (std::abs(w1 - w0) / ref > minRelChange) {
            out.push_back(i - 1);
            out.push_back(i);
        }
    }
    return out;
}

// Indices de courbure maximale locale (§ étape 4) : angle entre tangentes
// consécutives, maximum local dépassant `minAngleRad`.
std::vector<std::size_t> curvature_maxima_indices(const std::vector<Station>& st,
                                                   double minAngleRad) {
    std::vector<std::size_t> out;
    if (st.size() < 3) {
        return out;
    }
    std::vector<double> turn(st.size(), 0.0);
    for (std::size_t i = 1; i + 1 < st.size(); ++i) {
        const double c = std::clamp(dot(st[i - 1].tangent, st[i + 1].tangent), -1.0, 1.0);
        turn[i] = std::acos(c);
    }
    for (std::size_t i = 1; i + 1 < st.size(); ++i) {
        if (turn[i] < minAngleRad) {
            continue;
        }
        if (turn[i] >= turn[i - 1] && turn[i] >= turn[i + 1]) {
            out.push_back(i);
        }
    }
    return out;
}

// Indices retenus par Douglas-Peucker sur `pts`, en RÉUTILISANT
// `geometry::simplify` (§ étape 1 : « réutilise les primitives existantes »)
// plutôt que de réécrire l'algorithme : on construit un `Path` ouvert à
// partir de `pts`, on simplifie, puis on retrouve les indices d'origine par
// correspondance exacte de position (simplify ne DÉPLACE jamais un nœud
// conservé, il ne fait qu'en retirer).
std::vector<std::size_t> douglas_peucker_indices(const std::vector<P2>& pts,
                                                 Micrometers tolerance) {
    std::vector<std::size_t> out;
    if (pts.size() < 3) {
        for (std::size_t i = 0; i < pts.size(); ++i) {
            out.push_back(i);
        }
        return out;
    }
    geometry::Path path;
    path.closed = false;
    path.nodes.reserve(pts.size());
    for (const P2& p : pts) {
        path.nodes.push_back({to_um(p), geometry::NodeType::Corner, std::nullopt, std::nullopt});
    }
    const geometry::Path simplified = geometry::simplify(path, tolerance);
    std::size_t searchFrom = 0;
    for (const auto& node : simplified.nodes) {
        for (std::size_t i = searchFrom; i < pts.size(); ++i) {
            if (path.nodes[i].pos == node.pos) {
                out.push_back(i);
                searchFrom = i + 1;
                break;
            }
        }
    }
    if (out.empty() || out.front() != 0) {
        out.insert(out.begin(), 0);
    }
    if (out.back() != pts.size() - 1) {
        out.push_back(pts.size() - 1);
    }
    return out;
}

// Sélectionne les stations STRUCTURANTES (§ étape 4) : union des extrémités,
// extrema de largeur, maxima de courbure, et simplification Douglas-Peucker
// des DEUX rails (chacun vote ses propres indices importants). Le résultat
// est ensuite raffiné pour respecter `minimum_control_pair_spacing` (fusion
// des indices trop proches) — `maximum_control_pair_spacing` et l'erreur de
// reconstruction sont appliqués séparément par `fit_rail` (§ subdivision
// adaptative), qui peut réinsérer des indices ici absents.
std::vector<std::size_t> select_structural_indices(const std::vector<Station>& st,
                                                    const SatinColumnsParameters& params) {
    std::set<std::size_t> chosen;
    chosen.insert(0);
    chosen.insert(st.size() - 1);

    for (std::size_t i : width_extrema_indices(st, 0.15)) {
        chosen.insert(i);
    }
    for (std::size_t i : adjacent_width_jump_indices(st, 0.25)) {
        chosen.insert(i);
    }
    constexpr double kCurvatureThresholdRad = 12.0 * std::numbers::pi / 180.0;
    for (std::size_t i : curvature_maxima_indices(st, kCurvatureThresholdRad)) {
        chosen.insert(i);
    }
    std::vector<P2> railA, railB;
    railA.reserve(st.size());
    railB.reserve(st.size());
    for (const Station& s : st) {
        railA.push_back(s.railA);
        railB.push_back(s.railB);
    }
    for (std::size_t i : douglas_peucker_indices(railA, params.bezier_fit_tolerance)) {
        chosen.insert(i);
    }
    for (std::size_t i : douglas_peucker_indices(railB, params.bezier_fit_tolerance)) {
        chosen.insert(i);
    }

    // Fusionne les indices trop rapprochés (§ minimum_control_pair_spacing) :
    // parcours croissant, un indice n'est gardé que si son abscisse d'axe
    // dépasse le précédent gardé de la marge minimale (les deux extrémités
    // sont toujours gardées, quelle que soit leur proximité — un objet trop
    // court pour respecter l'espacement minimal reste représenté par ses
    // deux seules extrémités).
    std::vector<std::size_t> sorted(chosen.begin(), chosen.end());
    std::vector<double> axisArc(st.size(), 0.0);
    for (std::size_t i = 1; i < st.size(); ++i) {
        axisArc[i] = axisArc[i - 1] + norm(st[i].axis - st[i - 1].axis);
    }
    const double minSpacing = static_cast<double>(params.minimum_control_pair_spacing.value);
    std::vector<std::size_t> merged;
    merged.push_back(sorted.front());
    for (std::size_t k = 1; k + 1 < sorted.size(); ++k) {
        if (axisArc[sorted[k]] - axisArc[merged.back()] >= minSpacing) {
            merged.push_back(sorted[k]);
        }
    }
    if (sorted.size() > 1) {
        merged.push_back(sorted.back());
    }
    return merged;
}

// Ajuste conjointement les DEUX rails entre `fromIdx`/`toIdx` (§ étape 6 :
// une même paire structurante doit correspondre EXACTEMENT à une paire de
// points sur les deux rails — donc un seul ensemble d'ancrages, partagé,
// jamais deux subdivisions indépendantes qui dériveraient l'une de l'autre).
// Subdivise ADAPTATIVEMENT tant que L'UN OU L'AUTRE rail dépasse
// `bezier_fit_tolerance` (distance), `maximum_tangent_error_deg` (tangente,
// mesurée à l'ancrage retenu contre la tangente réelle de station), ou que
// l'intervalle dépasse `maximum_control_pair_spacing` — bornée par
// `minimum_control_pair_spacing` et par une profondeur de récursion
// (garantit la terminaison, § étape 5).
struct JointRailSegment {
    std::size_t fromIdx{0};
    std::size_t toIdx{0};
    P2 aP1, aP2;  // poignées rail A
    P2 bP1, bP2;  // poignées rail B
    double maxError{0.0};
};

void fit_both_rails_recursive(const std::vector<Station>& st, std::size_t fromIdx,
                              std::size_t toIdx, const SatinColumnsParameters& params, int depth,
                              std::vector<JointRailSegment>& out) {
    std::vector<P2> ptsA, ptsB;
    ptsA.reserve(toIdx - fromIdx + 1);
    ptsB.reserve(toIdx - fromIdx + 1);
    for (std::size_t i = fromIdx; i <= toIdx; ++i) {
        ptsA.push_back(st[i].railA);
        ptsB.push_back(st[i].railB);
    }
    const CubicFit fitA = fit_cubic_segment(ptsA, st[fromIdx].tangent, st[toIdx].tangent);
    const CubicFit fitB = fit_cubic_segment(ptsB, st[fromIdx].tangent, st[toIdx].tangent);

    const double tolerance = static_cast<double>(params.bezier_fit_tolerance.value);
    const double maxSpacingUm = static_cast<double>(params.maximum_control_pair_spacing.value);
    const double minSpacingUm = static_cast<double>(params.minimum_control_pair_spacing.value);
    const double tangentTolRad = params.maximum_tangent_error_deg * std::numbers::pi / 180.0;
    double axisSpan = 0.0;
    for (std::size_t i = fromIdx + 1; i <= toIdx; ++i) {
        axisSpan += norm(st[i].axis - st[i - 1].axis);
    }
    // Erreur de tangente (§ étape 4) : angle entre la tangente d'AXE réelle
    // à la station médiane et la tangente de la corde reliant les deux
    // ancrages -- un proxy simple de « la courbe garde la bonne direction »,
    // peu coûteux (pas de dérivée analytique de Bézier nécessaire ici).
    double tangentError = 0.0;
    {
        const std::size_t midIdx = fromIdx + (toIdx - fromIdx) / 2;
        const P2 chord = unit(st[toIdx].axis - st[fromIdx].axis);
        if (norm(chord) > 1e-9) {
            const double c = std::clamp(dot(chord, st[midIdx].tangent), -1.0, 1.0);
            tangentError = std::acos(c);
        }
    }

    constexpr int kMaxDepth = 10;
    const bool tooLong = axisSpan > maxSpacingUm;
    const bool tooImprecise =
        fitA.maxError > tolerance || fitB.maxError > tolerance || tangentError > tangentTolRad;
    if ((tooLong || tooImprecise) && depth < kMaxDepth && toIdx > fromIdx + 1) {
        // Point de coupure partagé : la station la plus déviante (sur l'un
        // ou l'autre rail), ou le milieu par longueur d'axe (dépassement
        // d'espacement pur, courbes déjà fidèles).
        std::size_t splitIdx = fromIdx;
        if (tooImprecise) {
            double worst = -1.0;
            for (std::size_t i = fromIdx + 1; i < toIdx; ++i) {
                const double dA =
                    cubic_point_distance(ptsA.front(), fitA.p1, fitA.p2, ptsA.back(), st[i].railA);
                const double dB =
                    cubic_point_distance(ptsB.front(), fitB.p1, fitB.p2, ptsB.back(), st[i].railB);
                const double d = std::max(dA, dB);
                if (d > worst) {
                    worst = d;
                    splitIdx = i;
                }
            }
        }
        if (!tooImprecise || splitIdx == fromIdx) {
            double target = axisSpan * 0.5;
            double acc = 0.0;
            splitIdx = toIdx - 1;
            for (std::size_t i = fromIdx + 1; i <= toIdx; ++i) {
                acc += norm(st[i].axis - st[i - 1].axis);
                if (acc >= target) {
                    splitIdx = i;
                    break;
                }
            }
        }
        splitIdx = std::clamp(splitIdx, fromIdx + 1, toIdx - 1);
        double leftLen = 0.0;
        for (std::size_t i = fromIdx + 1; i <= splitIdx; ++i) {
            leftLen += norm(st[i].axis - st[i - 1].axis);
        }
        double rightLen = axisSpan - leftLen;
        // Le point de coupure « idéal » (pire erreur, ou milieu) peut violer
        // `minimum_control_pair_spacing` -- typiquement un SAUT abrupt collé
        // au tout premier/dernier point (station de fermeture d'un bout
        // ouvert, cf. `adjacent_width_jump_indices`). Plutôt que d'abandonner
        // toute subdivision (et garder un unique segment très imprécis, donc
        // potentiellement débordant), on cherche le point valide le PLUS
        // PROCHE de la coupure idéale qui respecte l'espacement des deux
        // côtés -- seul un intervalle réellement trop court pour contenir
        // DEUX espacements minimaux reste non subdivisé.
        if (!(leftLen >= minSpacingUm && rightLen >= minSpacingUm)) {
            std::size_t bestIdx = 0;
            double bestDelta = std::numeric_limits<double>::max();
            double accLeft = 0.0;
            for (std::size_t i = fromIdx + 1; i < toIdx; ++i) {
                accLeft += norm(st[i].axis - st[i - 1].axis);
                const double accRight = axisSpan - accLeft;
                if (accLeft >= minSpacingUm && accRight >= minSpacingUm) {
                    const double delta = std::abs(static_cast<double>(i) -
                                                  static_cast<double>(splitIdx));
                    if (delta < bestDelta) {
                        bestDelta = delta;
                        bestIdx = i;
                        leftLen = accLeft;
                        rightLen = accRight;
                    }
                }
            }
            if (bestDelta < std::numeric_limits<double>::max()) {
                splitIdx = bestIdx;
            } else {
                leftLen = rightLen = -1.0;  // aucune coupure valide : intervalle trop court.
            }
        }
        if (leftLen >= minSpacingUm && rightLen >= minSpacingUm) {
            fit_both_rails_recursive(st, fromIdx, splitIdx, params, depth + 1, out);
            fit_both_rails_recursive(st, splitIdx, toIdx, params, depth + 1, out);
            return;
        }
    }
    JointRailSegment seg;
    seg.fromIdx = fromIdx;
    seg.toIdx = toIdx;
    seg.aP1 = fitA.p1;
    seg.aP2 = fitA.p2;
    seg.bP1 = fitB.p1;
    seg.bP2 = fitB.p2;
    seg.maxError = std::max(fitA.maxError, fitB.maxError);
    out.push_back(seg);
}

// Résultat de l'ajustement conjoint des deux rails : la séquence d'indices
// d'ancrage PARTAGÉE effectivement retenue après subdivision adaptative
// (peut contenir plus d'indices que `select_structural_indices` initial),
// et les poignées Bézier de chaque segment, pour chaque rail.
struct RailFit {
    std::vector<std::size_t> anchors;  // indices dans `st`, triés, PARTAGÉS par les deux rails
    std::vector<P2> aTanOut, aTanIn;   // poignées rail A (taille anchors.size()-1 chacune)
    std::vector<P2> bTanOut, bTanIn;   // poignées rail B
    double maxError{0.0};
};

RailFit fit_both_rails(const std::vector<Station>& st, const std::vector<std::size_t>& initialAnchors,
                       const SatinColumnsParameters& params) {
    std::vector<JointRailSegment> segments;
    for (std::size_t k = 0; k + 1 < initialAnchors.size(); ++k) {
        fit_both_rails_recursive(st, initialAnchors[k], initialAnchors[k + 1], params, 0, segments);
    }
    std::sort(segments.begin(), segments.end(), [](const JointRailSegment& a,
                                                    const JointRailSegment& b) {
        return a.fromIdx < b.fromIdx;
    });

    RailFit fit;
    fit.anchors.push_back(segments.front().fromIdx);
    for (const auto& seg : segments) {
        fit.anchors.push_back(seg.toIdx);
        fit.aTanOut.push_back(seg.aP1);
        fit.aTanIn.push_back(seg.aP2);
        fit.bTanOut.push_back(seg.bP1);
        fit.bTanIn.push_back(seg.bP2);
        fit.maxError = std::max(fit.maxError, seg.maxError);
    }
    return fit;
}

// Construit un `geometry::Path` Bézier épars depuis un `RailFit` : un
// `PathNode` par ancrage PARTAGÉ, `tan_out`/`tan_in` = poignée moins position
// (convention `PathNode`, offset relatif). Le premier ancrage n'a pas de
// `tan_in`, le dernier pas de `tan_out` (bout de l'objet). `pickA` sélectionne
// les poignées du rail A ou B — mêmes ancrages pour les deux (§ étape 6).
geometry::Path rail_bezier_path(const std::vector<Station>& st, bool pickA, const RailFit& fit) {
    geometry::Path path;
    path.closed = false;
    const auto point = [&](std::size_t i) { return pickA ? st[i].railA : st[i].railB; };
    const auto& tanOut = pickA ? fit.aTanOut : fit.bTanOut;
    const auto& tanIn = pickA ? fit.aTanIn : fit.bTanIn;
    path.nodes.reserve(fit.anchors.size());
    for (std::size_t k = 0; k < fit.anchors.size(); ++k) {
        geometry::PathNode node;
        node.pos = to_um(point(fit.anchors[k]));
        node.type = geometry::NodeType::Smooth;
        if (k > 0) {
            node.tan_in = to_um(tanIn[k - 1]) - node.pos;
        }
        if (k + 1 < fit.anchors.size()) {
            node.tan_out = to_um(tanOut[k]) - node.pos;
        }
        path.nodes.push_back(node);
    }
    return path;
}

// Construit les `SatinControlPair`/`SatinAngleGuide`/`SatinRung` depuis un
// `RailFit` conjoint : une paire par ancrage partagé (§ étape 4 et 7 — les
// deux notions coïncident tant qu'aucune édition manuelle ne les dissocie).
// Les tangentes exposées sont celles de la STATION dense correspondante
// (déjà unitaires, orientées le long de l'axe), pas les poignées Bézier
// (qui peuvent être asymétriques entre segments adjacents).
void build_control_pairs_and_guides(const std::vector<Station>& st, const RailFit& fit,
                                    ParametricSatinObject& obj) {
    obj.control_pairs.reserve(fit.anchors.size());
    obj.angle_guides.reserve(fit.anchors.size());
    obj.rungs.reserve(fit.anchors.size());
    double arcA = 0.0, arcB = 0.0;
    for (std::size_t k = 0; k < fit.anchors.size(); ++k) {
        const std::size_t idx = fit.anchors[k];
        if (k > 0) {
            arcA += norm(st[idx].railA - st[fit.anchors[k - 1]].railA);
            arcB += norm(st[idx].railB - st[fit.anchors[k - 1]].railB);
        }
        SatinControlPair pair;
        pair.axis_point = to_um(st[idx].axis);
        pair.rail_a_point = to_um(st[idx].railA);
        pair.rail_b_point = to_um(st[idx].railB);
        pair.tangent = to_um(st[idx].axis + st[idx].tangent * 1000.0) - pair.axis_point;
        pair.width_um = st[idx].width;
        pair.structural = true;
        obj.control_pairs.push_back(pair);

        SatinAngleGuide guide;
        guide.rail_a_point = pair.rail_a_point;
        guide.rail_b_point = pair.rail_b_point;
        guide.rail_a_arc_um = arcA;
        guide.rail_b_arc_um = arcB;
        guide.structural = true;
        obj.angle_guides.push_back(guide);

        obj.rungs.push_back({pair.rail_a_point, pair.rail_b_point});
    }
}

// --- § jonctions paramétriques (étape 9) : recouvrement local, sans ancre --
//
// PAS d'ancre centrale commune, pas de sommet de bissectrice, pas de
// déplacement du dernier nœud, pas de séparateur/secteur/noyau résiduel
// (§ interdictions explicites). Chaque branche reste un objet INDÉPENDANT ;
// à un bout de jonction, on la prolonge simplement un peu plus loin qu'elle
// ne l'aurait été si ce bout était refusé/tronqué — en ré-échantillonnant
// quelques sections transversales RÉELLES (mêmes primitives que
// `extend_tip`), jusqu'à une distance bornée. Contrairement à `extend_tip`
// (bout ouvert, s'arrête quand la largeur cesse de rétrécir), on avance ici
// jusqu'à une DISTANCE cible : la largeur peut légitimement croître en
// entrant dans le bourrelet de confluence, ce n'est pas une erreur, c'est le
// recouvrement voulu.
std::vector<Station> extend_into_confluence(const std::vector<Poly>& polys, P2 base, P2 marchDir,
                                            P2 orientTan, double maxWidth, double stepLen,
                                            double overlapUm) {
    std::vector<Station> ext;
    if (stepLen <= 0.0 || overlapUm <= 0.0) {
        return ext;
    }
    const P2 nrm{-orientTan.y, orientTan.x};
    double advanced = 0.0;
    const int maxSteps = std::max(1, static_cast<int>(std::ceil(overlapUm / stepLen)) + 1);
    for (int k = 1; k <= maxSteps && advanced < overlapUm; ++k) {
        const double s = std::min(static_cast<double>(k) * stepLen, overlapUm);
        const P2 p = base + marchDir * s;
        if (!in_region(polys, p)) {
            break;
        }
        const auto sec = cross_section(polys, p, nrm, maxWidth);
        if (!sec) {
            break;
        }
        Station st;
        st.axis = p;
        st.tangent = orientTan;
        st.railA = p + nrm * sec->second;
        st.railB = p + nrm * sec->first;
        st.width = sec->second - sec->first;
        ext.push_back(st);
        advanced = s;
    }
    return ext;
}

// Recouvrement RÉELLEMENT applicable à un bout de jonction : fraction de la
// largeur locale (`junction_overlap_ratio`), bornée par un plancher/plafond
// absolu (§ étape 9 : « jamais prolonger un objet très loin dans une branche
// voisine »).
double junction_overlap_target(double referenceWidth, const SatinColumnsParameters& params) {
    const double raw = referenceWidth * params.junction_overlap_ratio;
    return std::clamp(raw, static_cast<double>(params.junction_overlap_min.value),
                      static_cast<double>(params.junction_overlap_max.value));
}

// Finalisation PARAMÉTRIQUE (§ refonte auto-satin) : sélectionne les paires
// structurantes, ajuste les deux rails en Bézier épars, construit les
// guides/barreaux, et prolonge chaque bout de JONCTION d'un recouvrement
// local borné (jamais un bout OUVERT — celui-ci est déjà étendu jusqu'au
// bord réel par `compute_column_stations`/`extend_tip`, en amont, exactement
// comme en mode legacy).
std::optional<ParametricSatinObject> build_parametric_object(
    const std::vector<Vec2um>& centerline, const std::vector<Poly>& polys,
    const SatinColumnsParameters& params, bool extendStart, bool extendEnd,
    std::vector<std::string>& warnings) {
    auto stationsOpt =
        compute_column_stations(centerline, polys, params, extendStart, extendEnd, warnings);
    if (!stationsOpt) {
        return std::nullopt;
    }
    std::vector<Station> st = std::move(*stationsOpt);

    const double maxWidth = static_cast<double>(params.analysis.thresholds.max_satin_width.value);
    const double stepLen = static_cast<double>(params.station_spacing.value);
    const double referenceWidth = representative_station_width(st);

    ParametricSatinObject obj;
    obj.raw_station_count = st.size();

    // Recouvrement de jonction (§ étape 9) : appliqué APRÈS
    // `compute_column_stations` (qui a déjà amputé la queue instable côté
    // jonction via `trim_unstable_junction_tail`), en repartant du dernier
    // point stable vers l'intérieur de la confluence -- jamais vers un point
    // partagé avec une autre branche.
    if (!extendEnd && params.anchor_junction_ends && st.size() >= 2) {
        const double overlapUm = junction_overlap_target(referenceWidth, params);
        const Station& last = st.back();
        auto tail =
            extend_into_confluence(polys, last.axis, last.tangent, last.tangent, maxWidth, stepLen,
                                   overlapUm);
        if (!tail.empty()) {
            obj.end_overlap_um = norm(tail.back().axis - last.axis);
            for (auto& s : tail) {
                st.push_back(std::move(s));
            }
        }
    }
    if (!extendStart && params.anchor_junction_ends && st.size() >= 2) {
        const double overlapUm = junction_overlap_target(referenceWidth, params);
        const Station& first = st.front();
        auto head = extend_into_confluence(polys, first.axis, first.tangent * -1.0, first.tangent,
                                           maxWidth, stepLen, overlapUm);
        if (!head.empty()) {
            obj.start_overlap_um = norm(head.back().axis - first.axis);
            std::reverse(head.begin(), head.end());
            st.insert(st.begin(), head.begin(), head.end());
        }
    }

    const auto initialAnchors = select_structural_indices(st, params);
    const RailFit fit = fit_both_rails(st, initialAnchors, params);
    obj.max_fit_error_um = fit.maxError;

    obj.rail_a = rail_bezier_path(st, true, fit);
    obj.rail_b = rail_bezier_path(st, false, fit);
    build_control_pairs_and_guides(st, fit, obj);

    // Marque les paires terminales (§ étape 4 : `junction_pair`/`open_tip_pair`).
    if (!obj.control_pairs.empty()) {
        if (!extendStart) {
            obj.control_pairs.front().junction_pair = true;
        } else {
            obj.control_pairs.front().open_tip_pair = true;
        }
        if (!extendEnd) {
            obj.control_pairs.back().junction_pair = true;
        } else {
            obj.control_pairs.back().open_tip_pair = true;
        }
    }

    double wMean = 0.0, wMin = std::numeric_limits<double>::max(), wMax = 0.0, length = 0.0;
    for (std::size_t i = 0; i < st.size(); ++i) {
        wMean += st[i].width;
        wMin = std::min(wMin, st[i].width);
        wMax = std::max(wMax, st[i].width);
        if (i > 0) {
            length += norm(st[i].axis - st[i - 1].axis);
        }
    }
    obj.mean_width_um = wMean / static_cast<double>(st.size());
    obj.min_width_um = wMin;
    obj.max_width_um = wMax;
    obj.length_um = length;

    return obj;
}

// Validation géométrique d'un `ParametricSatinObject` (§ étape 12), par
// échantillonnage dense des rails Bézier APLATIS (`geometry::flatten`) --
// jamais sur les seuls nœuds de contrôle, qui ne disent rien de la courbe
// réelle entre eux. Renvoie un message d'échec explicite, ou `nullopt` si
// l'objet est valide.
std::optional<std::string> validate_parametric_object(const ParametricSatinObject& obj,
                                                       const std::vector<Poly>& polys,
                                                       const SatinColumnsParameters& params) {
    if (obj.rail_a.nodes.size() < 2 || obj.rail_b.nodes.size() < 2) {
        return "rail trop court";
    }
    const Micrometers flattenTol{50};
    const auto flatA = geometry::flatten(obj.rail_a, flattenTol);
    const auto flatB = geometry::flatten(obj.rail_b, flattenTol);
    if (flatA.points.size() < 2 || flatB.points.size() < 2) {
        return "rail aplati degenere";
    }
    const auto toP2 = [](Vec2um v) {
        return P2{static_cast<double>(v.x.value), static_cast<double>(v.y.value)};
    };
    Poly polyA, polyB;
    polyA.reserve(flatA.points.size());
    polyB.reserve(flatB.points.size());
    for (const auto& p : flatA.points) polyA.push_back(toP2(p));
    for (const auto& p : flatB.points) polyB.push_back(toP2(p));

    const double maxWidthLimit =
        static_cast<double>(params.analysis.thresholds.max_satin_width.value) * 1.5;
    // Un rail satin trace le CONTOUR par construction (`cross_section`
    // intersecte exactement le bord) : un point de rail est donc légitimement
    // ON THE BOUNDARY, pas strictement à l'intérieur. `in_region` (test de
    // parité par lancer de rayon) est ambigu pile sur une arête — l'arrondi
    // au µm d'un point déjà quasi-tangent peut le faire retomber du mauvais
    // côté sans qu'aucune géométrie ne soit réellement en cause. `fit_cubic_segment`
    // ne mesure l'erreur qu'aux stations DENSES (échantillons discrets) :
    // entre deux stations, la courbe continue peut légèrement déborder sans
    // qu'aucun échantillon ne l'ait détecté. La tolérance de validation doit
    // donc rester cohérente avec la tolérance de FIT elle-même
    // (`bezier_fit_tolerance`, déjà le budget d'imprécision explicitement
    // accepté) plutôt qu'une constante arbitraire indépendante — un facteur
    // 1,5 absorbe cet écart résiduel entre stations sans jamais masquer un
    // VRAI débordement (poignée Bézier qui bombe hors la forme), qui reste
    // d'un tout autre ordre de grandeur (plusieurs dixièmes de mm au moins).
    const double boundaryToleranceUm =
        std::max(25.0, static_cast<double>(params.bezier_fit_tolerance.value) * 1.5);
    const auto onBoundaryOrInside = [&](P2 p) {
        return in_region(polys, p) || distance_to_polys(polys, p) <= boundaryToleranceUm;
    };
    for (const P2& p : polyA) {
        if (!onBoundaryOrInside(p)) {
            return "rail A hors region";
        }
    }
    for (const P2& p : polyB) {
        if (!onBoundaryOrInside(p)) {
            return "rail B hors region";
        }
    }
    for (std::size_t i = 0; i + 1 < polyA.size(); ++i) {
        for (std::size_t j = i + 2; j + 1 < polyA.size(); ++j) {
            if (segments_cross(polyA[i], polyA[i + 1], polyA[j], polyA[j + 1])) {
                return "rail A auto-croise";
            }
        }
    }
    for (std::size_t i = 0; i + 1 < polyB.size(); ++i) {
        for (std::size_t j = i + 2; j + 1 < polyB.size(); ++j) {
            if (segments_cross(polyB[i], polyB[i + 1], polyB[j], polyB[j + 1])) {
                return "rail B auto-croise";
            }
        }
    }
    for (std::size_t i = 0; i + 1 < polyA.size(); ++i) {
        for (std::size_t j = 0; j + 1 < polyB.size(); ++j) {
            if (segments_cross(polyA[i], polyA[i + 1], polyB[j], polyB[j + 1])) {
                return "rails A/B se croisent";
            }
        }
    }
    for (const auto& guide : obj.angle_guides) {
        const P2 a = toP2(guide.rail_a_point);
        const P2 b = toP2(guide.rail_b_point);
        const double width = norm(a - b);
        if (!(width > 0.0)) {
            return "ligne d'angle degeneree (largeur nulle)";
        }
        if (width > maxWidthLimit) {
            return "ligne d'angle trop large";
        }
        if (!in_region(polys, (a + b) * 0.5)) {
            return "ligne d'angle hors region";
        }
    }
    for (std::size_t i = 0; i + 1 < obj.angle_guides.size(); ++i) {
        const auto& g0 = obj.angle_guides[i];
        const auto& g1 = obj.angle_guides[i + 1];
        if (g1.rail_a_arc_um <= g0.rail_a_arc_um || g1.rail_b_arc_um <= g0.rail_b_arc_um) {
            return "correspondance non monotone entre lignes d'angle";
        }
    }
    for (std::size_t i = 0; i + 1 < obj.angle_guides.size(); ++i) {
        if (segments_cross(toP2(obj.angle_guides[i].rail_a_point),
                           toP2(obj.angle_guides[i].rail_b_point),
                           toP2(obj.angle_guides[i + 1].rail_a_point),
                           toP2(obj.angle_guides[i + 1].rail_b_point))) {
            return "lignes d'angle croisees";
        }
    }
    return std::nullopt;
}

// --- Résolution globale des ancres de jonction (§ audit jonctions
// branchées/concaves) -------------------------------------------------------
//
// Défaut trouvé par revue : chaque bout de jonction était ancré
// indépendamment (`trim_and_anchor_junction_end`, désormais scindée en
// `trim_unstable_junction_tail`), chaque rail cherchant pour son propre
// compte le sommet reflex le plus proche. Sur une confluence asymétrique à
// 3+ branches, deux branches angulairement ADJACENTES pouvaient donc élire
// des sommets différents pour ce qui est géométriquement la MÊME encoche —
// d'où des rails terminaux qui ne coïncident pas exactement, des barreaux
// terminaux en éventail, et une zone triangulaire mal couverte entre les
// deux branches. Ce qui suit remplace la résolution indépendante par une
// résolution GLOBALE par jonction : les branches incidentes sont triées par
// angle autour du centre de la confluence, et chaque ESPACE ANGULAIRE entre
// deux branches adjacentes reçoit au plus UNE ancre (le sommet reflex le
// plus proche du centre dans ce secteur), appliquée identiquement aux deux
// rails qui se font face.

P2 rail_point(const geometry::Path& rail, std::size_t i) {
    return {static_cast<double>(rail.nodes[i].pos.x.value),
            static_cast<double>(rail.nodes[i].pos.y.value)};
}

P2 terminal_point(const geometry::Path& rail, bool atEnd) {
    return atEnd ? rail_point(rail, rail.nodes.size() - 1) : rail_point(rail, 0);
}

// Point de rail à `offset` stations en retrait du bout `atEnd` (offset=0 =
// le tout dernier nœud, comme `terminal_point` ; offset croissant = on
// s'éloigne encore plus de la jonction). Saturé au premier/dernier nœud
// disponible.
P2 station_point(const geometry::Path& rail, bool atEnd, std::size_t offset) {
    const std::size_t n = rail.nodes.size();
    if (n == 0) {
        return {0.0, 0.0};
    }
    const std::size_t capped = std::min(offset, n - 1);
    return atEnd ? rail_point(rail, n - 1 - capped) : rail_point(rail, capped);
}

// Direction SORTANTE de la jonction le long de la branche (du centre vers
// l'intérieur de la colonne), dérivée des deux stations terminales du rail —
// ni `axis` ni `Station` ne survivent au-delà de `build_column`, seuls les
// rails/barreaux du résultat final sont disponibles ici.
P2 outward_tangent(const geometry::Path& railA, const geometry::Path& railB, bool atEnd) {
    const std::size_t n = railA.nodes.size();
    if (n < 2) {
        return {0.0, 0.0};
    }
    const std::size_t termIdx = atEnd ? n - 1 : 0;
    const std::size_t nextIdx = atEnd ? n - 2 : 1;
    const P2 termMid = (rail_point(railA, termIdx) + rail_point(railB, termIdx)) * 0.5;
    const P2 nextMid = (rail_point(railA, nextIdx) + rail_point(railB, nextIdx)) * 0.5;
    return unit(nextMid - termMid);
}

// Variante de `outward_tangent` à `offset` stations en retrait (voir
// `station_point`) : nécessaire pour ré-évaluer la tangente d'une
// `StableBranchEnd` reculée par `resolve_junction`.
P2 outward_tangent_at(const geometry::Path& railA, const geometry::Path& railB, bool atEnd,
                      std::size_t offset) {
    const std::size_t n = railA.nodes.size();
    if (n < 2) {
        return {0.0, 0.0};
    }
    const std::size_t maxOffset = n - 2;
    const std::size_t o = std::min(offset, maxOffset);
    const std::size_t termIdx = atEnd ? n - 1 - o : o;
    const std::size_t nextIdx = atEnd ? termIdx - 1 : termIdx + 1;
    const P2 termMid = (rail_point(railA, termIdx) + rail_point(railB, termIdx)) * 0.5;
    const P2 nextMid = (rail_point(railA, nextIdx) + rail_point(railB, nextIdx)) * 0.5;
    return unit(nextMid - termMid);
}

// Une branche incidente à une jonction, vue depuis cette jonction : quelle
// colonne, quel bout (`atEnd`), et sous quel angle elle en repart.
struct JunctionBranch {
    std::size_t columnIndex{0};
    bool atEnd{false}; // true = end_junction (arrière du rail) ; false = start_junction (avant)
    double angle{0.0};
};

// Regroupe toutes les branches par jonction (`graph.nodes[id]`), triées par
// angle croissant autour du centre — ordre exploité tel quel par
// `resolve_junction_anchors` et `validate_junctions` pour parcourir les
// espaces angulaires entre branches adjacentes. Template sur le type de
// colonne : `SatinColumnGeometry` (legacy) et `ParametricSatinObject`
// partagent les champs `rail_a`/`rail_b`/`start_junction`/`end_junction`
// exploités ici.
template <typename ColumnT>
std::map<std::uint32_t, std::vector<JunctionBranch>>
collect_junction_branches(const std::vector<ColumnT>& columns) {
    std::map<std::uint32_t, std::vector<JunctionBranch>> byJunction;
    for (std::size_t ci = 0; ci < columns.size(); ++ci) {
        const auto& col = columns[ci];
        if (col.rail_a.nodes.size() < 2 || col.rail_b.nodes.size() != col.rail_a.nodes.size()) {
            continue;
        }
        if (col.start_junction) {
            const P2 outward = outward_tangent(col.rail_a, col.rail_b, false);
            byJunction[*col.start_junction].push_back(
                {ci, false, std::atan2(outward.y, outward.x)});
        }
        if (col.end_junction) {
            const P2 outward = outward_tangent(col.rail_a, col.rail_b, true);
            byJunction[*col.end_junction].push_back({ci, true, std::atan2(outward.y, outward.x)});
        }
    }
    for (auto& [id, branches] : byJunction) {
        std::sort(
            branches.begin(), branches.end(),
            [](const JunctionBranch& a, const JunctionBranch& b) { return a.angle < b.angle; });
    }
    return byJunction;
}

// --- StableBranchEnd + JunctionSeparator + secteurs de jonction (§ suite) ---
//
// L'ancienne résolution (`resolve_junction_anchors`, supprimée) cherchait un
// sommet reflex ou une intersection de contour PARTAGÉE entre deux branches
// adjacentes, puis déplaçait le dernier nœud de chaque rail vers ce point
// (`set_terminal_point`) — la diagonale/l'éventail observés sur "trident".
//
// Une deuxième correction (toujours sans déplacer aucun rail) capturait la
// DERNIÈRE section transversale stable de chaque branche comme bridge, puis
// cherchait par optimisation combinatoire, PARMI PLUSIEURS candidates de ce
// type (reculs variés le long de la même branche), celle qui minimisait
// l'aire du `JunctionCore`. Toujours insuffisant sur "trident" : reculer une
// section transversale NE CHANGE PAS où se trouve la véritable frontière
// géométrique entre deux branches (l'encoche réelle du contour) — seule la
// combinatoire sur les sections transversales ne peut donc pas la trouver, le
// noyau résiduel restait grand (~22 mm²) et couvrait presque toute la
// confluence.
//
// Défaut architectural identifié : une dernière section transversale stable
// n'a AUCUNE raison d'être la frontière définitive entre deux branches. Ce
// qui suit sépare donc explicitement deux concepts :
//
// 1. `StableBranchEnd` : la dernière VRAIE section transversale stable d'une
//    branche (ce qu'on appelait jusqu'ici le « bridge ») — marque la fin de
//    son corps régulier, point d'entrée du traitement de jonction. Unique
//    par branche (fini la recherche combinatoire sur plusieurs reculs).
// 2. `JunctionSeparator` : la frontière locale PARTAGÉE entre deux branches
//    angulairement adjacentes, construite DANS la zone de confluence — un
//    sommet reflex local du contour (l'encoche réelle), ou à défaut
//    l'intersection de la bissectrice angulaire avec le contour. Ce n'est
//    PAS une section transversale classique, et ce n'est jamais un point
//    sur un rail.
//
// La région locale de jonction (disque de rayon MINIMAL — la distance à la
// plus proche `StableBranchEnd`, jamais une constante arbitraire — autour du
// nœud) est partitionnée en un secteur par branche, chacun bordé par ses deux
// `JunctionSeparator` voisins et l'arc de contour réel jusqu'à son propre
// `StableBranchEnd`. Le `JunctionCore` résiduel est le polygone des
// `JunctionSeparator` eux-mêmes (simple par construction : sommets triés par
// angle autour d'un centre commun = polygone en étoile) — jamais calculé
// comme « région entière moins colonnes ». AUCUN rail n'est jamais modifié :
// StableBranchEnd/JunctionSeparator/secteurs sont des constructions purement
// diagnostiques qui n'affectent ni ne déplacent la moindre station de rail.

// Sommets REFLEX (concaves) d'un polygone fermé, indépendant du sens de
// parcours (le signe de l'aire signée détermine le sens ; un sommet est
// reflex si son virage local est de signe OPPOSÉ à ce sens). Ce sont les
// « encoches » d'une jonction en Y/T/croix — les points où le contour réel
// bascule d'une branche à sa voisine ; le candidat naturel pour un
// `JunctionSeparator`.
std::vector<P2> reflex_vertices(const Poly& poly) {
    std::vector<P2> out;
    const std::size_t n = poly.size();
    if (n < 3) {
        return out;
    }
    const double orientSign = signed_area(poly) >= 0.0 ? 1.0 : -1.0;
    for (std::size_t i = 0; i < n; ++i) {
        const P2& prev = poly[(i + n - 1) % n];
        const P2& cur = poly[i];
        const P2& next = poly[(i + 1) % n];
        const double turn = cross(cur - prev, next - cur);
        if (turn * orientSign < 0.0) {
            out.push_back(cur);
        }
    }
    return out;
}

// Dernière section transversale RÉELLEMENT stable d'une branche à une
// jonction (§ StableBranchEnd) : capture telle quelle une station déjà
// présente dans le rail (`trim_unstable_junction_tail` l'a déjà amenée à sa
// position finale dans `build_column`) — jamais déplacée.
struct StableBranchEnd {
    std::size_t columnIndex{0};
    bool atEnd{false};
    P2 rail_a_point{};
    P2 rail_b_point{};
    P2 axis_point{};
    P2 tangent{};
    double width_um{0.0};
    // Côté qui fait face à la branche SUIVANTE (angle croissant autour de la
    // jonction) / PRÉCÉDENTE. Attention : ce n'est PAS toujours
    // `rail_a`/`rail_b` respectivement — la convention gauche/droite d'un
    // rail est relative au sens de parcours de SON PROPRE axe interne
    // (`build_column`), qui va tantôt de la jonction vers le bout ouvert,
    // tantôt l'inverse selon l'orientation de l'arête du squelette
    // (`atEnd`) ; `tangent` (`outward_tangent`, toujours orienté
    // correctement VERS l'intérieur de la branche quel que soit `atEnd`)
    // sert de référence fiable pour déterminer lequel des deux est
    // réellement le côté « suivant ».
    P2 leading_point{};
    P2 trailing_point{};
};

// `offset` : nombre de stations en retrait du bout de rail (0 = la toute
// dernière, comme avant). `resolve_junction` l'augmente pour une branche
// dont la coupe, bien que déjà stable en LARGEUR (`trim_unstable_junction_tail`),
// s'avère encore géométriquement incompatible avec une branche voisine
// (croise son rail) — un signal de position que le seul critère de largeur
// ne peut pas détecter (cf. commentaire de `build_separator`).
StableBranchEnd make_stable_branch_end(const SatinColumnGeometry& col, const JunctionBranch& b,
                                       std::size_t offset = 0) {
    StableBranchEnd e;
    e.columnIndex = b.columnIndex;
    e.atEnd = b.atEnd;
    e.rail_a_point = station_point(col.rail_a, b.atEnd, offset);
    e.rail_b_point = station_point(col.rail_b, b.atEnd, offset);
    e.axis_point = (e.rail_a_point + e.rail_b_point) * 0.5;
    e.tangent = outward_tangent_at(col.rail_a, col.rail_b, b.atEnd, offset);
    e.width_um = norm(e.rail_a_point - e.rail_b_point);

    const P2 leadingNormal{-e.tangent.y, e.tangent.x};
    const bool aIsLeading = dot(e.rail_a_point - e.axis_point, leadingNormal) >= 0.0;
    e.leading_point = aIsLeading ? e.rail_a_point : e.rail_b_point;
    e.trailing_point = aIsLeading ? e.rail_b_point : e.rail_a_point;
    return e;
}

// Construit le `JunctionSeparator` entre deux branches angulairement
// adjacentes (§ JunctionSeparator). Le découpage par SECTEUR ANGULAIRE
// (angle du rayon squelette de chaque branche) a été essayé puis abandonné :
// une branche large (dont les deux points `leading`/`trailing` peuvent
// s'écarter de plus de 90° autour du nœud) fait que l'encoche réelle entre
// elle et sa voisine tombe, en angle pur, du côté de l'AUTRE secteur — sur
// "trident", l'encoche entre la branche verticale (6 mm) et la branche fine
// horizontale se retrouvait ainsi classée dans le secteur horizontale/
// diagonale, laissant les deux secteurs qui en avaient réellement besoin
// sans aucun candidat et repliés sur un rayon arbitraire qui pouvait heurter
// le mur d'une branche sans rapport (noyau résiduel ~14 mm², bien trop grand).
//
// Le repère fiable est la CONTIGUÏTÉ SUR LE CONTOUR, pas l'angle depuis le
// nœud : l'encoche entre la branche `i` et la branche `i+1` est nécessairement
// sur l'arc de contour reliant leur point `leading`/`trailing` respectif (le
// plus court des deux, `ContourDirection::Shortest`). On y cherche le sommet
// reflex le plus profond (le plus proche du nœud) parmi ceux qui appartiennent
// réellement à CET arc ; à défaut (encoche non anguleuse), on replie sur le
// milieu curviligne du même arc — qui reste, par construction, dans la bonne
// zone (contrairement à un rayon lancé depuis le nœud).
std::optional<P2> build_separator(const std::vector<P2>& reflexVertices,
                                  const ContourPolyline& contour, P2 leadPoint, P2 trailPoint,
                                  P2 center, double radius) {
    const auto projLead = project_to_contour(contour, leadPoint);
    const auto projTrail = project_to_contour(contour, trailPoint);
    const Poly arc = extract_contour_arc(contour, projLead.arc_length, projTrail.arc_length,
                                         ContourDirection::Shortest);
    if (arc.size() < 2) {
        return std::nullopt;
    }

    std::optional<P2> best;
    double bestDist = radius;
    for (const P2& rv : reflexVertices) {
        bool onArc = false;
        for (const P2& p : arc) {
            if (norm(p - rv) < 1.0) {
                onArc = true;
                break;
            }
        }
        if (!onArc) {
            continue;
        }
        const double d = norm(rv - center);
        if (d < 1e-6 || d > bestDist) {
            continue;
        }
        bestDist = d;
        best = rv;
    }
    if (best) {
        return best;
    }

    const double span =
        contour_arc_span(projLead.arc_length, projTrail.arc_length, contour.total_length,
                         ContourDirection::Shortest);
    if (span > radius * 3.0) {
        return std::nullopt;  // les deux branches ne sont pas des voisines locales plausibles.
    }
    const P2 mid = arc[arc.size() / 2];
    return norm(mid - center) <= radius * 1.5 ? std::optional<P2>{mid} : std::nullopt;
}

// `true` si le polygone `boundary` (fermé) s'auto-intersecte : deux arêtes
// NON adjacentes qui se croisent. Un `JunctionCore` replié sur lui-même
// calculerait une aire (formule du lacet) artificiellement petite par
// annulation partielle — un faux minimum géométriquement invalide.
bool polygon_self_intersects(const Poly& boundary) {
    const std::size_t m = boundary.size();
    if (m < 4) {
        return false;
    }
    for (std::size_t i = 0; i < m; ++i) {
        const P2 a0 = boundary[i];
        const P2 a1 = boundary[(i + 1) % m];
        for (std::size_t j = i + 2; j < m; ++j) {
            if (i == 0 && j + 1 == m) {
                continue; // arêtes adjacentes par bouclage (partagent boundary[0])
            }
            const P2 b0 = boundary[j];
            const P2 b1 = boundary[(j + 1) % m];
            if (segments_cross(a0, a1, b0, b1)) {
                return true;
            }
        }
    }
    return false;
}

// Première paire de StableBranchEnd dont les coupes transversales se
// croisent (index dans `ends`), ou `nullopt` si aucune ne se croise. Un
// croisement direct ici signale un rail déjà incohérent — aucune
// construction de séparateur ne peut réparer ça.
std::optional<std::pair<std::size_t, std::size_t>>
find_crossing_end_pair(const std::vector<StableBranchEnd>& ends) {
    const std::size_t n = ends.size();
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            if (segments_cross(ends[i].rail_a_point, ends[i].rail_b_point, ends[j].rail_a_point,
                               ends[j].rail_b_point)) {
                return std::pair{i, j};
            }
        }
    }
    return std::nullopt;
}

// `true` si le SEGMENT transversal de `e` traverse l'un des deux rails de
// `other` (§ invariant « aucun rail ne traverse la confluence ») : une
// StableBranchEnd est une coupe LOCALE de sa propre branche, elle ne doit
// jamais couper à travers le corps d'une branche voisine.
bool end_crosses_other_rail(const StableBranchEnd& e, const SatinColumnGeometry& other) {
    const auto crossesPolyline = [&](const geometry::Path& rail) {
        for (std::size_t k = 0; k + 1 < rail.nodes.size(); ++k) {
            const P2 p0 = rail_point(rail, k);
            const P2 p1 = rail_point(rail, k + 1);
            if (segments_cross(e.rail_a_point, e.rail_b_point, p0, p1)) {
                return true;
            }
        }
        return false;
    };
    return crossesPolyline(other.rail_a) || crossesPolyline(other.rail_b);
}

// Résultat complet de la résolution d'une jonction : une `StableBranchEnd`
// et un secteur par branche (même ordre que les `JunctionBranch` fournies en
// entrée), un `JunctionSeparator` optionnel par espace angulaire (index i =
// entre la branche i et la branche (i+1) mod n), et le `JunctionCore`
// résiduel qui en découle.
struct JunctionResolution {
    std::vector<StableBranchEnd> ends;
    std::vector<std::optional<P2>> separators;
    std::vector<Poly> sectors;
    Poly coreBoundary;
    double coreArea{0.0};
    double localRadius{0.0};
    double actualMaxRadius{0.0};
};

// Construit le secteur local d'une branche (§4) : borné par le séparateur
// PRÉCÉDENT (arc de contour jusqu'au côté `trailing` de la branche), le
// segment transversal `trailing -> leading` de sa `StableBranchEnd`, puis
// l'arc de contour du côté `leading` jusqu'au séparateur SUIVANT. Un
// séparateur manquant (aucune encoche ni intersection de bissectrice
// trouvée dans le rayon local) laisse ce côté du secteur ouvert — repli
// géré par l'appelant lors de la construction du `JunctionCore`.
Poly build_sector(const StableBranchEnd& e, const std::optional<P2>& sepPrev,
                  const std::optional<P2>& sepNext, const ContourPolyline& contour) {
    Poly boundary;
    if (sepPrev) {
        boundary.push_back(*sepPrev);
        const auto projSep = project_to_contour(contour, *sepPrev);
        const auto projTrail = project_to_contour(contour, e.trailing_point);
        Poly arc = extract_contour_arc(contour, projSep.arc_length, projTrail.arc_length,
                                       ContourDirection::Shortest);
        for (std::size_t k = 1; k + 1 < arc.size(); ++k) {
            boundary.push_back(arc[k]);
        }
    }
    boundary.push_back(e.trailing_point);
    boundary.push_back(e.leading_point);
    if (sepNext) {
        const auto projLead = project_to_contour(contour, e.leading_point);
        const auto projSep = project_to_contour(contour, *sepNext);
        Poly arc = extract_contour_arc(contour, projLead.arc_length, projSep.arc_length,
                                       ContourDirection::Shortest);
        for (std::size_t k = 1; k + 1 < arc.size(); ++k) {
            boundary.push_back(arc[k]);
        }
        boundary.push_back(*sepNext);
    }
    return boundary;
}

// Résout une jonction complète (§ StableBranchEnd / JunctionSeparator ci-
// dessus) : `nullopt` si un défaut structurel réel rend la confluence
// géométriquement incohérente (StableBranchEnd égarée, secteur qui traverse
// une branche voisine, ou — cas limite — noyau qui s'auto-croise malgré
// tout), auquel cas l'appelant refuse proprement la génération.
std::optional<JunctionResolution>
resolve_junction(const std::vector<SatinColumnGeometry>& columns,
                 const std::vector<JunctionBranch>& branches,
                 const std::vector<ContourPolyline>& contours, const std::vector<Poly>& polys,
                 const std::vector<P2>& reflexVertices, P2 center, double configuredRadius) {
    const std::size_t n = branches.size();
    if (n < 2) {
        return std::nullopt;
    }

    JunctionResolution res;
    res.ends.reserve(n);
    for (const auto& b : branches) {
        res.ends.push_back(make_stable_branch_end(columns[b.columnIndex], b));
    }

    // `trim_unstable_junction_tail` (dans `build_column`) ne détecte que la
    // DÉRIVE DE LARGEUR du bourrelet de confluence — pas sa forme. Sur une
    // jonction en T très asymétrique (bras large, branche fine), la toute
    // première section d'un bras peut avoir une largeur déjà plausible tout
    // en étant positionnée dans le corridor de la branche voisine (son axe
    // n'a pas encore quitté le bourrelet commun) : sa coupe transversale
    // croise alors le rail de cette voisine, ce qui prouve — largeur ou pas —
    // qu'elle n'est PAS réellement stable. On recule encore d'une station la
    // ou les `StableBranchEnd` impliquées dans un tel croisement, jusqu'à ce
    // que la confluence soit géométriquement cohérente ou qu'il n'y ait plus
    // aucune station disponible (refus propre, comme avant).
    std::vector<std::size_t> offsets(n, 0);
    constexpr std::size_t kMaxIterations = 200;
    for (std::size_t iter = 0; iter < kMaxIterations; ++iter) {
        std::optional<std::pair<std::size_t, std::size_t>> bad = find_crossing_end_pair(res.ends);
        if (!bad) {
            for (std::size_t i = 0; i < n && !bad; ++i) {
                for (std::size_t j = 0; j < n; ++j) {
                    if (i != j &&
                        end_crosses_other_rail(res.ends[i], columns[branches[j].columnIndex])) {
                        bad = std::pair{i, j};
                        break;
                    }
                }
            }
        }
        if (!bad) {
            break;
        }
        const auto retract = [&](std::size_t idx) {
            const auto& col = columns[branches[idx].columnIndex];
            const std::size_t nodeCount = col.rail_a.nodes.size();
            const std::size_t maxOffset = nodeCount < 2 ? 0 : nodeCount - 2;
            if (offsets[idx] >= maxOffset) {
                return false;
            }
            ++offsets[idx];
            res.ends[idx] = make_stable_branch_end(col, branches[idx], offsets[idx]);
            return true;
        };
        const bool retractedFirst = retract(bad->first);
        const bool retractedSecond = retract(bad->second);
        if (!retractedFirst && !retractedSecond) {
            return std::nullopt;
        }
    }
    if (find_crossing_end_pair(res.ends)) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            if (i != j && end_crosses_other_rail(res.ends[i], columns[branches[j].columnIndex])) {
                return std::nullopt;
            }
        }
    }

    // Rayon local (§2) : dérivé des données réelles — jamais une constante
    // arbitraire. Doit être assez grand pour CONTENIR la `StableBranchEnd`
    // la plus éloignée (sinon son propre secteur ne pourrait pas la
    // rejoindre) : la distance MAXIMALE, pas minimale — une branche dont la
    // fin stable se trouve déjà (sans recul) exactement au nœud (largeur
    // stable dès la première station, aucune instabilité à amputer — cas
    // réel sur "trident", branche verticale bien plus large que ses deux
    // voisines) a une distance nulle et ne doit PAS, à elle seule,
    // effondrer le rayon de toute la jonction. Petite marge (`kRadiusMargin`)
    // pour que la recherche de séparateur (sommet reflex / bissectrice)
    // puisse dépasser légèrement la plus éloignée des `StableBranchEnd` sans
    // être coupée court. `configuredRadius` reste un plafond de sécurité
    // (formes dégénérées), jamais la valeur reportée comme rayon réel.
    constexpr double kRadiusMargin = 1.2;
    double farthest = 0.0;
    for (const auto& e : res.ends) {
        farthest = std::max(farthest, norm(e.axis_point - center));
    }
    const double localRadius = std::min(farthest * kRadiusMargin, configuredRadius);
    if (!(localRadius > 0.0)) {
        return std::nullopt;
    }
    res.localRadius = localRadius;

    // Contour porteur de cette jonction (toutes ses branches touchent le
    // même contour en pratique — les anneaux, seul cas à plusieurs contours
    // pertinents, sont traités séparément par `build_annular_sections`).
    std::size_t contourIndex = 0;
    {
        double best = std::numeric_limits<double>::max();
        for (std::size_t ci = 0; ci < contours.size(); ++ci) {
            const double d = project_to_contour(contours[ci], res.ends.front().leading_point).distance;
            if (d < best) {
                best = d;
                contourIndex = ci;
            }
        }
    }
    const ContourPolyline& contour = contours[contourIndex];

    // JunctionSeparator (§3) : un par paire de branches angulairement
    // adjacentes, cherché sur l'arc de contour qui les relie réellement
    // (contiguïté de contour, pas secteur angulaire — cf. commentaire de
    // `build_separator`).
    res.separators.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        res.separators[i] = build_separator(reflexVertices, contour, res.ends[i].leading_point,
                                            res.ends[(i + 1) % n].trailing_point, center,
                                            localRadius);
    }

    // Secteurs (§4).
    res.sectors.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        const auto& sepPrev = res.separators[(i + n - 1) % n];
        const auto& sepNext = res.separators[i];
        res.sectors[i] = build_sector(res.ends[i], sepPrev, sepNext, contour);
    }

    // JunctionCore (§6) : le polygone des `JunctionSeparator` trouvés, dans
    // l'ordre angulaire — simple PAR CONSTRUCTION (sommets triés par angle
    // autour d'un centre commun = polygone en étoile, jamais auto-croisé).
    // Repli explicite pour un espace angulaire SANS séparateur (aucune
    // encoche ni bissectrice n'a atteint le contour dans le rayon local,
    // cas limite) : relie directement les deux `StableBranchEnd` voisines
    // par l'arc de contour réel — dégradé mais jamais invalide.
    Poly core;
    for (std::size_t i = 0; i < n; ++i) {
        if (res.separators[i]) {
            core.push_back(*res.separators[i]);
            continue;
        }
        const auto& cur = res.ends[i];
        const auto& nxt = res.ends[(i + 1) % n];
        const auto projLead = project_to_contour(contour, cur.leading_point);
        const auto projTrail = project_to_contour(contour, nxt.trailing_point);
        Poly arc = extract_contour_arc(contour, projLead.arc_length, projTrail.arc_length,
                                       ContourDirection::Shortest);
        for (const P2& p : arc) {
            core.push_back(p);
        }
    }
    if (polygon_self_intersects(core)) {
        return std::nullopt;
    }
    res.coreBoundary = core;
    res.coreArea = core.size() >= 3 ? std::abs(signed_area(core)) : 0.0;
    res.actualMaxRadius = 0.0;
    for (const P2& p : core) {
        res.actualMaxRadius = std::max(res.actualMaxRadius, norm(p - center));
    }
    return res;
}

// --- § étape 10 : ordre de couture à une jonction --------------------------
//
// Heuristique déterministe (première version, cf. `docs/source/satin.md`) :
// branche la plus large en premier (cousue dessous), branches secondaires
// ensuite par largeur décroissante, branche la plus fine en dernier (cousue
// dessus, masque les transitions des précédentes). Ne modifie AUCUNE
// géométrie -- ordonne seulement les index déjà construits.
std::vector<SatinJunctionPlan> plan_junction_stitch_order(
    const std::vector<ParametricSatinObject>& columns,
    const std::map<std::uint32_t, std::vector<JunctionBranch>>& junctionBranches) {
    std::vector<SatinJunctionPlan> plans;
    for (const auto& [junctionId, branches] : junctionBranches) {
        if (branches.size() < 2) {
            continue;
        }
        SatinJunctionPlan plan;
        plan.junction_id = junctionId;
        std::vector<std::uint32_t> order;
        for (const auto& b : branches) {
            order.push_back(static_cast<std::uint32_t>(b.columnIndex));
        }
        std::stable_sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) {
            return columns[a].mean_width_um > columns[b].mean_width_um;
        });
        plan.stitch_order = std::move(order);
        plans.push_back(std::move(plan));
    }
    return plans;
}

} // namespace

SatinColumnsResult build_satin_columns(const geometry::PathSet& region,
                                       const SatinColumnsParameters& params) {
    SatinColumnsResult r;
    auto analysis = analyze_region(region, params.analysis);
    if (!analysis) {
        r.refusal = "analyse impossible : " + analysis.error().message;
        return r;
    }
    r.report = analysis->report;
    r.debug = analysis->debug;
    r.status = analysis->report.status;
    const auto finalize_sections = [&r] {
        const auto count = static_cast<std::uint32_t>(r.columns.size());
        for (std::size_t i = 0; i < r.columns.size(); ++i) {
            r.columns[i].section_index = static_cast<std::uint32_t>(i);
            r.columns[i].section_count = count;
        }
    };

    if (region.holes.size() == 1) {
        std::string annularRefusal;
        auto sections = build_annular_sections(region, params, annularRefusal);
        if (!sections) {
            r.refusal = "anneau non appariable sans croisement : " + annularRefusal;
            return r;
        }
        r.columns = std::move(*sections);
        r.status = SatinabilityStatus::RequiresDecomposition;
        r.report.status = r.status;
        r.report.issues.clear();
        r.report.issues.push_back(
            {"Anneau decompose en quatre sections satin ouvertes raccordees."});
        r.warnings.push_back("couture fermee : routage cyclique de quatre sections");
        finalize_sections();
        return r;
    }

    const auto& graph = analysis->debug.graph;
    const std::vector<Poly> polys = region_polys(region);

    // Validation par jonction (§5/§10) puis résolution StableBranchEnd /
    // JunctionSeparator / secteurs / JunctionCore (§ suite) : appelé une fois
    // TOUTES les colonnes construites, quel que soit le statut
    // (`RequiresDecomposition` ou `Suitable` avec un bout touchant tout de
    // même une jonction). AUCUNE mutation de rail ici : `resolve_junction` ne
    // fait que LIRE les rails déjà produits par `build_column`. Une
    // incohérence géométrique réelle (StableBranchEnd égarée/traversante,
    // noyau auto-croisé) refuse proprement la génération ; un simple vide
    // résiduel entre branches ne refuse rien, il est exposé via
    // `r.junction_cores`.
    const auto resolve_and_validate_junctions = [&] {
        if (!params.anchor_junction_ends || r.columns.empty()) {
            return;
        }
        const double configuredRadius = static_cast<double>(params.junction_core_radius.value);
        std::vector<ContourPolyline> contours;
        contours.reserve(polys.size());
        for (const auto& p : polys) {
            contours.push_back(make_contour_polyline(p));
        }
        const std::vector<P2> reflexVertices = polys.empty() ? std::vector<P2>{}
                                                              : reflex_vertices(polys.front());
        const auto junctionBranches = collect_junction_branches(r.columns);

        for (const auto& [junctionId, branches] : junctionBranches) {
            if (junctionId >= graph.nodes.size()) {
                continue;
            }
            std::size_t expectedEnds = 0;
            for (const auto& e : graph.edges) {
                if (e.from == junctionId) ++expectedEnds;
                if (e.to == junctionId) ++expectedEnds;
            }
            if (branches.size() != expectedEnds) {
                const std::string problem =
                    "jonction " + std::to_string(junctionId) + " incomplete (" +
                    std::to_string(branches.size()) + "/" + std::to_string(expectedEnds) +
                    " branches disponibles)";
                r.warnings.push_back("refus : " + problem);
                r.columns.clear();
                r.refusal = "jonction incoherente : " + problem;
                return;
            }
            if (branches.size() < 2) {
                continue; // rien a resoudre : un seul bout touche ce noeud.
            }
            const auto& node = graph.nodes[junctionId];
            const P2 center{static_cast<double>(node.position.x.value),
                            static_cast<double>(node.position.y.value)};
            auto resolved = resolve_junction(r.columns, branches, contours, polys, reflexVertices,
                                             center, configuredRadius);
            if (!resolved) {
                const std::string problem =
                    "jonction " + std::to_string(junctionId) +
                    " : confluence geometriquement incoherente (StableBranchEnd egaree/"
                    "traversante, ou noyau auto-croise)";
                r.warnings.push_back("refus : " + problem);
                r.columns.clear();
                r.refusal = "jonction incoherente : " + problem;
                return;
            }
            // Expose StableBranchEnd / JunctionSeparator / secteurs (diagnostic SVG/tests).
            for (std::size_t bi = 0; bi < branches.size(); ++bi) {
                const auto& e = resolved->ends[bi];
                StableBranchEndInfo endInfo;
                endInfo.junction_id = junctionId;
                endInfo.column_index = e.columnIndex;
                endInfo.at_end = e.atEnd;
                endInfo.rail_a_point = to_um(e.rail_a_point);
                endInfo.rail_b_point = to_um(e.rail_b_point);
                r.stable_branch_ends.push_back(endInfo);

                if (resolved->separators[bi]) {
                    JunctionSeparatorInfo sepInfo;
                    sepInfo.junction_id = junctionId;
                    sepInfo.point = to_um(*resolved->separators[bi]);
                    sepInfo.column_index_before = resolved->ends[bi].columnIndex;
                    sepInfo.column_index_after = resolved->ends[(bi + 1) % branches.size()].columnIndex;
                    r.junction_separators.push_back(sepInfo);
                }

                JunctionSectorInfo sectorInfo;
                sectorInfo.junction_id = junctionId;
                sectorInfo.column_index = e.columnIndex;
                sectorInfo.boundary.reserve(resolved->sectors[bi].size());
                for (const P2& p : resolved->sectors[bi]) {
                    sectorInfo.boundary.push_back(to_um(p));
                }
                r.junction_sectors.push_back(std::move(sectorInfo));
            }
            JunctionCore pub;
            pub.junction_id = junctionId;
            pub.area_um2 = resolved->coreArea;
            pub.configured_radius_um = configuredRadius;
            pub.local_radius_um = resolved->localRadius;
            pub.actual_max_radius_um = resolved->actualMaxRadius;
            pub.requires_fill = pub.area_um2 > params.junction_core_significant_area_um2;
            pub.boundary.reserve(resolved->coreBoundary.size());
            for (const P2& p : resolved->coreBoundary) {
                pub.boundary.push_back(to_um(p));
            }
            if (pub.requires_fill) {
                r.warnings.push_back(
                    "jonction " + std::to_string(junctionId) +
                    " : zone centrale significative (" +
                    std::to_string(pub.area_um2 / 1'000'000.0) +
                    " mm2) -- necessite un objet de remplissage separe");
            }
            r.junction_cores.push_back(std::move(pub));
        }
    };

    const auto try_edge = [&](const SkeletonEdge& e) {
        const bool startIsJunction = graph.nodes[e.from].type == SkeletonNodeType::Junction;
        const bool endIsJunction = graph.nodes[e.to].type == SkeletonNodeType::Junction;
        // Un bout de jonction n'est jamais ÉTENDU (il doit rester exactement
        // au nœud du squelette) ; seul un bout OUVERT l'est. Un bout de
        // jonction est en revanche ampute (`trim_unstable_junction_tail`
        // dans `build_column`) puis verrouillé tel quel comme `JunctionBridge`
        // de la colonne, une fois toutes les colonnes construites — cf.
        // `resolve_and_validate_junctions` ci-dessus.
        if (auto col = build_column(e.centerline, polys, params, !startIsJunction, !endIsJunction,
                                    r.warnings)) {
            if (startIsJunction) {
                col->start_junction = e.from;
            }
            if (endIsJunction) {
                col->end_junction = e.to;
            }
            r.columns.push_back(std::move(*col));
        } else {
            r.warnings.push_back("branche ignoree (arete " + std::to_string(e.id) +
                                 ") : voir le diagnostic ci-dessus");
        }
    };

    // --- § mode Parametric : une arête = un objet paramétrique INDÉPENDANT
    // (§ étape 9 — jamais fusionné avec ses voisins à une jonction). Le
    // recouvrement local est déjà appliqué DANS `build_parametric_object` ;
    // ici on ne fait que collecter, valider, et planifier l'ordre de couture
    // (§ étape 10) — aucune mutation géométrique après coup.
    const auto try_edge_parametric = [&](const SkeletonEdge& e) {
        const bool startIsJunction = graph.nodes[e.from].type == SkeletonNodeType::Junction;
        const bool endIsJunction = graph.nodes[e.to].type == SkeletonNodeType::Junction;
        if (auto obj = build_parametric_object(e.centerline, polys, params, !startIsJunction,
                                               !endIsJunction, r.warnings)) {
            if (startIsJunction) {
                obj->start_junction = e.from;
            }
            if (endIsJunction) {
                obj->end_junction = e.to;
            }
            if (auto problem = validate_parametric_object(*obj, polys, params)) {
                r.warnings.push_back("objet paramétrique refuse (arete " + std::to_string(e.id) +
                                     ") : " + *problem);
                return;
            }
            r.parametric_columns.push_back(std::move(*obj));
        } else {
            r.warnings.push_back("branche ignoree (arete " + std::to_string(e.id) +
                                 ") : voir le diagnostic ci-dessus");
        }
    };
    const auto finalize_parametric_sections = [&r] {
        const auto count = static_cast<std::uint32_t>(r.parametric_columns.size());
        for (std::size_t i = 0; i < r.parametric_columns.size(); ++i) {
            r.parametric_columns[i].section_index = static_cast<std::uint32_t>(i);
            r.parametric_columns[i].section_count = count;
        }
        r.junction_plans =
            plan_junction_stitch_order(r.parametric_columns,
                                       collect_junction_branches(r.parametric_columns));
    };

    const bool parametric = params.geometry_mode == SatinGeometryMode::Parametric;

    switch (r.status) {
    case SatinabilityStatus::Unsuitable:
        r.refusal = "forme non satinable (trou, trop large ou trop étroite)";
        return r;
    case SatinabilityStatus::Ambiguous:
        r.refusal = "direction ambiguë (forme quasi circulaire)";
        return r;
    case SatinabilityStatus::RequiresDecomposition: {
        if (analysis->report.junction_count > params.max_junctions) {
            r.refusal = "trop de jonctions pour une décomposition fiable";
            return r;
        }
        // Une colonne par ARÊTE du squelette élagué, sans distinction sur le
        // type de ses deux extrémités : une arête reliant deux jonctions (le
        // pont d'un "H", ou plus généralement tout segment entre deux
        // confluences) est un bras aussi réel que ceux menant à une extrémité
        // — la restreindre aux arêtes touchant au moins une extrémité (ancien
        // comportement) laissait ce pont totalement non converti en colonne,
        // sans aucune couture ni avertissement (défaut trouvé par revue :
        // `try_edge` gère déjà indépendamment chaque bout selon son type via
        // `startIsJunction`/`endIsJunction`, donc aucune branche particulière
        // n'était nécessaire ici).
        if (parametric) {
            for (const auto& e : graph.edges) {
                try_edge_parametric(e);
            }
            if (r.parametric_columns.empty()) {
                r.refusal = "aucune branche exploitable";
            }
            finalize_parametric_sections();
            return r;
        }
        for (const auto& e : graph.edges) {
            try_edge(e);
        }
        if (r.columns.empty()) {
            r.refusal = "aucune branche exploitable";
        }
        resolve_and_validate_junctions();
        finalize_sections();
        return r;
    }
    case SatinabilityStatus::Suitable:
    case SatinabilityStatus::SuitableWithWarnings: {
        if (graph.edges.empty()) {
            r.refusal = "squelette vide";
            return r;
        }
        // Axe principal = arête la plus longue.
        const auto longest = std::max_element(
            graph.edges.begin(), graph.edges.end(),
            [](const SkeletonEdge& a, const SkeletonEdge& b) { return a.length_um < b.length_um; });
        if (parametric) {
            try_edge_parametric(*longest);
            if (r.parametric_columns.empty()) {
                r.refusal = "axe principal inexploitable";
            }
            finalize_parametric_sections();
            return r;
        }
        try_edge(*longest);
        if (r.columns.empty()) {
            r.refusal = "axe principal inexploitable";
        }
        resolve_and_validate_junctions();
        finalize_sections();
        return r;
    }
    }
    return r;
}

} // namespace openstitch::auto_satin