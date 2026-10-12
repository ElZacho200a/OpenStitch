// SPDX-License-Identifier: Apache-2.0
//
// Ajustement de courbes de Bézier d'après Ph. J. Schneider, « An Algorithm for
// Automatically Fitting Digitized Curves », Graphics Gems I, 1990, complété
// par une détection de coins par fenêtre d'arc. Écrit à partir de la
// publication.
#include "openstitch/geometry/curve_fit.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include "openstitch/geometry/polyline.hpp"

namespace openstitch::geometry {

namespace {

struct P {
    double x{0.0};
    double y{0.0};
};

P operator+(P a, P b) {
    return {a.x + b.x, a.y + b.y};
}
P operator-(P a, P b) {
    return {a.x - b.x, a.y - b.y};
}
P operator*(P a, double k) {
    return {a.x * k, a.y * k};
}
double dot(P a, P b) {
    return a.x * b.x + a.y * b.y;
}
double len(P a) {
    return std::sqrt(dot(a, a));
}
P unit(P a) {
    const double l = len(a);
    return l > 1e-12 ? P{a.x / l, a.y / l} : P{0.0, 0.0};
}

P to_p(Vec2um v) {
    return {static_cast<double>(v.x.value), static_cast<double>(v.y.value)};
}

struct Cubic {
    P c[4];
    bool line{false};
};

P bez_point(const P (&c)[4], double t) {
    const double s = 1.0 - t;
    return c[0] * (s * s * s) + c[1] * (3.0 * s * s * t) + c[2] * (3.0 * s * t * t) +
           c[3] * (t * t * t);
}
P bez_d1(const P (&c)[4], double t) {
    const double s = 1.0 - t;
    return (c[1] - c[0]) * (3.0 * s * s) + (c[2] - c[1]) * (6.0 * s * t) +
           (c[3] - c[2]) * (3.0 * t * t);
}
P bez_d2(const P (&c)[4], double t) {
    const double s = 1.0 - t;
    return (c[2] - c[1] * 2.0 + c[0]) * (6.0 * s) + (c[3] - c[2] * 2.0 + c[1]) * (6.0 * t);
}

double dist_point_segment(P p, P a, P b) {
    const P ab = b - a;
    const double l2 = dot(ab, ab);
    if (l2 <= 0.0) {
        return len(p - a);
    }
    const double t = std::clamp(dot(p - a, ab) / l2, 0.0, 1.0);
    return len(p - (a + ab * t));
}

// Section ouverte de points (indices consécutifs d'un tableau).
using Pts = std::vector<P>;

// Écart géométrique réel dans les deux sens ; renvoie aussi l'indice du point
// d'entrée le plus éloigné de la courbe (pour la découpe).
struct ErrorInfo {
    double max_err{0.0};
    std::size_t worst{0};
};

ErrorInfo geometric_error(const Cubic& cb, const Pts& pts, std::size_t first, std::size_t last) {
    constexpr int kSamples = 32;
    P flat[kSamples + 1];
    for (int i = 0; i <= kSamples; ++i) {
        flat[i] = bez_point(cb.c, static_cast<double>(i) / kSamples);
    }
    ErrorInfo info;
    for (std::size_t i = first; i <= last; ++i) {
        double best = std::numeric_limits<double>::max();
        for (int k = 0; k < kSamples; ++k) {
            best = std::min(best, dist_point_segment(pts[i], flat[k], flat[k + 1]));
        }
        if (best > info.max_err) {
            info.max_err = best;
            info.worst = i;
        }
    }
    // Sens inverse : la courbe ne doit pas s'écarter des données entre deux
    // points (entrée creuse).
    for (int k = 0; k <= kSamples; ++k) {
        double best = std::numeric_limits<double>::max();
        for (std::size_t i = first; i < last; ++i) {
            best = std::min(best, dist_point_segment(flat[k], pts[i], pts[i + 1]));
        }
        info.max_err = std::max(info.max_err, best);
    }
    return info;
}

std::vector<double> chord_params(const Pts& pts, std::size_t first, std::size_t last) {
    std::vector<double> u(last - first + 1, 0.0);
    for (std::size_t i = first + 1; i <= last; ++i) {
        u[i - first] = u[i - first - 1] + len(pts[i] - pts[i - 1]);
    }
    const double total = u.back();
    if (total > 0.0) {
        for (double& v : u) {
            v /= total;
        }
    }
    return u;
}

Cubic generate_bezier(const Pts& pts, std::size_t first, std::size_t last,
                      const std::vector<double>& u, P t1, P t2) {
    const P p0 = pts[first];
    const P p3 = pts[last];
    const std::size_t n = last - first + 1;
    double c00 = 0.0, c01 = 0.0, c11 = 0.0, x0 = 0.0, x1 = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double t = u[i];
        const double s = 1.0 - t;
        const double b0 = s * s * s;
        const double b1 = 3.0 * s * s * t;
        const double b2 = 3.0 * s * t * t;
        const double b3 = t * t * t;
        const P a1 = t1 * b1;
        const P a2 = t2 * b2;
        c00 += dot(a1, a1);
        c01 += dot(a1, a2);
        c11 += dot(a2, a2);
        const P tmp = pts[first + i] - (p0 * (b0 + b1) + p3 * (b2 + b3));
        x0 += dot(a1, tmp);
        x1 += dot(a2, tmp);
    }
    const double det = c00 * c11 - c01 * c01;
    const double seg = len(p3 - p0);
    double a1 = 0.0;
    double a2 = 0.0;
    if (std::fabs(det) > 1e-12) {
        a1 = (x0 * c11 - x1 * c01) / det;
        a2 = (c00 * x1 - c01 * x0) / det;
    }
    const double eps = 1e-6 * seg;
    if (std::fabs(det) <= 1e-12 || a1 < eps || a2 < eps) {
        a1 = a2 = seg / 3.0; // heuristique de Wu/Barsky
    }
    Cubic cb;
    cb.c[0] = p0;
    cb.c[1] = p0 + t1 * a1;
    cb.c[2] = p3 + t2 * a2;
    cb.c[3] = p3;
    return cb;
}

void reparameterize(const Cubic& cb, const Pts& pts, std::size_t first, std::vector<double>& u) {
    for (std::size_t i = 0; i < u.size(); ++i) {
        const double t = u[i];
        const P d = bez_point(cb.c, t) - pts[first + i];
        const P d1 = bez_d1(cb.c, t);
        const P d2 = bez_d2(cb.c, t);
        const double num = dot(d, d1);
        const double den = dot(d1, d1) + dot(d, d2);
        if (std::fabs(den) > 1e-12) {
            u[i] = std::clamp(t - num / den, 0.0, 1.0);
        }
    }
}

// Tangente unitaire en `idx` mesurée sur une fenêtre d'arc `w` du côté
// `side` (+1 : vers l'avant, -1 : vers l'arrière, 0 : centrée), à l'intérieur
// de [first,last].
P tangent_at(const Pts& pts, std::size_t first, std::size_t last, std::size_t idx, int side,
             double w) {
    const auto walk = [&](int dir) {
        double acc = 0.0;
        std::size_t j = idx;
        while (true) {
            const bool can = dir > 0 ? j < last : j > first;
            if (!can) {
                break;
            }
            const std::size_t nj = dir > 0 ? j + 1 : j - 1;
            acc += len(pts[nj] - pts[j]);
            j = nj;
            if (acc >= w) {
                break;
            }
        }
        return j;
    };
    if (side > 0) {
        return unit(pts[walk(1)] - pts[idx]);
    }
    if (side < 0) {
        return unit(pts[walk(-1)] - pts[idx]);
    }
    return unit(pts[walk(1)] - pts[walk(-1)]);
}

struct Fitter {
    const Pts& pts;
    double tol;
    double window;
    std::vector<Cubic>& out;

