// SPDX-License-Identifier: Apache-2.0
#include "openstitch/auto_satin/satin_column.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace openstitch::auto_satin {

namespace {

// Travail en µm double, repère modèle (Y vers le haut).
struct P2 {
    double x{0.0};
    double y{0.0};
};

P2 operator+(P2 a, P2 b) { return {a.x + b.x, a.y + b.y}; }
P2 operator-(P2 a, P2 b) { return {a.x - b.x, a.y - b.y}; }
P2 operator*(P2 a, double s) { return {a.x * s, a.y * s}; }
double dot(P2 a, P2 b) { return a.x * b.x + a.y * b.y; }
double cross(P2 a, P2 b) { return a.x * b.y - a.y * b.x; }
double norm(P2 a) { return std::sqrt(dot(a, a)); }
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
            poly.push_back({static_cast<double>(n.pos.x.value), static_cast<double>(n.pos.y.value)});
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
        if (((a.y > p.y) != (b.y > p.y)) &&
            (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)) {
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

// Section transversale : intersecte la droite (A, direction N unitaire) avec le
// contour de la région et retourne l'intervalle intérieur encadrant A :
// t_lo < 0 < t_hi (extrémités du côté -N et +N). Renvoie nullopt si A n'est pas
// strictement intérieur, si l'intervalle n'encadre pas A, ou s'il est plus large
// que `max_width` (normale quasi parallèle au bord -> section aberrante).
std::optional<std::pair<double, double>> cross_section(const std::vector<Poly>& polys, P2 A, P2 N,
                                                       double max_width) {
    std::vector<double> ts;
    for (const auto& poly : polys) {
        const std::size_t n = poly.size();
        for (std::size_t i = 0; i < n; ++i) {
            const P2 p = poly[i];
            const P2 q = poly[(i + 1) % n];
            const P2 d = q - p;
            const double det = -N.x * d.y + d.x * N.y;
            if (std::abs(det) < 1e-9) {
                continue;  // parallèle
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
    if (!std::isfinite(lo) || !std::isfinite(hi)) {
        return std::nullopt;
    }
    if (hi - lo > max_width) {
        return std::nullopt;
    }
    if (!in_region(polys, A + N * ((lo + hi) * 0.5))) {
        return std::nullopt;
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

Poly open_at_rightmost(Poly poly, double seamY) {
    if (poly.empty()) return {};
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
        const auto vertex = std::max_element(poly.begin(), poly.end(), [](P2 a, P2 b) {
            return a.x < b.x;
        });
        seamEdge = static_cast<std::size_t>(vertex - poly.begin());
        seam = *vertex;
    }
    Poly open;
    open.reserve(poly.size() + 2);
    open.push_back(seam);
    for (std::size_t i = 1; i <= poly.size(); ++i) {
        const P2 point = poly[(seamEdge + i) % poly.size()];
        if (norm(point - open.back()) > 1e-9) open.push_back(point);
    }
    if (norm(seam - open.back()) > 1e-9) open.push_back(seam);
    return open;
}

double polyline_length(const Poly& points) {
    double length = 0.0;
    for (std::size_t i = 1; i < points.size(); ++i) length += norm(points[i] - points[i - 1]);
    return length;
}

// Un anneau est un satin ferme, donc un petit reseau cyclique et non une
// colonne ouverte. On ouvre deterministement les deux contours au point le
// plus a droite, les oriente dans le meme sens, puis les decompose en quatre
// sections ouvertes raccordees bout a bout. Les validations ci-dessous
// interdisent de recreer une gerbe ou un noeud papillon sur un anneau trop
// irregulier pour cet appariement automatique.
std::optional<std::vector<SatinColumnGeometry>> build_annular_sections(
    const geometry::PathSet& region, const SatinColumnsParameters& params, std::string& refusal) {
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
    if (outerArea * innerArea < 0.0) std::reverse(inner.begin(), inner.end());
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
    int intervals = std::max(4, static_cast<int>(std::ceil(std::max(outerLength, innerLength) /
                                                           stationSpacing)));
    intervals = ((intervals + 3) / 4) * 4;
    outer = resample_arc(outer, outerLength / intervals);
    inner = resample_arc(inner, innerLength / intervals);
    if (outer.size() != static_cast<std::size_t>(intervals + 1) || outer.size() != inner.size()) {
        refusal = "reechantillonnage incomplet";
        return std::nullopt;
    }

    const double minWidth =
        static_cast<double>(params.analysis.thresholds.min_satin_width.value);
    const double maxWidth =
        static_cast<double>(params.analysis.thresholds.max_satin_width.value);
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
        if (i > 0 &&
            (segments_cross(outer[i - 1], inner[i - 1], outer[i], inner[i]) ||
             segments_cross(outer[i - 1], outer[i], inner[i - 1], inner[i]))) {
            refusal = "croisement local a la station " + std::to_string(i);
            return std::nullopt;
        }
    }
    for (std::size_t i = 0; i + 2 < outer.size(); ++i) {
        for (std::size_t j = i + 2; j < outer.size(); ++j) {
            if (i == 0 && j + 1 == outer.size()) continue;
            if (segments_cross(outer[i], inner[i], outer[j], inner[j])) {
                refusal = "croisement global des barreaux " + std::to_string(i) + "/" +
                          std::to_string(j);
                return std::nullopt;
            }
        }
    }

    std::vector<SatinColumnGeometry> columns;
    columns.reserve(4);
    const int perSection = intervals / 4;
    const double actualSpacing = std::max(outerLength, innerLength) / intervals;
    const int rungStride = std::max(
        1, static_cast<int>(std::lround(params.rung_max_spacing.value / actualSpacing)));
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
    P2 railA;  // côté +N (gauche)
    P2 railB;  // côté -N (droite)
    P2 tangent;
    double width{0.0};
};

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
            break;  // ne rétrécit plus : on a dépassé la pointe (forme non convexe)
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
            return ext;  // déjà assez fin : la fermeture par bissection n'apporterait rien
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
    if (norm(tip - prevPoint) > 1.0) {  // évite un point quasi dupliqué (< 1 µm)
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

// Construit une colonne depuis une centerline (axe) et les polygones de région.
// `extendStart`/`extendEnd` : true si ce bout est OUVERT (pas de jonction) et
// doit donc être étendu jusqu'au bord réel — cf. `extend_tip`.
std::optional<SatinColumnGeometry> build_column(const std::vector<Vec2um>& centerline,
                                                const std::vector<Poly>& polys,
                                                const SatinColumnsParameters& params,
                                                bool extendStart, bool extendEnd) {
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
    std::vector<Station> raw;
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
        const P2 nrm{-tan.y, tan.x};  // +90° : +N = gauche
        const auto sec = cross_section(polys, axis[i], nrm, maxWidth);
        if (!sec) {
            continue;
        }
        Station st;
        st.axis = axis[i];
        st.tangent = tan;
        st.railA = axis[i] + nrm * sec->second;  // t_hi > 0
        st.railB = axis[i] + nrm * sec->first;   // t_lo < 0
        st.width = sec->second - sec->first;
        raw.push_back(st);
    }
    if (raw.size() < 2) {
        return std::nullopt;
    }

    // Nettoyage anti-croisement : on retire une station dont le quadrilatère avec
    // la précédente s'auto-intersecte (rails ou barreaux croisés).
    std::vector<Station> st;
    st.push_back(raw.front());
    for (std::size_t i = 1; i < raw.size(); ++i) {
        const Station& prev = st.back();
        const Station& cur = raw[i];
        const bool crosses = segments_cross(prev.railA, cur.railA, prev.railB, cur.railB) ||
                             segments_cross(prev.railA, prev.railB, cur.railA, cur.railB);
        if (!crosses) {
            st.push_back(cur);
        }
    }
    if (st.size() < 2) {
        return std::nullopt;
    }

    // Étend les bouts OUVERTS (sans jonction) jusqu'au bord réel de la région
    // (embout arrondi/pointu) — cf. `extend_tip`. Un bout de jonction reste
    // inchangé : il doit rester exactement au nœud du squelette pour la
    // reconciliation multi-sections (guides liés, routage).
    if (params.extend_open_ends) {
        const double stepLen = static_cast<double>(params.station_spacing.value);
        const double tipMin =
            static_cast<double>(std::max<std::int32_t>(1, params.tip_min_width.value));
        if (extendEnd) {
            const Station& last = st.back();
            auto tail = extend_tip(polys, last.axis, last.tangent, last.tangent, last.width,
                                   maxWidth, stepLen, tipMin);
            for (auto& s : tail) {
                st.push_back(std::move(s));
            }
        }
        if (extendStart) {
            const Station& first = st.front();
            auto head = extend_tip(polys, first.axis, first.tangent * -1.0, first.tangent,
                                   first.width, maxWidth, stepLen, tipMin);
            std::reverse(head.begin(), head.end());
            st.insert(st.begin(), head.begin(), head.end());
        }
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
    return col;
}

}  // namespace

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

    const auto try_edge = [&](const SkeletonEdge& e) {
        const bool startIsJunction = graph.nodes[e.from].type == SkeletonNodeType::Junction;
        const bool endIsJunction = graph.nodes[e.to].type == SkeletonNodeType::Junction;
        // Un bout de jonction ne doit jamais être étendu (il doit rester
        // exactement au nœud du squelette) ; seul un bout OUVERT l'est.
        if (auto col = build_column(e.centerline, polys, params, !startIsJunction, !endIsJunction)) {
            if (startIsJunction) {
                col->start_junction = e.from;
            }
            if (endIsJunction) {
                col->end_junction = e.to;
            }
            r.columns.push_back(std::move(*col));
        } else {
            r.warnings.push_back("branche ignorée (section transversale instable)");
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
        // Une colonne par branche menant à une extrémité (bras du Y/T).
        for (const auto& e : graph.edges) {
            const bool armFrom = graph.nodes[e.from].type == SkeletonNodeType::Endpoint;
            const bool armTo = graph.nodes[e.to].type == SkeletonNodeType::Endpoint;
            if (armFrom || armTo) {
                try_edge(e);
            }
        }
        if (r.columns.empty()) {
            r.refusal = "aucune branche exploitable";
        }
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
        finalize_sections();
        return r;
    }
    }
    return r;
}

}  // namespace openstitch::auto_satin
