// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/contour_fill.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <optional>
#include <utility>

#include "openstitch/geometry/offset.hpp"

namespace openstitch::stitch_generation {

namespace {

constexpr int kMaxLevels = 1'500;             // garde-fou : anneaux successifs d'une région
constexpr double kSharpCos = 0.9063077870366; // cos(25°) : au-delà, le sommet est un coin forcé

struct Pt {
    double x{0};
    double y{0};
};

Pt operator+(Pt a, Pt b) {
    return {a.x + b.x, a.y + b.y};
}
Pt operator-(Pt a, Pt b) {
    return {a.x - b.x, a.y - b.y};
}
Pt operator*(Pt a, double k) {
    return {a.x * k, a.y * k};
}
double dot(Pt a, Pt b) {
    return a.x * b.x + a.y * b.y;
}
double norm(Pt a) {
    return std::hypot(a.x, a.y);
}

// Anneau fermé (le dernier sommet se reboucle sur le premier), orienté CCW, avec abscisses
// curvilignes cumulées. `v[0]` est le point de départ de la couture.
struct Ring {
    std::vector<Pt> v;
    std::vector<double> cum; // cum[i] = longueur de v[0] à v[i]
    double length{0};

    [[nodiscard]] bool valid() const { return v.size() >= 3 && length > 0.0; }
};

Ring finish_ring(std::vector<Pt> pts) {
    // Doublons consécutifs (et bouclage) supprimés.
    std::vector<Pt> v;
    v.reserve(pts.size());
    for (const Pt& p : pts) {
        if (v.empty() || norm(p - v.back()) > 0.5) {
            v.push_back(p);
        }
    }
    while (v.size() > 1 && norm(v.back() - v.front()) <= 0.5) {
        v.pop_back();
    }
    Ring r;
    r.v = std::move(v);
    r.cum.assign(r.v.size(), 0.0);
    double s = 0.0;
    for (std::size_t i = 0; i < r.v.size(); ++i) {
        r.cum[i] = s;
        s += norm(r.v[(i + 1) % r.v.size()] - r.v[i]);
    }
    r.length = s;
    return r;
}

// Anneau CCW d'un chemin fermé (nœuds déjà aplatis). Invalide si dégénéré.
Ring make_ring(const geometry::Path& path) {
    std::vector<Pt> pts;
    pts.reserve(path.nodes.size());
    for (const auto& n : path.nodes) {
        pts.push_back({double(n.pos.x.value), double(n.pos.y.value)});
    }
    double area2 = 0.0;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const Pt& a = pts[i];
        const Pt& b = pts[(i + 1) % pts.size()];
        area2 += a.x * b.y - b.x * a.y;
    }
    if (area2 < 0.0) {
        std::reverse(pts.begin(), pts.end());
    }
    return finish_ring(std::move(pts));
}

// Même anneau, redémarré au point de la boucle le plus proche de `p` (point projeté sur une
// arête, inséré comme sommet). Égalités : première arête.
Ring rotate_to(const Ring& r, Pt p) {
    const std::size_t n = r.v.size();
    double best = 0.0;
    std::size_t bestEdge = 0;
    double bestU = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const Pt a = r.v[i];
        const Pt e = r.v[(i + 1) % n] - a;
        const double len2 = dot(e, e);
        const double u = len2 > 0.0 ? std::clamp(dot(p - a, e) / len2, 0.0, 1.0) : 0.0;
        const Pt q = a + e * u;
        const double d = norm(p - q);
        if (i == 0 || d < best) {
            best = d;
            bestEdge = i;
            bestU = u;
        }
    }
    const Pt a = r.v[bestEdge];
    const Pt q = a + (r.v[(bestEdge + 1) % n] - a) * bestU;
    std::vector<Pt> pts;
    pts.reserve(n + 1);
    pts.push_back(q);
    for (std::size_t k = 1; k <= n; ++k) {
        pts.push_back(r.v[(bestEdge + k) % n]);
    }
    return finish_ring(std::move(pts));
}