    void fit(std::size_t first, std::size_t last, P t1, P t2, int depth) {
        const std::size_t n = last - first + 1;
        const P p0 = pts[first];
        const P p3 = pts[last];
        // 1) Segment droit ?
        if (len(p3 - p0) > tol) {
            Cubic line;
            line.line = true;
            line.c[0] = p0;
            line.c[1] = p0 + (p3 - p0) * (1.0 / 3.0);
            line.c[2] = p0 + (p3 - p0) * (2.0 / 3.0);
            line.c[3] = p3;
            if (geometric_error(line, pts, first, last).max_err <= tol) {
                out.push_back(line);
                return;
            }
        }
        if (n == 2) {
            Cubic line;
            line.line = true;
            line.c[0] = p0;
            line.c[1] = p0;
            line.c[2] = p3;
            line.c[3] = p3;
            out.push_back(line);
            return;
        }
        // 2) Courbe de Schneider.
        std::vector<double> u = chord_params(pts, first, last);
        Cubic best = generate_bezier(pts, first, last, u, t1, t2);
        ErrorInfo info = geometric_error(best, pts, first, last);
        for (int it = 0; it < 4 && info.max_err > tol; ++it) {
            reparameterize(best, pts, first, u);
            const Cubic cand = generate_bezier(pts, first, last, u, t1, t2);
            const ErrorInfo ci = geometric_error(cand, pts, first, last);
            if (ci.max_err < info.max_err) {
                best = cand;
                info = ci;
            } else {
                break;
            }
        }
        if (info.max_err <= tol || depth >= 40 || n <= 3) {
            out.push_back(best);
            return;
        }
        // 3) Découpe au point d'écart maximal (jamais aux extrémités).
        std::size_t split = std::clamp(info.worst, first + 1, last - 1);
        const P tc = tangent_at(pts, first, last, split, 0, window);
        P tcSafe = tc;
        if (tcSafe.x == 0.0 && tcSafe.y == 0.0) {
            tcSafe = unit(pts[split + 1] - pts[split - 1]);
        }
        fit(first, split, t1, tcSafe * -1.0, depth + 1);
        fit(split, last, tcSafe, t2, depth + 1);
    }
};

Vec2um to_um(P p) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(p.x))},
                  Micrometers{static_cast<std::int32_t>(std::lround(p.y))}};
}

} // namespace

