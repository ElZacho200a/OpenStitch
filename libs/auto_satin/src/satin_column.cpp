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

struct Station {
    P2 axis;
    P2 railA;  // côté +N (gauche)
    P2 railB;  // côté -N (droite)
    P2 tangent;
    double width{0.0};
};

// Construit une colonne depuis une centerline (axe) et les polygones de région.
std::optional<SatinColumnGeometry> build_column(const std::vector<Vec2um>& centerline,
                                                const std::vector<Poly>& polys,
                                                const SatinColumnsParameters& params) {
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

    const auto& graph = analysis->debug.graph;
    const std::vector<Poly> polys = region_polys(region);

    const auto try_edge = [&](const SkeletonEdge& e) {
        if (auto col = build_column(e.centerline, polys, params)) {
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
        return r;
    }
    }
    return r;
}

}  // namespace openstitch::auto_satin