// Position à la fraction `t` ∈ [0 ; 1] de la longueur de l'anneau.
Pt ring_at(const Ring& r, double t) {
    if (t <= 0.0 || t >= 1.0) {
        return r.v[0];
    }
    const double s = t * r.length;
    std::size_t i =
        static_cast<std::size_t>(std::upper_bound(r.cum.begin(), r.cum.end(), s) - r.cum.begin());
    i = i == 0 ? 0 : i - 1;
    const std::size_t n = r.v.size();
    const double end = i + 1 < n ? r.cum[i + 1] : r.length;
    const double el = end - r.cum[i];
    const double u = el > 0.0 ? (s - r.cum[i]) / el : 0.0;
    return r.v[i] + (r.v[(i + 1) % n] - r.v[i]) * u;
}

// Fractions de longueur des points d'un anneau : les coins marqués sont toujours cousus, entre
// deux coins les points sont équirépartis (au plus `max_len`, au moins `min_len` si possible).
std::vector<double> sample_fractions(const Ring& r, double max_len, double min_len) {
    const std::size_t n = r.v.size();
    std::vector<std::size_t> breaks{0};
    for (std::size_t i = 1; i < n; ++i) {
        const Pt a = r.v[i] - r.v[i - 1];
        const Pt b = r.v[(i + 1) % n] - r.v[i];
        const double na = norm(a);
        const double nb = norm(b);
        if (na > 0.0 && nb > 0.0 && dot(a, b) / (na * nb) < kSharpCos) {
            breaks.push_back(i);
        }
    }
    std::vector<double> out;
    for (std::size_t j = 0; j < breaks.size(); ++j) {
        const double s0 = r.cum[breaks[j]];
        const double s1 = j + 1 < breaks.size() ? r.cum[breaks[j + 1]] : r.length;
        const double d = s1 - s0;
        int count = std::max(1, static_cast<int>(std::ceil(d / max_len - 1e-9)));
        if (count > 1 && d / count < min_len) {
            const int fewer = std::max(1, static_cast<int>(std::floor(d / min_len)));
            if (d / fewer <= max_len) {
                count = fewer;
            }
        }
        for (int k = 0; k < count; ++k) {
            out.push_back((s0 + d * double(k) / double(count)) / r.length);
        }
    }
    return out;
}

// Arbre des anneaux : le fils d'un nœud est sa région décalée d'un cran (une région qui se scinde
// donne plusieurs fils ; celle qui disparaît, aucun).
struct Node {
    geometry::PathSet set;
    std::vector<Node> children;
};

void grow(Node& node, Micrometers spacing, int level) {
    if (level >= kMaxLevels) {
        return;
    }
    auto shrunk = geometry::inset_path_set(node.set, spacing);
    if (!shrunk) {
        return;
    }
    for (geometry::PathSet& s : *shrunk) {
        Node child;
        child.set = std::move(s);
        grow(child, spacing, level + 1);
        node.children.push_back(std::move(child));
    }
}

class Builder {
public:
    Builder(double max_len, double min_len, bool spiral)
        : max_len_(max_len), min_len_(min_len), spiral_(spiral) {}

    std::vector<FillStitch> take() { return std::move(out_); }

    void emit_node(const Node& node, const Ring& outer) {
        emit_holes(node, outer.v[0]);

        // Fils : anneau décalé redémarré au plus près du départ, le plus proche d'abord.
        struct Child {
            const Node* node;
            Ring ring;
            double dist;
        };
        std::vector<Child> kids;
        for (const Node& c : node.children) {
            Ring ring = make_ring(c.set.outer);
            if (!ring.valid()) {
                continue;
            }
            ring = rotate_to(ring, outer.v[0]);
            kids.push_back({&c, std::move(ring), 0.0});
            kids.back().dist = norm(kids.back().ring.v[0] - outer.v[0]);
        }
        std::stable_sort(kids.begin(), kids.end(),
                         [](const Child& a, const Child& b) { return a.dist < b.dist; });

        const std::vector<double> t = sample_fractions(outer, max_len_, min_len_);
        // Spirale seulement si le fils épouse bien le parent : un îlot issu d'une scission serait
        // rejoint par des segments traversant le vide.
        const bool spirals =
            spiral_ && !kids.empty() && kids.front().ring.length > 0.5 * outer.length;
        if (spirals) {
            // Chaque tour glisse de l'anneau vers l'anneau suivant ; le dernier point (t = 1)
            // est le départ de l'anneau suivant, cousu par l'appel récursif.
            for (const double ti : t) {
                push(ring_at(outer, ti) * (1.0 - ti) + ring_at(kids.front().ring, ti) * ti);
            }
        } else {
            for (const double ti : t) {
                push(ring_at(outer, ti));
            }
            push(outer.v[0]); // anneau fermé
        }
        for (std::size_t i = 0; i < kids.size(); ++i) {
            const Ring start = i == 0 ? kids[i].ring : rotate_to(kids[i].ring, tail());
            emit_node(*kids[i].node, start);
        }
    }

private:
    [[nodiscard]] Pt tail() const {
        return {double(out_.back().pos.x.value), double(out_.back().pos.y.value)};
    }