Path fit_curves(const Path& polygon, const CurveFitOptions& options) {
    if (options.tolerance.value <= 0 || polygon.nodes.size() < 4) {
        return polygon;
    }
    // Points sans doublons consécutifs.
    Pts pts;
    pts.reserve(polygon.nodes.size());
    for (const PathNode& nd : polygon.nodes) {
        const P p = to_p(nd.pos);
        if (pts.empty() || len(p - pts.back()) > 0.0) {
            pts.push_back(p);
        }
    }
    if (polygon.closed && pts.size() > 1 && len(pts.front() - pts.back()) == 0.0) {
        pts.pop_back();
    }
    const std::size_t n = pts.size();
    if (n < 4) {
        return polygon;
    }

    const double tol = static_cast<double>(options.tolerance.value);
    const bool closed = polygon.closed;

    // Longueurs cumulées (chemin fermé : une boucle de plus).
    std::vector<double> cum(n + 1, 0.0);
    for (std::size_t i = 1; i <= n; ++i) {
        cum[i] = cum[i - 1] + len(pts[i % n] - pts[i - 1]);
    }
    const double perimeter = cum[n];
    const double window = std::max(
        tol, std::min(options.window_in_tolerances * tol, closed ? perimeter / 24.0 : perimeter));

    // --- Coins ----------------------------------------------------------------
    const auto at_arc = [&](double s) {
        if (closed) {
            s = std::fmod(s, perimeter);
            if (s < 0.0) {
                s += perimeter;
            }
        } else {
            s = std::clamp(s, 0.0, perimeter);
        }
        const auto it = std::upper_bound(cum.begin(), cum.end(), s);
        std::size_t i = static_cast<std::size_t>(it - cum.begin());
        i = std::clamp<std::size_t>(i, 1, n);
        const double seg = cum[i] - cum[i - 1];
        const double t = seg > 0.0 ? (s - cum[i - 1]) / seg : 0.0;
        const P a = pts[i - 1];
        const P b = pts[i % n];
        return a + (b - a) * t;
    };
    std::vector<double> turn(n, 0.0);
    constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;
    for (std::size_t i = 0; i < n; ++i) {
        if (!closed && (i == 0 || i == n - 1)) {
            continue;
        }
        const P before = pts[i] - at_arc(cum[i] - window);
        const P after = at_arc(cum[i] + window) - pts[i];
        const double lb = len(before);
        const double la = len(after);
        if (lb <= 0.0 || la <= 0.0) {
            continue;
        }
        const double c = std::clamp(dot(before, after) / (lb * la), -1.0, 1.0);
        turn[i] = std::acos(c) * kRadToDeg;
    }
    std::vector<std::size_t> corners;
    for (std::size_t i = 0; i < n; ++i) {
        if (turn[i] < options.corner_angle_deg) {
            continue;
        }
        bool is_max = true;
        for (std::size_t j = 0; j < n && is_max; ++j) {
            if (j == i || turn[j] < options.corner_angle_deg) {
                continue;
            }
            double d = std::fabs(cum[j] - cum[i]);
            if (closed) {
                d = std::min(d, perimeter - d);
            }
            if (d <= window && (turn[j] > turn[i] || (turn[j] == turn[i] && j < i))) {
                is_max = false;
            }
        }
        if (is_max) {
            corners.push_back(i);
        }
    }

    // --- Sections -------------------------------------------------------------
    // Une section = (premier index, dernier index) sur le tableau dupliqué,
    // `wrap` ajoute n aux indices au-delà de n-1.
    struct Section {
        std::size_t a;
        std::size_t b; // b >= a ; en fermé, peut dépasser n-1 (indice modulo n)
        bool start_corner;
        bool end_corner;
    };
    std::vector<Section> sections;
    if (closed) {
        if (corners.empty()) {
            sections.push_back({0, n, false, false});
        } else {
            for (std::size_t k = 0; k < corners.size(); ++k) {
                const std::size_t a = corners[k];
                std::size_t b = corners[(k + 1) % corners.size()];
                if (b <= a) {
                    b += n;
                }
                sections.push_back({a, b, true, true});
            }
        }
    } else {
        std::size_t a = 0;
        for (const std::size_t c : corners) {
            sections.push_back({a, c, a != 0, true});
            a = c;
        }
        sections.push_back({a, n - 1, a != 0, false});
    }

    struct SecResult {
        std::vector<Cubic> cubics;
        bool start_corner;
        bool end_corner;
    };
    std::vector<SecResult> results;
    results.reserve(sections.size());
    for (const Section& s : sections) {
        Pts sec;
        sec.reserve(s.b - s.a + 1);
        for (std::size_t i = s.a; i <= s.b; ++i) {
            sec.push_back(pts[i % n]);
        }
        SecResult r;
        r.start_corner = s.start_corner;
        r.end_corner = s.end_corner;
        const std::size_t last = sec.size() - 1;
        Fitter fitter{sec, tol, window, r.cubics};
        P t1;
        P t2;
        if (closed && !s.start_corner) {
            // Boucle lisse : tangente centrée à travers la jonction.
            const P tc = unit(pts[1 % n] - pts[n - 1]);
            t1 = tc;
            t2 = tc * -1.0;
        } else {
            t1 = tangent_at(sec, 0, last, 0, 1, window);
            t2 = tangent_at(sec, 0, last, last, -1, window);
        }
        if ((t1.x == 0.0 && t1.y == 0.0) || (t2.x == 0.0 && t2.y == 0.0)) {
            t1 = unit(sec[last] - sec[0]);
            t2 = t1 * -1.0;
        }
        fitter.fit(0, last, t1, t2, 0);
        results.push_back(std::move(r));
    }

    // --- Assemblage des nœuds ---------------------------------------------------
    std::vector<Cubic> all;
    std::vector<bool> corner_before; // le nœud de départ de la courbe i est-il un coin ?
    for (const SecResult& r : results) {
        for (std::size_t k = 0; k < r.cubics.size(); ++k) {
            all.push_back(r.cubics[k]);
            corner_before.push_back(k == 0 && (r.start_corner || !closed));
        }
    }
    Path out;
    out.closed = closed;
    const std::size_t m = all.size();
    const std::size_t node_count = closed ? m : m + 1;
    out.nodes.resize(node_count);
    for (std::size_t i = 0; i < m; ++i) {
        PathNode& a = out.nodes[i];
        a.pos = to_um(all[i].c[0]);
        if (!all[i].line) {
            a.tan_out = to_um(all[i].c[1] - all[i].c[0]);
        }
        PathNode& b = out.nodes[(i + 1) % node_count];
        b.pos = to_um(all[i].c[3]);
        if (!all[i].line) {
            b.tan_in = to_um(all[i].c[2] - all[i].c[3]);
        }
    }
    for (std::size_t i = 0; i < node_count; ++i) {
        PathNode& nd = out.nodes[i];
        const bool smooth = nd.tan_in && nd.tan_out && !(i < m && corner_before[i]);
        nd.type = smooth ? NodeType::Smooth : NodeType::Corner;
    }
    return out;
}

double hausdorff_distance_um(const Path& a, const Path& b) {
    const Polyline fa = flatten(a, Micrometers{2});
    const Polyline fb = flatten(b, Micrometers{2});
    const auto directed = [](const Polyline& from, const Polyline& to) {
        double worst = 0.0;
        const std::size_t nt = to.points.size();
        for (const Vec2um& v : from.points) {
            const P p = to_p(v);
            double best = std::numeric_limits<double>::max();
            const std::size_t segs = to.closed ? nt : (nt > 0 ? nt - 1 : 0);
            for (std::size_t i = 0; i < segs; ++i) {
                best = std::min(
                    best, dist_point_segment(p, to_p(to.points[i]), to_p(to.points[(i + 1) % nt])));
            }
            if (segs == 0 && nt == 1) {
                best = len(p - to_p(to.points[0]));
            }
            worst = std::max(worst, best);
        }
        return worst;
    };
    return std::max(directed(fa, fb), directed(fb, fa));
}

} // namespace openstitch::geometry
