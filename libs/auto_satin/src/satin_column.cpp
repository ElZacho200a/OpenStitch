// SPDX-License-Identifier: Apache-2.0
#include "openstitch/auto_satin/satin_column.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <expected>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
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

// Sommets REFLEX (concaves) d'un polygone fermé, indépendant du sens de
// parcours (le signe de l'aire signée détermine le sens ; un sommet est
// reflex si son virage local est de signe OPPOSÉ à ce sens). Ce sont les
// « encoches » d'une jonction en Y/T/croix — les points où le contour réel
// bascule d'une branche à sa voisine (§ ancrage des jonctions).
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

// Ampute la queue d'une extrémité de JONCTION dont la largeur mesurée dérive
// (§ ancrage des jonctions : la section transversale d'une branche, calculée
// depuis sa seule tangente locale, balaie le bourrelet de la confluence — pas
// la ceinture réelle de CETTE branche — dès qu'on approche du nœud du
// squelette ; démontré empiriquement : largeur stable ~5,0 mm loin de la
// jonction, dérivant jusqu'à ~9,2 mm sur la dernière station). On retire les
// stations terminales tant que leur largeur DÉCROÎT strictement vers
// l'intérieur (motif observé : le bourrelet ne retombe pas toujours d'un seul
// saut net, il peut décroître progressivement sur plusieurs stations — ex.
// mesuré sur "y" : 9200 → 8586 → 7576 → 6566 → 5554 → 5000(stable), où CHAQUE
// écart pris isolément reste sous 10 %, mais la dérive cumulée atteint +72 %.
// Un ancien critère à seuil relatif fixe comparant seulement une station à sa
// voisine immédiate s'arrêtait dès le premier écart local < 10 % — souvent le
// tout premier, laissant la quasi-totalité du bourrelet en place — défaut
// trouvé par revue. Toute décroissance stricte signale qu'on est encore dans
// la zone d'influence de la confluence ; une variation progressive légitime
// ailleurs dans la forme ne s'applique jamais ici, cette fonction n'agissant
// QUE sur un bout de jonction, jamais un bout ouvert).
//
// N'ANCRE PLUS le rail terminal sur un sommet reflex (§ audit jonctions
// branchées/concaves) : cette étape se faisait auparavant ICI, de façon
// totalement indépendante pour chaque branche/rail — deux branches
// angulairement adjacentes à la MÊME confluence pouvaient donc choisir des
// sommets reflex différents (ou le même par accident), sans aucune garantie
// de coïncidence exacte au raccord. La résolution des ancres est désormais
// globale par jonction (`resolve_junction_anchors`, après que toutes les
// colonnes ont été construites) : elle trie les branches par angle autour du
// centre de la confluence et impose qu'une même encoche serve d'ancre
// commune aux deux rails qui se font face de part et d'autre. Cette fonction
// se limite donc à la seule amputation ; le rail terminal (post-amputation)
// reste à sa position brute calculée par `cross_section`, en attente de
// résolution globale.
void trim_unstable_junction_tail(std::vector<Station>& st, bool atEnd) {
    constexpr double kEpsilon = 1.0; // tolérance bruit flottant, en µm
    while (st.size() >= 3) {
        const Station& last = atEnd ? st.back() : st.front();
        const Station& prev = atEnd ? st[st.size() - 2] : st[1];
        if (last.width > prev.width + kEpsilon) {
            if (atEnd) {
                st.pop_back();
            } else {
                st.erase(st.begin());
            }
        } else {
            break;
        }
    }
}