    // Boucles de trou du nœud, la plus proche de l'aiguille d'abord, chacune cousue en anneau
    // fermé (elles ne sont pas reliées en spirale : un trou est contourné à chaque niveau).
    void emit_holes(const Node& node, Pt from) {
        std::vector<Ring> holes;
        for (const geometry::Path& h : node.set.holes) {
            Ring ring = make_ring(h);
            if (ring.valid()) {
                holes.push_back(std::move(ring));
            }
        }
        while (!holes.empty()) {
            const Pt at = out_.empty() ? from : tail();
            std::size_t best = 0;
            double bestD = 0.0;
            for (std::size_t i = 0; i < holes.size(); ++i) {
                double d = 0.0;
                for (std::size_t k = 0; k < holes[i].v.size(); ++k) {
                    const double dk = norm(holes[i].v[k] - at);
                    d = k == 0 ? dk : std::min(d, dk);
                }
                if (i == 0 || d < bestD) {
                    best = i;
                    bestD = d;
                }
            }
            const Ring ring = rotate_to(holes[best], at);
            holes.erase(holes.begin() + static_cast<std::ptrdiff_t>(best));
            for (const double ti : sample_fractions(ring, max_len_, min_len_)) {
                push(ring_at(ring, ti));
            }
            push(ring.v[0]);
        }
    }

    // Ajoute un point : lié au précédent par un point (≤ max), subdivisé (≤ 2 max) ou un saut.
    void push(Pt p) {
        const Vec2um q{Micrometers{static_cast<std::int32_t>(std::lround(p.x))},
                       Micrometers{static_cast<std::int32_t>(std::lround(p.y))}};
        if (!out_.empty()) {
            const Vec2um last = out_.back().pos;
            if (last == q) {
                return;
            }
            const double dx = double(q.x.value) - double(last.x.value);
            const double dy = double(q.y.value) - double(last.y.value);
            const double d = std::hypot(dx, dy);
            if (d > max_len_) {
                if (d <= 2.0 * max_len_) {
                    const int steps = static_cast<int>(std::ceil(d / max_len_));
                    for (int s = 1; s < steps; ++s) {
                        const double f = double(s) / double(steps);
                        out_.push_back({Vec2um{Micrometers{static_cast<std::int32_t>(
                                                   std::lround(double(last.x.value) + dx * f))},
                                               Micrometers{static_cast<std::int32_t>(
                                                   std::lround(double(last.y.value) + dy * f))}},
                                        false, false});
                    }
                } else {
                    out_.push_back({q, true, false});
                    return;
                }
            }
        }
        out_.push_back({q, false, false});
    }

    double max_len_;
    double min_len_;
    bool spiral_;
    std::vector<FillStitch> out_;
};

} // namespace

std::vector<FillStitch> fill_contour(const geometry::PathSet& region,
                                     const document::ContourFillParams& params) {
    const std::int32_t spacingUm = std::clamp(params.row_spacing.value, 100, 10'000);
    const double maxLen = std::clamp(params.stitch_length.value, 1'000, 7'000);
    const double minLen = std::clamp(double(params.min_length.value), 0.0, maxLen);

    Ring root = make_ring(region.outer);
    if (!root.valid()) {
        return {};
    }
    // Départ déterministe : le sommet le plus bas (puis le plus à gauche).
    std::size_t first = 0;
    for (std::size_t i = 1; i < root.v.size(); ++i) {
        const Pt& a = root.v[i];
        const Pt& b = root.v[first];
        if (a.y < b.y || (a.y == b.y && a.x < b.x)) {
            first = i;
        }
    }
    root = rotate_to(root, root.v[first]);

    Node tree;
    tree.set = region;
    grow(tree, Micrometers{spacingUm}, 0);

    Builder builder(maxLen, minLen, params.spiral);
    builder.emit_node(tree, root);
    return builder.take();
}

} // namespace openstitch::stitch_generation
