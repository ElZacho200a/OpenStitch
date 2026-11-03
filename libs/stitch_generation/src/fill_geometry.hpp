// SPDX-License-Identifier: Apache-2.0
// Outils internes partagés par les remplissages radial et en spirale (HP-STI-008/009) :
// appartenance d'un point à une région (extérieur moins trous, règle pair-impair), intervalles
// d'un segment situés dans la région, découpe d'un trajet en points. Interne à la bibliothèque.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "openstitch/geometry/path.hpp"
#include "openstitch/stitch_generation/tatami.hpp"

namespace openstitch::stitch_generation::detail {

struct P2 {
    double x{0};
    double y{0};
};

[[nodiscard]] inline Vec2um to_um(P2 p) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(p.x))},
                  Micrometers{static_cast<std::int32_t>(std::lround(p.y))}};
}

[[nodiscard]] inline P2 lerp(P2 a, P2 b, double t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

template <class F> void for_each_edge(const geometry::PathSet& region, F&& f) {
    const auto ring = [&](const geometry::Path& path) {
        const std::size_t n = path.nodes.size();
        for (std::size_t i = 0; i < n; ++i) {
            const auto& a = path.nodes[i].pos;
            const auto& b = path.nodes[(i + 1) % n].pos;
            f(P2{double(a.x.value), double(a.y.value)}, P2{double(b.x.value), double(b.y.value)});
        }
    };
    ring(region.outer);
    for (const geometry::Path& h : region.holes) {
        ring(h);
    }
}

// Point dans la région (extérieur moins trous), règle pair-impair sur toutes les arêtes.
[[nodiscard]] inline bool point_in_region(const geometry::PathSet& region, P2 p) {
    bool in = false;
    for_each_edge(region, [&](P2 a, P2 b) {
        if ((a.y > p.y) != (b.y > p.y)) {
            const double x = a.x + (p.y - a.y) / (b.y - a.y) * (b.x - a.x);
            if (p.x < x) {
                in = !in;
            }
        }
    });
    return in;
}

// Intervalles [t0 ; t1] ⊂ [0 ; 1] du segment [a ; b] situés dans la région. Les intervalles
// contigus sont fusionnés ; ordre croissant de t.
[[nodiscard]] inline std::vector<std::pair<double, double>>
inside_intervals(const geometry::PathSet& region, P2 a, P2 b) {
    std::vector<double> ts{0.0, 1.0};
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    for_each_edge(region, [&](P2 c, P2 d) {
        const double ex = d.x - c.x;
        const double ey = d.y - c.y;
        const double den = dx * ey - dy * ex;
        if (den == 0.0) {
            return;
        }
        const double t = ((c.x - a.x) * ey - (c.y - a.y) * ex) / den;
        const double u = ((c.x - a.x) * dy - (c.y - a.y) * dx) / den;
        if (t > 0.0 && t < 1.0 && u >= 0.0 && u <= 1.0) {
            ts.push_back(t);
        }
    });
    std::sort(ts.begin(), ts.end());
    std::vector<std::pair<double, double>> out;
    for (std::size_t i = 0; i + 1 < ts.size(); ++i) {
        if (ts[i + 1] - ts[i] < 1e-12) {
            continue;
        }
        if (point_in_region(region, lerp(a, b, 0.5 * (ts[i] + ts[i + 1])))) {
            if (!out.empty() && out.back().second >= ts[i] - 1e-12) {
                out.back().second = ts[i + 1];
            } else {
                out.emplace_back(ts[i], ts[i + 1]);
            }
        }
    }
    return out;
}

// Nombre de pas égaux d'un trajet de longueur `len` : au plus `max_len` par pas ; au moins
// `min_len` si c'est compatible avec le maximum.
[[nodiscard]] inline int step_count(double len, double max_len, double min_len) {
    int count = std::max(1, static_cast<int>(std::ceil(len / max_len - 1e-9)));
    if (count > 1 && len / count < min_len) {
        const int fewer = std::max(1, static_cast<int>(std::floor(len / min_len)));
        if (len / fewer <= max_len) {
            count = fewer;
        }
    }
    return count;
}

// Accumulateur de points d'un remplissage : `line_to` ajoute un point cousu, `start_at` rejoint
// un nouveau départ (liaison cousue subdivisée en pas <= max si elle reste dans la région et
// n'excède pas `kMaxLinkSteps` pas, sinon saut, que le linker de génération peut cacher).
class FillEmitter {
public:
    static constexpr double kMaxLinkSteps = 20.0;

    FillEmitter(const geometry::PathSet& region, double max_len, double min_len)
        : tester_(region), max_len_(max_len), min_len_(min_len) {}

    void start_at(P2 p) {
        const Vec2um q = to_um(p);
        if (out_.empty()) {
            out_.push_back({q, false, false});
            return;
        }
        const Vec2um last = out_.back().pos;
        if (last == q) {
            return;
        }
        const double d = dist(last, q);
        if (d <= kMaxLinkSteps * max_len_ && tester_.stays_inside(last, q)) {
            walk(last, q, d);
        } else {
            out_.push_back({q, true, false});
        }
    }

    // Segment cousu de la dernière position à `p`, subdivisé (<= max, >= min si possible).
    void line_to(P2 p) {
        const Vec2um q = to_um(p);
        if (out_.empty()) {
            out_.push_back({q, false, false});
            return;
        }
        const Vec2um last = out_.back().pos;
        if (last == q) {
            return;
        }
        walk(last, q, dist(last, q));
    }

    [[nodiscard]] bool empty() const { return out_.empty(); }
    [[nodiscard]] std::vector<FillStitch> take() { return std::move(out_); }

private:
    [[nodiscard]] static double dist(Vec2um a, Vec2um b) {
        return std::hypot(double(b.x.value) - double(a.x.value),
                          double(b.y.value) - double(a.y.value));
    }

    void walk(Vec2um from, Vec2um to, double d) {
        const int steps = step_count(d, max_len_, min_len_);
        for (int s = 1; s < steps; ++s) {
            const double f = double(s) / double(steps);
            const Vec2um m =
                to_um({double(from.x.value) + (double(to.x.value) - double(from.x.value)) * f,
                       double(from.y.value) + (double(to.y.value) - double(from.y.value)) * f});
            if (m != out_.back().pos) {
                out_.push_back({m, false, false});
            }
        }
        out_.push_back({to, false, false});
    }

    RegionSegmentTester tester_;
    double max_len_;
    double min_len_;
    std::vector<FillStitch> out_;
};

// Centre par défaut : milieu de la boîte englobante du contour extérieur ; rayon : distance
// maximale du centre aux sommets du contour.
struct Polar {
    P2 center;
    double radius{0};
};

[[nodiscard]] inline Polar polar_frame(const geometry::PathSet& region,
                                       const std::optional<Vec2um>& center) {
    Polar f;
    if (region.outer.nodes.empty()) {
        return f;
    }
    if (center) {
        f.center = {double(center->x.value), double(center->y.value)};
    } else {
        double x0 = 1e18;
        double x1 = -1e18;
        double y0 = 1e18;
        double y1 = -1e18;
        for (const auto& n : region.outer.nodes) {
            x0 = std::min(x0, double(n.pos.x.value));
            x1 = std::max(x1, double(n.pos.x.value));
            y0 = std::min(y0, double(n.pos.y.value));
            y1 = std::max(y1, double(n.pos.y.value));
        }
        f.center = {std::round(0.5 * (x0 + x1)), std::round(0.5 * (y0 + y1))};
    }
    for (const auto& n : region.outer.nodes) {
        f.radius = std::max(f.radius, std::hypot(double(n.pos.x.value) - f.center.x,
                                                 double(n.pos.y.value) - f.center.y));
    }
    f.radius += 1.0;
    return f;
}

} // namespace openstitch::stitch_generation::detail