// Construit une colonne depuis une centerline (axe) et les polygones de région.
// `extendStart`/`extendEnd` : true si ce bout est OUVERT (pas de jonction) et
// doit donc être étendu jusqu'au bord réel — cf. `extend_tip`. Un bout de
// jonction (`!extendStart`/`!extendEnd`) est amputé par
// `trim_unstable_junction_tail` seulement ; son ancrage sur une encoche
// partagée avec la branche voisine se fait après coup, globalement par
// jonction (`resolve_junction_anchors`), pas ici. `warnings` reçoit un
// diagnostic explicite à chaque station corrigée ou refus (§ audit génération
// partielle sur formes concaves/larges : plus aucune station ni aucun trou
// n'est ignoré en silence — voir les commentaires ci-dessous pour chaque
// étape).
std::optional<SatinColumnGeometry>
build_column(const std::vector<Vec2um>& centerline, const std::vector<Poly>& polys,
             const SatinColumnsParameters& params, bool extendStart, bool extendEnd,
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
    if (st.size() < 2) {
        warnings.push_back(
            "colonne refusee : moins de 2 stations valides apres nettoyage anti-croisement");
        return std::nullopt;
    }

    // Validation de continuité et de couverture du CŒUR de l'axe (point 6),
    // c'est-à-dire AVANT extension des bouts / ancrage de jonction : ces deux
    // étapes ont leurs propres tolérances dédiées et déjà testées (`extend_tip`
    // impose 5 % de rétrécissement maximum par pas mais introduit
    // délibérément un grand saut de largeur au tout dernier point de fermeture
    // — largeur PLANCHER `tip_min_width` contre la largeur réelle voisine —
    // et `trim_and_anchor_junction_end` ampute justement la queue dont la
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
    double coreAxisLength = 0.0;
    for (std::size_t idx = firstIdx + 1; idx <= lastIdx; ++idx) {
        coreAxisLength += norm(axis[idx] - axis[idx - 1]);
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
        if (extendEnd) {
            if (params.extend_open_ends) {
                const Station& last = st.back();
                auto tail = extend_tip(polys, last.axis, last.tangent, last.tangent, last.width,
                                       maxWidth, stepLen, tipMin);
                for (auto& s : tail) {
                    st.push_back(std::move(s));
                }
            }
        } else if (params.anchor_junction_ends) {
            trim_unstable_junction_tail(st, /*atEnd=*/true);
        }
        if (extendStart) {
            if (params.extend_open_ends) {
                const Station& first = st.front();
                auto head = extend_tip(polys, first.axis, first.tangent * -1.0, first.tangent,
                                       first.width, maxWidth, stepLen, tipMin);
                std::reverse(head.begin(), head.end());
                st.insert(st.begin(), head.begin(), head.end());
            }
        } else if (params.anchor_junction_ends) {
            trim_unstable_junction_tail(st, /*atEnd=*/false);
        }
    }

    if (st.size() < 2) {
        warnings.push_back("colonne refusee : moins de 2 stations valides apres extension/ancrage");
        return std::nullopt;
    }

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

void set_terminal_point(geometry::Path& rail, bool atEnd, P2 p) {
    if (atEnd) {
        rail.nodes.back().pos = to_um(p);
    } else {
        rail.nodes.front().pos = to_um(p);
    }
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
// espaces angulaires entre branches adjacentes.
std::map<std::uint32_t, std::vector<JunctionBranch>>
collect_junction_branches(const std::vector<SatinColumnGeometry>& columns) {
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
        std::sort(branches.begin(), branches.end(),
                  [](const JunctionBranch& a, const JunctionBranch& b) { return a.angle < b.angle; });
    }
    return byJunction;
}

// Sommet reflex le plus proche du centre parmi ceux dont l'angle (relatif au
// centre) tombe dans le secteur [lo, hi) (`hi` peut dépasser 2π : la
// comparaison ramène l'angle du candidat dans la bonne branche de 2π pour
// gérer le rebouclage du dernier secteur sur le premier).
std::optional<P2> best_anchor_in_sector(const std::vector<P2>& reflexVertices, P2 center, double lo,
                                        double hi, double radius) {
    constexpr double kTwoPi = 2.0 * std::numbers::pi;
    std::optional<P2> best;
    double bestDist = radius;
    for (const P2& v : reflexVertices) {
        const P2 rel = v - center;
        const double d = norm(rel);
        if (d < 1e-6 || d > bestDist) {
            continue;
        }
        double a = std::atan2(rel.y, rel.x);
        while (a < lo) {
            a += kTwoPi;
        }
        if (a > hi + 1e-9) {
            continue;
        }
        bestDist = d;
        best = v;
    }
    return best;
}

// Met à jour le barreau terminal (premier/dernier de `col.rungs`) correspondant
// au bout `atEnd` d'une colonne, pour qu'il reflète les positions courantes
// des rails terminaux — appelé après toute mutation de `rail_a`/`rail_b`.
void sync_terminal_rung(SatinColumnGeometry& col, bool atEnd) {
    if (col.rungs.empty()) {
        return;
    }
    auto& rung = atEnd ? col.rungs.back() : col.rungs.front();
    rung.a = to_um(terminal_point(col.rail_a, atEnd));
    rung.b = to_um(terminal_point(col.rail_b, atEnd));
}

// Applique la résolution globale : pour chaque jonction, pour chaque espace
// angulaire entre deux branches adjacentes, pose (si trouvée) une ancre
// PARTAGÉE sur les deux rails qui se font face — le rail A (gauche, cf.
// convention `+N = gauche`) de la branche « avant » dans l'ordre angulaire,
// et le rail B (droite) de la branche « après ». Une station sans ancre à
// portée garde sa position brute post-amputation (repli, comme avant l'audit
// — toutes les jonctions ne sont pas des étoiles symétriques à n encoches
// pour n branches).
//
// Défaut trouvé en cours d'audit : la section transversale brute d'une
// branche, tout près d'un sommet reflex (encoche), rétrécit naturellement
// vers zéro du côté qui longe l'encoche — c'est la géométrie réelle, pas une
// erreur de calcul. Quand SEUL le côté opposé de cette branche reçoit une
// ancre partagée (l'autre gap, côté encoche, n'en trouvant aucune — cas
// légitime d'une confluence asymétrique à moins d'encoches que de branches,
// cf. jonction en T), le rail non ancré restait à cette position brute
// quasi nulle : le barreau terminal de la branche devenait dégénéré (largeur
// ~0), régression sur l'invariant « aucun barreau dégénéré » déjà couvert par
// les tests existants. Correction : une fois toutes les ancres partagées
// posées, toute branche dont la largeur terminale finale reste sous
// `tip_min_width` est repoussée à cette largeur plancher — en ne déplaçant
// QUE le côté non ancré (le côté ancré doit rester EXACTEMENT à l'ancre
// partagée, sous peine de rouvrir le défaut que l'ancrage corrige), le long
// de la normale locale (perpendiculaire à la tangente sortante de la
// branche). Si aucun des deux côtés n'a d'ancre, les deux sont écartés
// symétriquement autour de leur position brute — même principe que le
// bissection de `extend_tip`.
void resolve_junction_anchors(std::vector<SatinColumnGeometry>& columns, const SkeletonGraph& graph,
                              const std::vector<P2>& reflexVertices, double anchorRadius,
                              double tipMinWidth, std::vector<std::string>& warnings) {
    constexpr double kTwoPi = 2.0 * std::numbers::pi;
    const auto byJunction = collect_junction_branches(columns);
    for (const auto& [junctionId, branches] : byJunction) {
        if (junctionId >= graph.nodes.size() || branches.size() < 2) {
            continue;
        }
        const auto& node = graph.nodes[junctionId];
        const P2 center{static_cast<double>(node.position.x.value),
                        static_cast<double>(node.position.y.value)};
        const std::size_t n = branches.size();
        std::map<std::size_t, std::array<bool, 2>> anchoredSides; // index dans `branches` -> {A, B}
        for (std::size_t i = 0; i < n; ++i) {
            const JunctionBranch& cur = branches[i];
            const JunctionBranch& nxt = branches[(i + 1) % n];
            double hi = nxt.angle;
            while (hi <= cur.angle) {
                hi += kTwoPi;
            }
            const auto anchor = best_anchor_in_sector(reflexVertices, center, cur.angle, hi, anchorRadius);
            if (!anchor) {
                continue;
            }
            set_terminal_point(columns[cur.columnIndex].rail_a, cur.atEnd, *anchor);
            sync_terminal_rung(columns[cur.columnIndex], cur.atEnd);
            anchoredSides[i][0] = true;
            set_terminal_point(columns[nxt.columnIndex].rail_b, nxt.atEnd, *anchor);
            sync_terminal_rung(columns[nxt.columnIndex], nxt.atEnd);
            anchoredSides[(i + 1) % n][1] = true;
            warnings.push_back("jonction " + std::to_string(junctionId) +
                               " : ancre partagee entre colonnes " + std::to_string(cur.columnIndex) +
                               " et " + std::to_string(nxt.columnIndex));
        }
        // Plancher de largeur : ne touche que les branches dont le côté
        // NON ancré a naturellement collapsé sous `tip_min_width`.
        for (std::size_t i = 0; i < n; ++i) {
            const JunctionBranch& b = branches[i];
            SatinColumnGeometry& col = columns[b.columnIndex];
            const P2 a = terminal_point(col.rail_a, b.atEnd);
            const P2 bb = terminal_point(col.rail_b, b.atEnd);
            const double width = norm(a - bb);
            if (width >= tipMinWidth) {
                continue;
            }
            const auto flags = anchoredSides[i];
            if (flags[0] && flags[1]) {
                continue; // les deux côtés ancrés au même point : légitime (étoile symétrique).
            }
            const P2 tangent{std::cos(b.angle), std::sin(b.angle)};
            const P2 leftNormal{-tangent.y, tangent.x}; // vers rail A
            if (flags[0] && !flags[1]) {
                set_terminal_point(col.rail_b, b.atEnd, a - leftNormal * tipMinWidth);
            } else if (flags[1] && !flags[0]) {
                set_terminal_point(col.rail_a, b.atEnd, bb + leftNormal * tipMinWidth);
            } else {
                const P2 mid = (a + bb) * 0.5;
                set_terminal_point(col.rail_a, b.atEnd, mid + leftNormal * (tipMinWidth * 0.5));
                set_terminal_point(col.rail_b, b.atEnd, mid - leftNormal * (tipMinWidth * 0.5));
            }
            sync_terminal_rung(col, b.atEnd);
            warnings.push_back("jonction " + std::to_string(junctionId) + " : colonne " +
                               std::to_string(b.columnIndex) +
                               " largeur terminale plancher appliquee (repli quasi degenere)");
        }
    }
}

// Validation finale par jonction (point 5 de l'audit) : après résolution des
// ancres partagées, vérifie que chaque confluence forme un raccord cohérent
// plutôt que de laisser passer un éventail ou un trou triangulaire. Retourne
// un message de refus explicite au premier problème trouvé (chaîne vide si
// tout est cohérent).
std::string validate_junctions(const std::vector<SatinColumnGeometry>& columns,
                               const SkeletonGraph& graph, double anchorRadius) {
    constexpr double kTwoPi = 2.0 * std::numbers::pi;
    constexpr double kCoincidenceEpsilonUm = 5.0;
    const auto byJunction = collect_junction_branches(columns);
    for (const auto& [junctionId, branches] : byJunction) {
        if (junctionId >= graph.nodes.size()) {
            continue;
        }
        // 1) Toutes les arêtes du graphe incidentes à cette jonction ont bien
        // produit une branche (aucune ambiguïté d'un raccord amputé).
        std::size_t expectedEnds = 0;
        for (const auto& e : graph.edges) {
            if (e.from == junctionId) ++expectedEnds;
            if (e.to == junctionId) ++expectedEnds;
        }
        if (branches.size() != expectedEnds) {
            return "jonction " + std::to_string(junctionId) + " incomplete (" +
                   std::to_string(branches.size()) + "/" + std::to_string(expectedEnds) +
                   " branches disponibles)";
        }
        if (branches.size() < 2) {
            continue;
        }
        const auto& node = graph.nodes[junctionId];
        const P2 center{static_cast<double>(node.position.x.value),
                        static_cast<double>(node.position.y.value)};
        const std::size_t n = branches.size();
        for (std::size_t i = 0; i < n; ++i) {
            const JunctionBranch& cur = branches[i];
            const JunctionBranch& nxt = branches[(i + 1) % n];
            const auto& curCol = columns[cur.columnIndex];
            const auto& nxtCol = columns[nxt.columnIndex];
            const P2 curPoint = terminal_point(curCol.rail_a, cur.atEnd);
            const P2 nxtPoint = terminal_point(nxtCol.rail_b, nxt.atEnd);
            // 2/3) Ancre partagée exacte, ou repli légitime mais borné : les
            // deux côtés d'un espace angulaire sans encoche à portée doivent
            // rester proches du centre (jamais une pointe libre qui se serait
            // échappée loin de la confluence — signature d'un éventail).
            const double gap = norm(curPoint - nxtPoint);
            if (gap > kCoincidenceEpsilonUm) {
                const double curDist = norm(curPoint - center);
                const double nxtDist = norm(nxtPoint - center);
                if (curDist > anchorRadius * 1.5 || nxtDist > anchorRadius * 1.5) {
                    return "jonction " + std::to_string(junctionId) +
                           " : rails terminaux incoherents entre colonnes " +
                           std::to_string(cur.columnIndex) + " et " + std::to_string(nxt.columnIndex) +
                           " (ecart " + std::to_string(static_cast<long>(gap)) + " um)";
                }
            }
            // 4) Aucun croisement entre les barreaux terminaux des deux
            // branches adjacentes.
            if (!curCol.rungs.empty() && !nxtCol.rungs.empty()) {
                const auto& rc = cur.atEnd ? curCol.rungs.back() : curCol.rungs.front();
                const auto& rn = nxt.atEnd ? nxtCol.rungs.back() : nxtCol.rungs.front();
                const P2 ra{static_cast<double>(rc.a.x.value), static_cast<double>(rc.a.y.value)};
                const P2 rb{static_cast<double>(rc.b.x.value), static_cast<double>(rc.b.y.value)};
                const P2 na{static_cast<double>(rn.a.x.value), static_cast<double>(rn.a.y.value)};
                const P2 nb{static_cast<double>(rn.b.x.value), static_cast<double>(rn.b.y.value)};
                if (segments_cross(ra, rb, na, nb)) {
                    return "jonction " + std::to_string(junctionId) +
                           " : croisement entre barreaux terminaux des colonnes " +
                           std::to_string(cur.columnIndex) + " et " + std::to_string(nxt.columnIndex);
                }
            }
            // 5) Couverture locale : un secteur angulaire anormalement large
            // sans aucune ancre commune (repli des deux côtés) signale une
            // zone potentiellement non couverte autour du centre plutôt
            // qu'une vraie encoche absente (celles-ci restent étroites en
            // pratique, cf. jonction en T testée plus haut).
            double hi = nxt.angle;
            while (hi <= cur.angle) {
                hi += kTwoPi;
            }
            constexpr double kMaxUncoveredSectorRad = 2.6; // ~149 deg
            if (gap > kCoincidenceEpsilonUm && (hi - cur.angle) > kMaxUncoveredSectorRad) {
                return "jonction " + std::to_string(junctionId) +
                       " : secteur non couvert entre colonnes " + std::to_string(cur.columnIndex) +
                       " et " + std::to_string(nxt.columnIndex);
            }
        }
    }
    return {};
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

    // Résolution globale des ancres de jonction (point 3 de l'audit
    // « jonctions branchées/concaves ») puis validation par jonction
    // (point 5) : appelée une fois TOUTES les colonnes construites, quel que
    // soit le statut (`RequiresDecomposition` ou `Suitable` avec un bout
    // touchant tout de même une jonction). En cas d'incohérence détectée
    // (raccord ambigu, éventail, trou triangulaire...), refuse proprement la
    // génération plutôt que de renvoyer des colonnes mal raccordées.
    const auto resolve_and_validate_junctions = [&] {
        if (!params.anchor_junction_ends || r.columns.empty()) {
            return;
        }
        const double anchorRadius = static_cast<double>(params.junction_anchor_radius.value);
        const double tipMinWidth =
            static_cast<double>(std::max<std::int32_t>(1, params.tip_min_width.value));
        const std::vector<P2> reflexVertices =
            polys.empty() ? std::vector<P2>{} : reflex_vertices(polys.front());
        resolve_junction_anchors(r.columns, graph, reflexVertices, anchorRadius, tipMinWidth,
                                 r.warnings);
        const std::string problem = validate_junctions(r.columns, graph, anchorRadius);
        if (!problem.empty()) {
            r.warnings.push_back("refus : " + problem);
            r.columns.clear();
            r.refusal = "jonction incoherente : " + problem;
        }
    };

    const auto try_edge = [&](const SkeletonEdge& e) {
        const bool startIsJunction = graph.nodes[e.from].type == SkeletonNodeType::Junction;
        const bool endIsJunction = graph.nodes[e.to].type == SkeletonNodeType::Junction;
        // Un bout de jonction n'est jamais ÉTENDU (il doit rester exactement
        // au nœud du squelette) ; seul un bout OUVERT l'est. Un bout de
        // jonction est en revanche ampute (`trim_unstable_junction_tail`
        // dans `build_column`) puis ancré GLOBALEMENT par jonction, une fois
        // toutes les colonnes construites — cf.
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
