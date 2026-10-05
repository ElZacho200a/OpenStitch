// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/directional_fill.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <numbers>
#include <utility>

#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/moments.hpp"
#include "openstitch/geometry/offset.hpp"
#include "openstitch/geometry/polyline.hpp"

namespace openstitch::stitch_generation {

namespace {

constexpr double kPi = std::numbers::pi;

// Bornes métier de la longueur de point (µm) : cible bornée à [1 ; 7] mm,
// aucun point plus court que 0,5 mm (y compris aux extrémités de ligne).
constexpr double kMinTarget = 1'000.0;
constexpr double kMaxTarget = 7'000.0;
constexpr double kMinStitch = 500.0;
// Tolérance d'aplatissement des guides/contours (µm).
constexpr Micrometers kFlattenTolerance{50};
// Écart maximal toléré entre une corde (point cousu) et la ligne de courant
// qu'elle approxime ; au-delà, la corde est scindée.
constexpr double kMaxSagitta = 150.0;
// Amplitude maximale de l'ondulation de direction « fait main » (à 100 %).
constexpr double kMaxHandmadeTurn = 6.0 * kPi / 180.0;
// Maille du bruit lissé de direction (µm) : l'ondulation varie à l'échelle
// de quelques millimètres, jamais d'un point à l'autre.
constexpr double kNoiseCell = 5'000.0;

// --- Petite algèbre 2D (µm, double) -----------------------------------------

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
P2 operator*(P2 a, double k) {
    return {a.x * k, a.y * k};
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
double dist2(P2 a, P2 b) {
    return dot(a - b, a - b);
}
double dist(P2 a, P2 b) {
    return std::sqrt(dist2(a, b));
}
double orient(P2 a, P2 b, P2 c) {
    return cross(b - a, c - a);
}
P2 to_p2(Vec2um v) {
    return {static_cast<double>(v.x.value), static_cast<double>(v.y.value)};
}
Vec2um to_um(P2 p) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(p.x))},
                  Micrometers{static_cast<std::int32_t>(std::lround(p.y))}};
}
// Orientation (vecteur unitaire, signe indifférent) -> vecteur sur l'angle
// DOUBLÉ : t et -t donnent le même résultat, ce qui rend l'interpolation
// insensible au sens de parcours des guides.
P2 doubled(P2 t) {
    return {t.x * t.x - t.y * t.y, 2.0 * t.x * t.y};
}
P2 from_angle(double a) {
    return {std::cos(a), std::sin(a)};
}
int posmod(int a, int m) {
    const int r = a % m;
    return r < 0 ? r + m : r;
}

// --- Pseudo-aléatoire à graine fixe (Phase 3) --------------------------------
//
// Jamais `std::uniform_real_distribution` : sa sortie dépend de
// l'implémentation de la bibliothèque standard. Un hachage splitmix64 donne
// la même valeur sur toute plateforme, pour une clé donnée — c'est ce qui
// garantit « même projet, même résultat ».

std::uint64_t splitmix(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27U)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31U);
}

// Valeur dans [-1, 1], fonction pure de (graine, a, b, c).
double hash_unit(std::uint32_t seed, std::int64_t a, std::int64_t b, std::int64_t c) {
    std::uint64_t h = splitmix(static_cast<std::uint64_t>(seed));
    h = splitmix(h ^ static_cast<std::uint64_t>(a));
    h = splitmix(h ^ static_cast<std::uint64_t>(b));
    h = splitmix(h ^ static_cast<std::uint64_t>(c));
    const double unit = static_cast<double>(h >> 11U) * (1.0 / 9007199254740992.0); // [0,1)
    return unit * 2.0 - 1.0;
}

// Bruit de valeur lissé (interpolation smoothstep entre des valeurs tirées
// aux nœuds d'un réseau de pas `kNoiseCell`), dans [-1, 1]. Continu : deux
// points voisins reçoivent presque la même perturbation.
double smooth_noise(std::uint32_t seed, P2 p) {
    const double gx = p.x / kNoiseCell;
    const double gy = p.y / kNoiseCell;
    const double fx0 = std::floor(gx);
    const double fy0 = std::floor(gy);
    const auto ix = static_cast<std::int64_t>(fx0);
    const auto iy = static_cast<std::int64_t>(fy0);
    const double tx = gx - fx0;
    const double ty = gy - fy0;
    const double sx = tx * tx * (3.0 - 2.0 * tx);
    const double sy = ty * ty * (3.0 - 2.0 * ty);
    const double v00 = hash_unit(seed, ix, iy, 7);
    const double v10 = hash_unit(seed, ix + 1, iy, 7);
    const double v01 = hash_unit(seed, ix, iy + 1, 7);
    const double v11 = hash_unit(seed, ix + 1, iy + 1, 7);
    const double a = v00 + (v10 - v00) * sx;
    const double b = v01 + (v11 - v01) * sx;
    return a + (b - a) * sy;
}

// --- Polygones -----------------------------------------------------------------

using Poly = std::vector<P2>;

// Aplatit un chemin FERMÉ (courbes comprises) en polygone ; le point de
// fermeture dupliqué éventuel est retiré.
Poly flatten_closed(const geometry::Path& path) {
    geometry::Path closed = path;
    closed.closed = true;
    const auto flat = geometry::flatten(closed, kFlattenTolerance);
    Poly poly;
    poly.reserve(flat.points.size());
    for (const Vec2um& v : flat.points) {
        poly.push_back(to_p2(v));
    }
    if (poly.size() >= 2 && dist2(poly.front(), poly.back()) < 1e-9) {
        poly.pop_back();
    }
    return poly;
}

std::vector<Poly> region_polys(const geometry::PathSet& set) {
    std::vector<Poly> polys;
    auto outer = flatten_closed(set.outer);
    if (outer.size() < 3) {
        return polys;
    }
    polys.push_back(std::move(outer));
    for (const auto& hole : set.holes) {
        auto h = flatten_closed(hole);
        if (h.size() >= 3) {
            polys.push_back(std::move(h));
        }
    }
    return polys;
}

// Aplatit un chemin OUVERT (guide, ligne de rupture).
std::vector<P2> flatten_open(const geometry::Path& path) {
    geometry::Path open = path;
    open.closed = false;
    const auto flat = geometry::flatten(open, kFlattenTolerance);
    std::vector<P2> pts;
    pts.reserve(flat.points.size());
    for (const Vec2um& v : flat.points) {
        if (pts.empty() || dist2(pts.back(), to_p2(v)) > 1e-9) {
            pts.push_back(to_p2(v));
        }
    }
    return pts;
}

struct Edge {
    P2 a;
    P2 b;
};

// Index spatial d'une région (extérieur + trous) : grille régulière dont
// chaque case connaît les arêtes qui la traversent et l'état
// intérieur/extérieur de son CENTRE. Rend exacts et rapides les trois tests
// dont le traçage a besoin des centaines de milliers de fois : point
// intérieur, segment intérieur, bord le plus proche. Les origines de grille
// sont décalées d'une fraction non entière pour qu'un centre de case ne
// tombe jamais exactement sur une arête (coordonnées entières en µm).
class RegionIndex {
public:
    RegionIndex() = default;

    RegionIndex(const std::vector<Poly>& polys, double cell) : cell_(std::max(cell, 50.0)) {
        for (const auto& poly : polys) {
            for (std::size_t i = 0; i < poly.size(); ++i) {
                edges_.push_back({poly[i], poly[(i + 1) % poly.size()]});
            }
        }
        if (edges_.empty()) {
            return;
        }
        double minx = edges_[0].a.x;
        double maxx = minx;
        double miny = edges_[0].a.y;
        double maxy = miny;
        for (const Edge& e : edges_) {
            minx = std::min(minx, e.a.x);
            maxx = std::max(maxx, e.a.x);
            miny = std::min(miny, e.a.y);
            maxy = std::max(maxy, e.a.y);
        }
        ox_ = minx - cell_ - 0.3719;
        oy_ = miny - cell_ - 0.2917;
        nx_ = static_cast<int>(std::ceil((maxx - ox_) / cell_)) + 2;
        ny_ = static_cast<int>(std::ceil((maxy - oy_) / cell_)) + 2;
        cells_.assign(static_cast<std::size_t>(nx_) * static_cast<std::size_t>(ny_), {});
        std::vector<int> touched;
        for (std::size_t k = 0; k < edges_.size(); ++k) {
            touched.clear();
            cells_of_segment(edges_[k].a, edges_[k].b, touched);
            for (const int c : touched) {
                cells_[static_cast<std::size_t>(c)].push_back(static_cast<int>(k));
            }
        }
        // État des centres de case, par balayage horizontal (règle
        // demi-ouverte sur y : un sommet n'est jamais compté deux fois).
        centerInside_.assign(cells_.size(), 0);
        std::vector<double> xs;
        for (int j = 0; j < ny_; ++j) {
            const double yc = oy_ + (j + 0.5) * cell_;
            xs.clear();
            for (const Edge& e : edges_) {
                if ((e.a.y <= yc && e.b.y > yc) || (e.b.y <= yc && e.a.y > yc)) {
                    const double t = (yc - e.a.y) / (e.b.y - e.a.y);
                    xs.push_back(e.a.x + t * (e.b.x - e.a.x));
                }
            }
            std::sort(xs.begin(), xs.end());
            std::size_t crossed = 0;
            for (int i = 0; i < nx_; ++i) {
                const double xc = ox_ + (i + 0.5) * cell_;
                while (crossed < xs.size() && xs[crossed] < xc) {
                    ++crossed;
                }
                centerInside_[index(i, j)] = static_cast<char>(crossed % 2 == 1);
            }
        }
    }

    [[nodiscard]] bool empty() const { return edges_.empty(); }
    [[nodiscard]] double min_x() const { return ox_ + cell_; }
    [[nodiscard]] double min_y() const { return oy_ + cell_; }
    [[nodiscard]] double max_x() const { return ox_ + (nx_ - 2) * cell_; }
    [[nodiscard]] double max_y() const { return oy_ + (ny_ - 2) * cell_; }

    // Point dans la région (extérieur moins trous) : parité du centre de sa
    // case, corrigée par les arêtes croisées entre ce centre et le point.
    [[nodiscard]] bool inside(P2 p) const {
        if (edges_.empty()) {
            return false;
        }
        const int i = static_cast<int>(std::floor((p.x - ox_) / cell_));
        const int j = static_cast<int>(std::floor((p.y - oy_) / cell_));
        if (i < 0 || j < 0 || i >= nx_ || j >= ny_) {
            return false;
        }
        const P2 c{ox_ + (i + 0.5) * cell_, oy_ + (j + 0.5) * cell_};
        bool in = centerInside_[index(i, j)] != 0;
        for (const int k : cells_[index(i, j)]) {
            if (crosses(c, p, edges_[static_cast<std::size_t>(k)])) {
                in = !in;
            }
        }
        return in;
    }

    // Le segment [a,b] reste-t-il entièrement dans la région ? Même principe
    // que `connector_invalid` du tatami (découpe à chaque intersection avec
    // une arête, milieu de chaque morceau testé), restreint aux arêtes des
    // cases traversées.
    [[nodiscard]] bool segment_inside(P2 a, P2 b) const {
        const P2 ab = b - a;
        const double abLen = norm(ab);
        if (abLen < 1e-6) {
            return inside(a);
        }
        std::vector<int> touched;
        cells_of_segment(a, b, touched);
        std::vector<int> ids;
        for (const int c : touched) {
            const auto& list = cells_[static_cast<std::size_t>(c)];
            ids.insert(ids.end(), list.begin(), list.end());
        }
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        std::vector<double> ts{0.0, 1.0};
        for (const int k : ids) {
            const Edge& e = edges_[static_cast<std::size_t>(k)];
            const P2 dc = e.b - e.a;
            const double denom = cross(ab, dc);
            if (std::abs(denom) < 1e-9 * abLen * norm(dc)) {
                continue; // parallèle : suivi de bord, pas de découpe
            }
            const double t = cross(e.a - a, dc) / denom;
            const double s = cross(e.a - a, ab) / denom;
            if (t > 1e-9 && t < 1.0 - 1e-9 && s >= -1e-9 && s <= 1.0 + 1e-9) {
                ts.push_back(t);
            }
        }
        std::sort(ts.begin(), ts.end());
        for (std::size_t i = 0; i + 1 < ts.size(); ++i) {
            if (!inside(a + ab * ((ts[i] + ts[i + 1]) / 2.0))) {
                return false;
            }
        }
        return true;
    }

    // Bord le plus proche : distance et tangente unitaire. Recherche par
    // anneaux de cases croissants, arrêtée dès qu'aucun anneau plus lointain
    // ne peut contenir mieux (égalité départagée par l'index d'arête).
    [[nodiscard]] std::optional<std::pair<double, P2>> nearest_edge(P2 p) const {
        if (edges_.empty()) {
            return std::nullopt;
        }
        const int ci = std::clamp(static_cast<int>(std::floor((p.x - ox_) / cell_)), 0, nx_ - 1);
        const int cj = std::clamp(static_cast<int>(std::floor((p.y - oy_) / cell_)), 0, ny_ - 1);
        double best = std::numeric_limits<double>::max();
        int bestK = -1;
        const int maxR = std::max(nx_, ny_);
        for (int r = 0; r <= maxR; ++r) {
            if (bestK >= 0 && best <= (r - 1) * cell_) {
                break;
            }
            for (int j = cj - r; j <= cj + r; ++j) {
                for (int i = ci - r; i <= ci + r; ++i) {
                    if (std::max(std::abs(i - ci), std::abs(j - cj)) != r || i < 0 || j < 0 ||
                        i >= nx_ || j >= ny_) {
                        continue;
                    }
                    for (const int k : cells_[index(i, j)]) {
                        const Edge& e = edges_[static_cast<std::size_t>(k)];
                        const double d = point_segment_distance(p, e.a, e.b);
                        if (d < best || (d == best && k < bestK)) {
                            best = d;
                            bestK = k;
                        }
                    }
                }
            }
        }
        if (bestK < 0) {
            return std::nullopt;
        }
        const Edge& e = edges_[static_cast<std::size_t>(bestK)];
        const P2 t = e.b - e.a;
        const double n = norm(t);
        if (n < 1e-9) {
            return std::nullopt;
        }
        return std::make_pair(best, t * (1.0 / n));
    }

    static double point_segment_distance(P2 p, P2 a, P2 b) {
        const P2 ab = b - a;
        const double len2 = dot(ab, ab);
        if (len2 < 1e-12) {
            return dist(p, a);
        }
        const double t = std::clamp(dot(p - a, ab) / len2, 0.0, 1.0);
        return dist(p, a + ab * t);
    }

private:
    [[nodiscard]] std::size_t index(int i, int j) const {
        return static_cast<std::size_t>(j) * static_cast<std::size_t>(nx_) +
               static_cast<std::size_t>(i);
    }

    // Parité demi-ouverte : un sommet exactement sur [c,p] est rangé du côté
    // « <= 0 » pour ses deux arêtes, donc compté une seule fois s'il est
    // réellement traversé, zéro ou deux fois s'il n'est que touché.
    static bool crosses(P2 c, P2 p, const Edge& e) {
        const double o1 = orient(c, p, e.a);
        const double o2 = orient(c, p, e.b);
        if ((o1 > 0.0) == (o2 > 0.0)) {
            return false;
        }
        const double o3 = orient(e.a, e.b, c);
        const double o4 = orient(e.a, e.b, p);
        return (o3 > 0.0) != (o4 > 0.0);
    }

    // Cases traversées par un segment (exact : par bande horizontale de
    // cases, intervalle en x du segment restreint à la bande).
    void cells_of_segment(P2 a, P2 b, std::vector<int>& out) const {
        const double y0 = std::min(a.y, b.y);
        const double y1 = std::max(a.y, b.y);
        const int j0 = std::clamp(static_cast<int>(std::floor((y0 - oy_) / cell_)), 0, ny_ - 1);
        const int j1 = std::clamp(static_cast<int>(std::floor((y1 - oy_) / cell_)), 0, ny_ - 1);
        const double dy = b.y - a.y;
        for (int j = j0; j <= j1; ++j) {
            const double bandLo = std::max(y0, oy_ + j * cell_);
            const double bandHi = std::min(y1, oy_ + (j + 1) * cell_);
            double xa = 0.0;
            double xb = 0.0;
            if (std::abs(dy) < 1e-12) {
                xa = a.x;
                xb = b.x;
            } else {
                const double ta = std::clamp((bandLo - a.y) / dy, 0.0, 1.0);
                const double tb = std::clamp((bandHi - a.y) / dy, 0.0, 1.0);
                xa = a.x + ta * (b.x - a.x);
                xb = a.x + tb * (b.x - a.x);
            }
            const int i0 = std::clamp(
                static_cast<int>(std::floor((std::min(xa, xb) - ox_) / cell_)), 0, nx_ - 1);
            const int i1 = std::clamp(
                static_cast<int>(std::floor((std::max(xa, xb) - ox_) / cell_)), 0, nx_ - 1);
            for (int i = i0; i <= i1; ++i) {
                out.push_back(static_cast<int>(index(i, j)));
            }
        }
    }

    std::vector<Edge> edges_;
    double cell_{500.0};
    double ox_{0.0};
    double oy_{0.0};
    int nx_{0};
    int ny_{0};
    std::vector<std::vector<int>> cells_;
    std::vector<char> centerInside_;
};

// --- Champ de directions -----------------------------------------------------

// Plus proche point d'une polyligne et tangente unitaire du segment porteur.
struct Nearest {
    double d2{std::numeric_limits<double>::max()};
    P2 tangent{1.0, 0.0};
};

Nearest nearest_on_polyline(const std::vector<P2>& pts, P2 p) {
    Nearest best;
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        const P2 a = pts[i];
        const P2 ab = pts[i + 1] - a;
        const double len2 = dot(ab, ab);
        if (len2 < 1e-12) {
            continue;
        }
        const double t = std::clamp(dot(p - a, ab) / len2, 0.0, 1.0);
        const double d2 = dist2(p, a + ab * t);
        if (d2 < best.d2) {
            best.d2 = d2;
            best.tangent = ab * (1.0 / std::sqrt(len2));
        }
    }
    return best;
}

struct FieldConfig {
    double softening{
        400.0}; // adoucissement de la pondération (µm) : pas de singularité sur un guide
    double edgeWeight{0.0};   // [0 ; 1]
    double edgeBand{2'000.0}; // décroissance de l'influence du bord (µm)
    double fallbackAngle{0.0};
    double noiseAmplitude{0.0}; // radians (0 = pas d'ondulation)
    std::uint32_t seed{0};
};

// Champ d'orientations d'un secteur, échantillonné sur une grille régulière
// (vecteurs unitaires sur l'angle doublé) puis interpolé bilinéairement.
class DirectionField {
public:
    DirectionField() = default;

    DirectionField(const std::vector<std::vector<P2>>& guides, const RegionIndex& edges,
                   const FieldConfig& cfg, P2 lo, P2 hi, double cell)
        : cfg_(cfg), cell_(cell) {
        ox_ = lo.x - 2.0 * cell_;
        oy_ = lo.y - 2.0 * cell_;
        nx_ = static_cast<int>(std::ceil((hi.x - ox_) / cell_)) + 3;
        ny_ = static_cast<int>(std::ceil((hi.y - oy_) / cell_)) + 3;
        const P2 fallback = doubled(from_angle(cfg_.fallbackAngle));
        const double h2 = cfg_.softening * cfg_.softening;
        values_.resize(static_cast<std::size_t>(nx_) * static_cast<std::size_t>(ny_));
        for (int j = 0; j < ny_; ++j) {
            for (int i = 0; i < nx_; ++i) {
                const P2 p{ox_ + i * cell_, oy_ + j * cell_};
                P2 base = fallback;
                if (!guides.empty()) {
                    // Pondération inverse au carré de la distance, une
                    // contribution par guide (tangente au point le plus proche).
                    P2 acc{0.0, 0.0};
                    double wsum = 0.0;
                    for (const auto& g : guides) {
                        const Nearest n = nearest_on_polyline(g, p);
                        if (n.d2 == std::numeric_limits<double>::max()) {
                            continue;
                        }
                        const double w = 1.0 / (n.d2 + h2);
                        acc = acc + doubled(n.tangent) * w;
                        wsum += w;
                    }
                    if (wsum > 0.0) {
                        const P2 mean = acc * (1.0 / wsum);
                        const double m = norm(mean);
                        // Guides en désaccord parfait (annulation) : repli sur l'axe.
                        base = m > 1e-6 ? mean * (1.0 / m) : fallback;
                    }
                }
                if (cfg_.edgeWeight > 0.0) {
                    if (const auto e = edges.nearest_edge(p)) {
                        const double alpha = std::clamp(cfg_.edgeWeight, 0.0, 1.0) *
                                             std::exp(-e->first / cfg_.edgeBand);
                        const P2 mixed = base * (1.0 - alpha) + doubled(e->second) * alpha;
                        const double m = norm(mixed);
                        if (m > 1e-6) {
                            base = mixed * (1.0 / m);
                        }
                    }
                }
                values_[static_cast<std::size_t>(j) * static_cast<std::size_t>(nx_) +
                        static_cast<std::size_t>(i)] = base;
            }
        }
    }

    // Orientation unitaire en p (signe arbitraire), ondulation comprise.
    [[nodiscard]] P2 dir(P2 p) const { return from_angle(angle(p)); }

    // Orientation en p, en radians (non normalisée à [0, pi)).
    [[nodiscard]] double angle(P2 p) const {
        const P2 v = doubled_at(p);
        double a = 0.5 * std::atan2(v.y, v.x);
        if (cfg_.noiseAmplitude > 0.0) {
            a += cfg_.noiseAmplitude * smooth_noise(cfg_.seed, p);
        }
        return a;
    }

    // Vecteur doublé interpolé (non ondulé), pour la direction moyenne.
    [[nodiscard]] P2 doubled_at(P2 p) const {
        const double gx = std::clamp((p.x - ox_) / cell_, 0.0, static_cast<double>(nx_ - 1));
        const double gy = std::clamp((p.y - oy_) / cell_, 0.0, static_cast<double>(ny_ - 1));
        const int i = std::min(static_cast<int>(gx), nx_ - 2);
        const int j = std::min(static_cast<int>(gy), ny_ - 2);
        const double tx = gx - i;
        const double ty = gy - j;
        const P2 v00 = at(i, j);
        const P2 v10 = at(i + 1, j);
        const P2 v01 = at(i, j + 1);
        const P2 v11 = at(i + 1, j + 1);
        const P2 v =
            (v00 * (1.0 - tx) + v10 * tx) * (1.0 - ty) + (v01 * (1.0 - tx) + v11 * tx) * ty;
        if (norm(v) < 1e-9) {
            // Point singulier exact : valeur du nœud le plus proche.
            return at(std::min(static_cast<int>(std::lround(gx)), nx_ - 1),
                      std::min(static_cast<int>(std::lround(gy)), ny_ - 1));
        }
        return v;
    }

private:
    [[nodiscard]] P2 at(int i, int j) const {
        return values_[static_cast<std::size_t>(j) * static_cast<std::size_t>(nx_) +
                       static_cast<std::size_t>(i)];
    }

    FieldConfig cfg_;
    double cell_{500.0};
    double ox_{0.0};
    double oy_{0.0};
    int nx_{0};
    int ny_{0};
    std::vector<P2> values_;
};

// --- Secteurs ------------------------------------------------------------------

struct Sector {
    std::vector<Poly> polys;      // secteur propre (sans chevauchement)
    std::vector<Poly> tracePolys; // zone de traçage (secteur élargi du chevauchement)
    RegionIndex own;
    RegionIndex trace;
    DirectionField field;
};

double clamp_spacing(const document::DirectionalFillParams& p) {
    return static_cast<double>(std::max<std::int32_t>(100, p.row_spacing.value));
}

double handmade_intensity(const document::DirectionalFillParams& p) {
    return p.handmade ? std::clamp(p.handmade_intensity, 0, 100) / 100.0 : 0.0;
}

// Bande fine (20 µm) le long d'un segment, prolongée de `extA`/`extB` aux
// extrémités : retirée de la région, elle la sépare en secteurs.
geometry::Path band(P2 a, P2 b, double extA, double extB) {
    const P2 d = b - a;
    const double n = norm(d);
    geometry::Path path;
    path.closed = true;
    if (n < 1e-6) {
        return path;
    }
    const P2 u = d * (1.0 / n);
    const P2 v{-u.y * 10.0, u.x * 10.0};
    const P2 a2 = a - u * extA;
    const P2 b2 = b + u * extB;
    for (const P2 q : {a2 + v, b2 + v, b2 - v, a2 - v}) {
        path.nodes.push_back({to_um(q), geometry::NodeType::Corner, {}, {}});
    }
    return path;
}

// Découpe `region` par les lignes de rupture ; chaque morceau est un secteur.
std::vector<geometry::PathSet> split_sectors(const geometry::PathSet& region,
                                             const document::DirectionalFillParams& params) {
    if (params.break_lines.empty()) {
        return {region};
    }
    const double spacing = clamp_spacing(params);
    // Prolongement des extrémités : une rupture tracée « presque » jusqu'au
    // bord la traverse quand même.
    const double ext = std::max(spacing, 1'000.0);
    std::vector<geometry::Path> cutouts;
    for (const auto& line : params.break_lines) {
        const auto pts = flatten_open(line);
        for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
            auto b = band(pts[i], pts[i + 1], i == 0 ? ext : 0.0, i + 2 == pts.size() ? ext : 0.0);
            if (b.nodes.size() == 4) {
                cutouts.push_back(std::move(b));
            }
        }
    }
    if (cutouts.empty()) {
        return {region};
    }
    auto pieces = geometry::subtract_polygons(region, cutouts);
    if (!pieces || pieces->empty()) {
        return {region};
    }
    // Les miettes (plus petites que quelques mailles) ne forment pas un secteur.
    std::vector<geometry::PathSet> out;
    for (auto& piece : *pieces) {
        if (geometry::path_set_area_um2(piece) >= 4.0 * spacing * spacing) {
            out.push_back(std::move(piece));
        }
    }
    return out.empty() ? std::vector<geometry::PathSet>{region} : out;
}

// Morceaux (≥ 2 points) des guides situés dans le secteur. Les guides sont
// d'abord densifiés (pas ≤ 0,5 mm) pour qu'un guide traversant une rupture
// soit coupé au bon endroit.
std::vector<std::vector<P2>> clip_guides(const std::vector<std::vector<P2>>& guides,
                                         const RegionIndex& own) {
    std::vector<std::vector<P2>> out;
    for (const auto& g : guides) {
        std::vector<P2> run;
        for (const P2 p : g) {
            if (own.inside(p)) {
                run.push_back(p);
            } else {
                if (run.size() >= 2) {
                    out.push_back(run);
                }
                run.clear();
            }
        }
        if (run.size() >= 2) {
            out.push_back(std::move(run));
        }
    }
    return out;
}

std::vector<std::vector<P2>> flattened_guides(const document::DirectionalFillParams& params,
                                              bool densify) {
    std::vector<std::vector<P2>> guides;
    for (const auto& guide : params.guides) {
        auto pts = flatten_open(guide);
        if (pts.size() < 2) {
            continue;
        }
        if (densify) {
            // Densification SANS perdre les sommets : chaque segment est
            // subdivisé indépendamment.
            std::vector<P2> dense{pts.front()};
            for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
                const double len = dist(pts[i], pts[i + 1]);
                const int steps = std::max(1, static_cast<int>(std::ceil(len / 500.0)));
                for (int k = 1; k <= steps; ++k) {
                    dense.push_back(pts[i] +
                                    (pts[i + 1] - pts[i]) * (static_cast<double>(k) / steps));
                }
            }
            pts = std::move(dense);
        }
        guides.push_back(std::move(pts));
    }
    return guides;
}

void bounds(const std::vector<Poly>& polys, P2& lo, P2& hi) {
    lo = {std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
    hi = {std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest()};
    for (const auto& poly : polys) {
        for (const P2 p : poly) {
            lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
            hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
        }
    }
}

// Construit les secteurs de `region` et leur champ.
std::vector<Sector> build_sectors(const geometry::PathSet& region,
                                  const document::DirectionalFillParams& params) {
    std::vector<Sector> sectors;
    const double spacing = clamp_spacing(params);
    const double indexCell = std::clamp(spacing, 250.0, 1'000.0);
    const auto pieces = split_sectors(region, params);
    const bool multi = pieces.size() > 1;
    const auto guides = flattened_guides(params, multi);
    const double overlap =
        static_cast<double>(std::max<std::int32_t>(0, params.sector_overlap.value));
    const double intensity = handmade_intensity(params);

    for (const auto& piece : pieces) {
        Sector s;
        s.polys = region_polys(piece);
        if (s.polys.empty()) {
            continue;
        }
        s.own = RegionIndex(s.polys, indexCell);
        // Zone de traçage : le secteur élargi du chevauchement, jamais au-delà
        // de la région d'origine.
        s.tracePolys = s.polys;
        if (multi && overlap > 0.0) {
            if (auto grown = geometry::inset_path_set(
                    piece, Micrometers{-static_cast<std::int32_t>(std::lround(overlap))});
                grown && !grown->empty()) {
                if (auto clipped = geometry::intersect_polygons(*grown, {region});
                    clipped && !clipped->empty()) {
                    s.tracePolys.clear();
                    for (const auto& c : *clipped) {
                        for (auto& poly : region_polys(c)) {
                            s.tracePolys.push_back(std::move(poly));
                        }
                    }
                }
            }
        }
        s.trace = RegionIndex(s.tracePolys, indexCell);

        FieldConfig cfg;
        cfg.softening = spacing;
        cfg.edgeWeight = std::clamp(params.edge_weight, 0.0, 1.0);
        cfg.edgeBand = std::max(2'000.0, 5.0 * spacing);
        cfg.seed = params.seed;
        cfg.noiseAmplitude = intensity * kMaxHandmadeTurn;
        if (const auto axis = geometry::principal_axis({piece})) {
            cfg.fallbackAngle = axis->angle.radians;
        }
        const auto sectorGuides = multi ? clip_guides(guides, s.own) : guides;
        P2 lo;
        P2 hi;
        bounds(s.tracePolys, lo, hi);
        s.field = DirectionField(sectorGuides, s.own, cfg, lo, hi, indexCell);
        sectors.push_back(std::move(s));
    }
    return sectors;
}

// --- Lignes de courant (Jobard & Lefer) ---------------------------------------

struct Traced {
    std::vector<P2> pts;
    std::vector<double> u; // coordonnée d'arc alignée sur la ligne mère (décalage des pénétrations)
    int depth{0};          // rang relatif (±1 d'une ligne à sa voisine)
    double seedU{0.0};     // u de la graine (abscisses de la grille relatives à elle)
};

// Grille de séparation : cellule = distance de séparation ; chaque point
// tracé y est rangé avec sa ligne et son abscisse (signée) sur cette ligne.
class SeparationGrid {
public:
    struct Ref {
        int line{-1};
        double s{0.0}; // abscisse signée sur la ligne, relative à sa graine
        P2 p;
    };

    SeparationGrid(P2 lo, P2 hi, double cell) : cell_(cell) {
        ox_ = lo.x - 2.0 * cell_;
        oy_ = lo.y - 2.0 * cell_;
        nx_ = static_cast<int>(std::ceil((hi.x - ox_) / cell_)) + 3;
        ny_ = static_cast<int>(std::ceil((hi.y - oy_) / cell_)) + 3;
        cells_.assign(static_cast<std::size_t>(nx_) * static_cast<std::size_t>(ny_), {});
    }

    void add(P2 p, int line, double s) { cells_[cell_of(p)].push_back({line, s, p}); }

    // Un point d'une AUTRE ligne à moins de r ? (ou de la même ligne, hors de
    // la fenêtre d'abscisse `selfWindow` autour de s : boucle sur elle-même).
    [[nodiscard]] bool too_close(P2 q, double r, int line, double s, double selfWindow) const {
        const double r2 = r * r;
        const int ci = cx(q.x);
        const int cj = cy(q.y);
        const int reach = static_cast<int>(std::ceil(r / cell_));
        for (int j = std::max(0, cj - reach); j <= std::min(ny_ - 1, cj + reach); ++j) {
            for (int i = std::max(0, ci - reach); i <= std::min(nx_ - 1, ci + reach); ++i) {
                for (const Ref& ref : cells_[idx(i, j)]) {
                    if (ref.line == line && std::abs(ref.s - s) <= selfWindow) {
                        continue;
                    }
                    if (dist2(ref.p, q) < r2) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    // Point tracé le plus proche de q (à moins de rmax), départagé par
    // (ligne, abscisse) pour rester déterministe.
    [[nodiscard]] std::optional<Ref> nearest(P2 q, double rmax) const {
        std::optional<Ref> best;
        double bd = rmax * rmax;
        const int ci = cx(q.x);
        const int cj = cy(q.y);
        const int reach = static_cast<int>(std::ceil(rmax / cell_));
        for (int j = std::max(0, cj - reach); j <= std::min(ny_ - 1, cj + reach); ++j) {
            for (int i = std::max(0, ci - reach); i <= std::min(nx_ - 1, ci + reach); ++i) {
                for (const Ref& ref : cells_[idx(i, j)]) {
                    const double d = dist2(ref.p, q);
                    if (d < bd || (best && d == bd &&
                                   std::pair(ref.line, ref.s) < std::pair(best->line, best->s))) {
                        bd = d;
                        best = ref;
                    }
                }
            }
        }
        return best;
    }

private:
    [[nodiscard]] int cx(double x) const {
        return std::clamp(static_cast<int>(std::floor((x - ox_) / cell_)), 0, nx_ - 1);
    }
    [[nodiscard]] int cy(double y) const {
        return std::clamp(static_cast<int>(std::floor((y - oy_) / cell_)), 0, ny_ - 1);
    }
    [[nodiscard]] std::size_t idx(int i, int j) const {
        return static_cast<std::size_t>(j) * static_cast<std::size_t>(nx_) +
               static_cast<std::size_t>(i);
    }
    [[nodiscard]] std::size_t cell_of(P2 p) const { return idx(cx(p.x), cy(p.y)); }

    double cell_;
    double ox_{0.0};
    double oy_{0.0};
    int nx_{0};
    int ny_{0};
    std::vector<std::vector<Ref>> cells_;
};

class StreamlineTracer {
public:
    StreamlineTracer(const Sector& sector, double dsep)
        : sector_(sector), dsep_(dsep), dtest_(0.5 * dsep), step_(0.2 * dsep),
          selfWindow_(3.0 * dsep), grid_(make_grid(sector, dsep)) {
        P2 lo;
        P2 hi;
        bounds(sector.tracePolys, lo, hi);
        lo_ = lo;
        hi_ = hi;
        maxSteps_ = static_cast<int>(4.0 * (hi.x - lo.x + hi.y - lo.y) / step_) + 1'000;
    }

    std::vector<Traced> run() {
        // 1) Graine initiale : point de grille intérieur le plus proche du
        //    centre de la zone (balayage déterministe).
        const P2 center{(lo_.x + hi_.x) / 2.0, (lo_.y + hi_.y) / 2.0};
        const double g = dsep_ / 4.0;
        std::optional<P2> first;
        double bestD = std::numeric_limits<double>::max();
        for (double y = lo_.y + g / 2.0; y < hi_.y; y += g) {
            for (double x = lo_.x + g / 2.0; x < hi_.x; x += g) {
                const P2 c{x, y};
                const double d = dist2(c, center);
                if (d < bestD && sector_.trace.inside(c)) {
                    bestD = d;
                    first = c;
                }
            }
        }
        if (!first) {
            return {};
        }
        seed_and_propagate(*first, 1.0);

        // 2) Balayage de comblement : tout point de grille resté à plus de
        //    0,9 × dsep de toute ligne reçoit une nouvelle graine — aucune
        //    zone vide (convergences, îlots non atteints par propagation).
        for (double y = lo_.y + g / 2.0; y < hi_.y; y += g) {
            for (double x = lo_.x + g / 2.0; x < hi_.x; x += g) {
                seed_and_propagate(P2{x, y}, 0.9);
            }
        }
        return std::move(lines_);
    }

private:
    static SeparationGrid make_grid(const Sector& sector, double dsep) {
        P2 lo;
        P2 hi;
        bounds(sector.tracePolys, lo, hi);
        return SeparationGrid(lo, hi, dsep);
    }

    [[nodiscard]] bool valid_seed(P2 c, double factor) const {
        return sector_.trace.inside(c) && !grid_.too_close(c, factor * dsep_, -1, 0.0, 0.0);
    }

    // Graine isolée : hérite de la ligne la plus proche (abscisse, rang,
    // sens) pour garder l'imbrication des pénétrations.
    void seed_and_propagate(P2 c, double factor) {
        if (!valid_seed(c, factor)) {
            return;
        }
        P2 d = sector_.field.dir(c);
        double u0 = 0.0;
        int depth = 0;
        if (const auto ref = grid_.nearest(c, 3.0 * dsep_)) {
            const Traced& parent = lines_[static_cast<std::size_t>(ref->line)];
            const std::size_t k = index_of(parent, ref->s);
            const P2 t = tangent(parent, k);
            if (dot(d, t) < 0.0) {
                d = d * -1.0;
            }
            u0 = parent.u[k];
            depth = parent.depth + (cross(t, c - parent.pts[k]) >= 0.0 ? 1 : -1);
        }
        std::deque<int> queue;
        queue.push_back(trace(c, d, u0, depth));
        while (!queue.empty()) {
            const int id = queue.front();
            queue.pop_front();
            // Copie : `lines_` grandit pendant la propagation.
            const Traced parent = lines_[static_cast<std::size_t>(id)];
            double nextU = std::numeric_limits<double>::lowest();
            for (std::size_t k = 0; k < parent.pts.size(); ++k) {
                if (parent.u[k] < nextU) {
                    continue;
                }
                nextU = parent.u[k] + 0.5 * dsep_;
                const P2 t = tangent(parent, k);
                const P2 n{-t.y, t.x};
                for (const int side : {1, -1}) {
                    const P2 cand = parent.pts[k] + n * (side * dsep_);
                    if (!valid_seed(cand, 0.99)) {
                        continue;
                    }
                    P2 dir = sector_.field.dir(cand);
                    if (dot(dir, t) < 0.0) {
                        dir = dir * -1.0;
                    }
                    queue.push_back(trace(cand, dir, parent.u[k], parent.depth + side));
                }
            }
        }
    }

    static P2 tangent(const Traced& line, std::size_t k) {
        const std::size_t a = k == 0 ? 0 : k - 1;
        const std::size_t b = std::min(k + 1, line.pts.size() - 1);
        const P2 t = line.pts[b] - line.pts[a];
        const double n = norm(t);
        return n > 1e-9 ? t * (1.0 / n) : P2{1.0, 0.0};
    }

    // Index du point de `line` dont l'abscisse locale (u - u_graine) vaut s.
    static std::size_t index_of(const Traced& line, double s) {
        // Les abscisses de la grille sont relatives à la graine : u = u_graine + s.
        // On retrouve le point par recherche du plus proche en u.
        const double target = line.seedU + s;
        const auto it = std::lower_bound(line.u.begin(), line.u.end(), target);
        if (it == line.u.end()) {
            return line.u.size() - 1;
        }
        const auto k = static_cast<std::size_t>(it - line.u.begin());
        if (k > 0 && std::abs(line.u[k - 1] - target) < std::abs(line.u[k] - target)) {
            return k - 1;
        }
        return k;
    }

    // Intègre une demi-ligne depuis p0 dans la direction d0 (RK2, pas fixe),
    // jusqu'au bord, à une autre ligne, à un point singulier, ou au plafond.
    void integrate(P2 p0, P2 d0, double sign, int lineId, std::vector<P2>& pts,
                   std::vector<double>& ss) {
        const double maxTurnCos = std::cos(40.0 * kPi / 180.0);
        P2 p = p0;
        P2 d = d0;
        double s = 0.0;
        for (int step = 0; step < maxSteps_; ++step) {
            P2 v1 = sector_.field.dir(p);
            if (dot(v1, d) < 0.0) {
                v1 = v1 * -1.0;
            }
            P2 v2 = sector_.field.dir(p + v1 * (step_ / 2.0));
            if (dot(v2, v1) < 0.0) {
                v2 = v2 * -1.0;
            }
            if (dot(v2, d) < maxTurnCos) {
                break; // virage brutal : point singulier du champ
            }
            const P2 q = p + v2 * step_;
            if (!sector_.trace.inside(q)) {
                // Sortie de la région : le point de bord exact est cherché par
                // dichotomie pour que la couture atteigne le contour.
                P2 in = p;
                P2 out = q;
                for (int it = 0; it < 14; ++it) {
                    const P2 m = (in + out) * 0.5;
                    if (sector_.trace.inside(m)) {
                        in = m;
                    } else {
                        out = m;
                    }
                }
                const double ds = dist(p, in);
                if (ds > 1.0 &&
                    !grid_.too_close(in, dtest_, lineId, sign * (s + ds), selfWindow_)) {
                    pts.push_back(in);
                    ss.push_back(s + ds);
                    grid_.add(in, lineId, sign * (s + ds));
                }
                break;
            }
            if (grid_.too_close(q, dtest_, lineId, sign * (s + step_), selfWindow_)) {
                break;
            }
            s += step_;
            pts.push_back(q);
            ss.push_back(s);
            grid_.add(q, lineId, sign * s);
            d = v2;
            p = q;
        }
    }

    int trace(P2 seed, P2 dir, double u0, int depth) {
        const int id = static_cast<int>(lines_.size());
        grid_.add(seed, id, 0.0);
        std::vector<P2> fwd;
        std::vector<double> fs;
        std::vector<P2> bwd;
        std::vector<double> bs;
        integrate(seed, dir, 1.0, id, fwd, fs);
        integrate(seed, dir * -1.0, -1.0, id, bwd, bs);
        Traced line;
        line.depth = depth;
        line.seedU = u0;
        for (std::size_t k = bwd.size(); k-- > 0;) {
            line.pts.push_back(bwd[k]);
            line.u.push_back(u0 - bs[k]);
        }
        line.pts.push_back(seed);
        line.u.push_back(u0);
        for (std::size_t k = 0; k < fwd.size(); ++k) {
            line.pts.push_back(fwd[k]);
            line.u.push_back(u0 + fs[k]);
        }
        lines_.push_back(std::move(line));
        return id;
    }

    const Sector& sector_;
    double dsep_;
    double dtest_;
    double step_;
    double selfWindow_;
    SeparationGrid grid_;
    P2 lo_;
    P2 hi_;
    int maxSteps_{0};
    std::vector<Traced> lines_;
};

// --- Découpe en points -------------------------------------------------------

struct StitchPlan {
    double target{3'000.0};
    int stagger{2};
    double intensity{0.0}; // 0 = régulier
    std::uint32_t seed{0};
};

// Point de la ligne à la coordonnée u (interpolation linéaire).
P2 point_at_u(const Traced& line, double u) {
    const auto it = std::lower_bound(line.u.begin(), line.u.end(), u);
    if (it == line.u.begin()) {
        return line.pts.front();
    }
    if (it == line.u.end()) {
        return line.pts.back();
    }
    const auto k = static_cast<std::size_t>(it - line.u.begin());
    const double u0 = line.u[k - 1];
    const double u1 = line.u[k];
    const double t = u1 > u0 ? (u - u0) / (u1 - u0) : 0.0;
    return line.pts[k - 1] + (line.pts[k] - line.pts[k - 1]) * t;
}

// Flèche maximale de la ligne entre u=a et u=b par rapport à la corde.
double sagitta(const Traced& line, double a, double b) {
    const P2 pa = point_at_u(line, a);
    const P2 pb = point_at_u(line, b);
    const auto lo = std::upper_bound(line.u.begin(), line.u.end(), a);
    const auto hi = std::lower_bound(line.u.begin(), line.u.end(), b);
    double worst = 0.0;
    for (auto it = lo; it < hi; ++it) {
        const auto k = static_cast<std::size_t>(it - line.u.begin());
        worst = std::max(worst, RegionIndex::point_segment_distance(line.pts[k], pa, pb));
    }
    return worst;
}

// Pénétrations d'une ligne (en coordonnée u), puis positions.
std::vector<P2> place_stitches(const Traced& line, const StitchPlan& plan, std::int64_t lineKey,
                               const RegionIndex& zone) {
    std::vector<P2> out;
    if (line.pts.size() < 2) {
        return out;
    }
    const double u0 = line.u.front();
    const double u1 = line.u.back();
    if (u1 - u0 < kMinStitch) {
        return out;
    }
    const double L = std::clamp(plan.target, kMinTarget, kMaxTarget);
    const double I = plan.intensity;
    const int stagger = std::max(1, plan.stagger);
    // Pénétrations sur une grille de pas L en u, déphasée selon le rang de la
    // ligne : les points des lignes voisines se décalent (effet brique).
    const double phase = L * static_cast<double>(posmod(line.depth, stagger)) / stagger;
    std::vector<double> pos;
    const double firstK = std::ceil((u0 - phase) / L);
    for (double k = firstK;; k += 1.0) {
        double x = phase + k * L;
        if (x - 0.5 * L >= u1) {
            break;
        }
        if (I > 0.0) {
            // Longueurs irrégulières : ±40 % × intensité autour de la cible
            // (chaque pénétration bouge de ±20 %, l'écart entre deux varie de ±40 %).
            x += 0.2 * I * L * hash_unit(plan.seed, lineKey, static_cast<std::int64_t>(k), 1);
        }
        if (x > u0 && x < u1) {
            pos.push_back(x);
        }
    }
    std::sort(pos.begin(), pos.end());

    // Aspect fait main : alternance court/long du PREMIER et du DERNIER point
    // d'une ligne à sa voisine (parité du rang) — les extrémités s'imbriquent
    // comme un passé empiétant au lieu de s'aligner sur le bord.
    if (I > 0.0) {
        const bool odd = posmod(line.depth, 2) == 1;
        const double shortF = 1.0 - 0.6 * I;
        double firstLen =
            L * (odd ? shortF : 1.0) * (1.0 + 0.15 * I * hash_unit(plan.seed, lineKey, -1, 2));
        double lastLen =
            L * (odd ? 1.0 : shortF) * (1.0 + 0.15 * I * hash_unit(plan.seed, lineKey, -2, 2));
        firstLen = std::max(firstLen, kMinTarget);
        lastLen = std::max(lastLen, kMinTarget);
        const double f = u0 + firstLen;
        const double l = u1 - lastLen;
        if (l - f >= 0.5 * L) {
            std::vector<double> kept{f};
            for (const double x : pos) {
                if (x > f + 0.5 * L && x < l - 0.5 * L) {
                    kept.push_back(x);
                }
            }
            kept.push_back(l);
            pos = std::move(kept);
        }
    }

    // Bornes : aucun point < 0,5 mm aux extrémités, < 1 mm à l'intérieur en
    // mode fait main (la grille régulière est déjà au pas L ≥ 1 mm), > 7 mm
    // nulle part (scission en parts égales).
    std::vector<double> all{u0};
    for (const double x : pos) {
        const double minGap = (I > 0.0 && all.size() > 1) ? kMinTarget : kMinStitch;
        if (x - all.back() >= minGap && u1 - x >= kMinStitch) {
            all.push_back(x);
        }
    }
    all.push_back(u1);
    std::vector<double> bounded{all.front()};
    for (std::size_t i = 1; i < all.size(); ++i) {
        const double start = bounded.back();
        const double gap = all[i] - start;
        const int parts = std::max(1, static_cast<int>(std::ceil(gap / kMaxTarget - 1e-9)));
        for (int p = 1; p < parts; ++p) {
            bounded.push_back(start + gap * static_cast<double>(p) / parts);
        }
        bounded.push_back(all[i]);
    }

    // Fidélité à la courbe : une corde qui s'écarte trop de la ligne (ou qui
    // sortirait de la zone, bord concave) est scindée en deux.
    for (int pass = 0; pass < 8; ++pass) {
        bool changed = false;
        std::vector<double> refined{bounded.front()};
        for (std::size_t i = 1; i < bounded.size(); ++i) {
            const double a = refined.back();
            const double b = bounded[i];
            if (b - a >= 2.0 * kMinStitch &&
                (sagitta(line, a, b) > kMaxSagitta ||
                 !zone.segment_inside(point_at_u(line, a), point_at_u(line, b)))) {
                refined.push_back((a + b) / 2.0);
                changed = true;
            }
            refined.push_back(b);
        }
        bounded = std::move(refined);
        if (!changed) {
            break;
        }
    }

    out.reserve(bounded.size());
    for (const double u : bounded) {
        out.push_back(point_at_u(line, u));
    }
    return out;
}

// --- Assemblage ----------------------------------------------------------------

struct SectorLines {
    std::vector<Traced> traced;
    std::vector<std::vector<P2>> stitched; // lignes retenues (≥ 2 pénétrations)
};

std::vector<SectorLines> compute_lines(const std::vector<Sector>& sectors,
                                       const document::DirectionalFillParams& params,
                                       bool withStitches) {
    std::vector<SectorLines> result;
    const double dsep = clamp_spacing(params);
    StitchPlan plan;
    plan.target = static_cast<double>(params.stitch_length.value);
    plan.stagger = params.stagger;
    plan.intensity = handmade_intensity(params);
    plan.seed = params.seed;
    for (std::size_t si = 0; si < sectors.size(); ++si) {
        SectorLines sl;
        StreamlineTracer tracer(sectors[si], dsep);
        sl.traced = tracer.run();
        if (withStitches) {
            for (std::size_t li = 0; li < sl.traced.size(); ++li) {
                const auto key =
                    static_cast<std::int64_t>(si) * 1'000'000 + static_cast<std::int64_t>(li);
                auto pts = place_stitches(sl.traced[li], plan, key, sectors[si].trace);
                if (pts.size() >= 2) {
                    sl.stitched.push_back(std::move(pts));
                }
            }
        }
        result.push_back(std::move(sl));
    }
    return result;
}

} // namespace

std::vector<DirectionalStreamline>
trace_directional_streamlines(const geometry::PathSet& region,
                              const document::DirectionalFillParams& params) {
    std::vector<DirectionalStreamline> out;
    const auto sectors = build_sectors(region, params);
    const auto lines = compute_lines(sectors, params, false);
    for (std::size_t si = 0; si < lines.size(); ++si) {
        for (const auto& t : lines[si].traced) {
            DirectionalStreamline s;
            s.sector = si;
            s.points.reserve(t.pts.size());
            for (const P2 p : t.pts) {
                s.points.push_back(to_um(p));
            }
            out.push_back(std::move(s));
        }
    }
    return out;
}

std::vector<std::vector<Vec2um>>
directional_stitch_lines(const geometry::PathSet& region,
                         const document::DirectionalFillParams& params) {
    std::vector<std::vector<Vec2um>> out;
    const auto sectors = build_sectors(region, params);
    for (const auto& sl : compute_lines(sectors, params, true)) {
        for (const auto& line : sl.stitched) {
            std::vector<Vec2um> um;
            um.reserve(line.size());
            for (const P2 p : line) {
                um.push_back(to_um(p));
            }
            out.push_back(std::move(um));
        }
    }
    return out;
}

std::vector<FillStitch> fill_directional(const geometry::PathSet& region,
                                         const document::DirectionalFillParams& params) {
    std::vector<FillStitch> out;
    const auto sectors = build_sectors(region, params);
    if (sectors.empty()) {
        return out;
    }
    const auto lines = compute_lines(sectors, params, true);
    const double spacing = clamp_spacing(params);
    const double L =
        std::clamp(static_cast<double>(params.stitch_length.value), kMinTarget, kMaxTarget);
    // Liaison cousue « normale » (comme la liaison de rangée du tatami) si
    // courte et intérieure ; trajet caché jusqu'au plafond du tatami ; au-delà,
    // saut.
    const double sewMax = std::max(4.0 * spacing, 1'500.0);
    const double underpathCap = std::max(6.0 * spacing, 8'000.0);

    const auto polys = region_polys(region);
    const RegionIndex whole(polys, std::clamp(spacing, 250.0, 1'000.0));

    // Autoroute d'underpath : contour extérieur rentré (comme le tatami).
    std::vector<P2> highway;
    if (params.hidden_underpath) {
        if (auto r = geometry::inset_path_set(geometry::PathSet{region.outer, {}},
                                              params.underlay_inset);
            r && !r->empty() && r->front().outer.nodes.size() >= 3) {
            highway = flatten_closed(r->front().outer);
        }
    }
    const auto routeHighway = [&](P2 a, P2 b) -> std::vector<P2> {
        const int H = static_cast<int>(highway.size());
        if (H < 3) {
            return {};
        }
        const auto nearest = [&](P2 p) {
            int bi = 0;
            double bd = std::numeric_limits<double>::max();
            for (int i = 0; i < H; ++i) {
                const double d = dist2(p, highway[static_cast<std::size_t>(i)]);
                if (d < bd) {
                    bd = d;
                    bi = i;
                }
            }
            return bi;
        };
        const int ia = nearest(a);
        const int ib = nearest(b);
        std::vector<P2> bestPath;
        double bestLen = underpathCap;
        for (const bool fwd : {true, false}) {
            std::vector<P2> v{a};
            int i = ia;
            for (int guard = 0; guard <= H; ++guard) {
                v.push_back(highway[static_cast<std::size_t>(i)]);
                if (i == ib) {
                    break;
                }
                i = fwd ? (i + 1) % H : (i - 1 + H) % H;
            }
            v.push_back(b);
            double len = 0.0;
            bool ok = true;
            for (std::size_t k = 1; k < v.size(); ++k) {
                len += dist(v[k - 1], v[k]);
                if (len > bestLen || !whole.segment_inside(v[k - 1], v[k])) {
                    ok = false;
                    break;
                }
            }
            if (ok && len <= bestLen) {
                bestLen = len;
                bestPath = std::move(v);
            }
        }
        return bestPath;
    };
    const auto emitTravel = [&](const std::vector<P2>& path) {
        for (std::size_t k = 1; k < path.size(); ++k) {
            const double d = dist(path[k - 1], path[k]);
            const int steps = std::max(1, static_cast<int>(std::ceil(d / L)));
            const bool lastSeg = k + 1 == path.size();
            for (int t = 1; t <= steps; ++t) {
                if (lastSeg && t == steps) {
                    break; // le dernier point est la pénétration d'arrivée
                }
                const P2 ip =
                    path[k - 1] + (path[k] - path[k - 1]) * (static_cast<double>(t) / steps);
                out.push_back({to_um(ip), false, true});
            }
        }
    };

    // Ordre : glouton sur les extrémités les plus proches. Chaque secteur est
    // parcouru d'un seul tenant ; on passe au secteur dont une extrémité est
    // la plus proche de la position courante. Départ : l'extrémité la plus
    // basse (puis la plus à gauche) de toute la région.
    std::vector<char> sectorDone(lines.size(), 0);
    bool hasCur = false;
    P2 cur{};
    const auto endpointKey = [](P2 p) { return std::pair(p.y, p.x); };
    for (std::size_t done = 0; done < lines.size(); ++done) {
        std::size_t next = lines.size();
        double bestD = std::numeric_limits<double>::max();
        std::pair<double, double> bestKey{std::numeric_limits<double>::max(), 0.0};
        for (std::size_t si = 0; si < lines.size(); ++si) {
            if (sectorDone[si]) {
                continue;
            }
            for (const auto& l : lines[si].stitched) {
                for (const P2 e : {l.front(), l.back()}) {
                    if (hasCur) {
                        const double d = dist2(cur, e);
                        if (d < bestD) {
                            bestD = d;
                            next = si;
                        }
                    } else if (endpointKey(e) < bestKey) {
                        bestKey = endpointKey(e);
                        next = si;
                    }
                }
            }
        }
        if (next == lines.size()) {
            break; // secteurs restants sans ligne
        }
        sectorDone[next] = 1;
        const auto& stitched = lines[next].stitched;
        const std::size_t n = stitched.size();
        // Voisinage : deux lignes sont voisines si une extrémité de l'une est
        // à portée de liaison cousue d'une extrémité de l'autre.
        std::vector<std::vector<std::size_t>> neighbors(n);
        const double sew2 = sewMax * sewMax;
        for (std::size_t a = 0; a < n; ++a) {
            for (std::size_t b = a + 1; b < n; ++b) {
                bool close = false;
                for (const P2 ea : {stitched[a].front(), stitched[a].back()}) {
                    for (const P2 eb : {stitched[b].front(), stitched[b].back()}) {
                        close = close || dist2(ea, eb) <= sew2;
                    }
                }
                if (close) {
                    neighbors[a].push_back(b);
                    neighbors[b].push_back(a);
                }
            }
        }
        std::vector<char> visited(n, 0);
        const auto freeDegree = [&](std::size_t li) {
            return std::count_if(neighbors[li].begin(), neighbors[li].end(),
                                 [&](std::size_t o) { return visited[o] == 0; });
        };
        for (std::size_t count = 0; count < n; ++count) {
            // Ligne suivante. Parmi les liaisons COUSABLES (courtes et
            // intérieures), règle de Warnsdorff : d'abord la ligne qui a le
            // moins de voisines libres — une ligne courte coincée entre deux
            // autres est cousue au passage au lieu de rester orpheline (et
            // d'imposer un long saut à la fin). Sinon, extrémité la plus
            // proche. Égalités départagées par distance puis index.
            std::size_t best = n;
            bool bestReversed = false;
            bool bestSewable = false;
            std::ptrdiff_t bestDegree = 0;
            double bd = std::numeric_limits<double>::max();
            std::pair<double, double> bk{std::numeric_limits<double>::max(), 0.0};
            for (std::size_t li = 0; li < n; ++li) {
                if (visited[li]) {
                    continue;
                }
                const auto& l = stitched[li];
                for (const bool rev : {false, true}) {
                    const P2 e = rev ? l.back() : l.front();
                    if (!hasCur) {
                        // Tout premier départ : une ligne EXTRÊME du faisceau
                        // (le moins de voisines), jamais une ligne du milieu
                        // qui couperait le parcours en deux moitiés.
                        const auto degree = static_cast<std::ptrdiff_t>(neighbors[li].size());
                        if (best == n || degree < bestDegree ||
                            (degree == bestDegree && endpointKey(e) < bk)) {
                            bk = endpointKey(e);
                            best = li;
                            bestReversed = rev;
                            bestDegree = degree;
                        }
                        continue;
                    }
                    const double d = dist2(cur, e);
                    const bool sewable = d <= sew2 && whole.segment_inside(cur, e);
                    const std::ptrdiff_t degree = sewable ? freeDegree(li) : 0;
                    bool better = false;
                    if (sewable != bestSewable) {
                        better = sewable;
                    } else if (sewable && degree != bestDegree) {
                        better = degree < bestDegree;
                    } else {
                        better = d < bd;
                    }
                    if (best == n || better) {
                        best = li;
                        bestReversed = rev;
                        bestSewable = sewable;
                        bestDegree = degree;
                        bd = d;
                    }
                }
            }
            visited[best] = 1;
            std::vector<P2> pts = stitched[best];
            if (bestReversed) {
                std::reverse(pts.begin(), pts.end());
            }
            for (std::size_t k = 0; k < pts.size(); ++k) {
                const P2 rp = pts[k];
                bool jump = false;
                if (k == 0) {
                    if (!hasCur) {
                        jump = true;
                    } else {
                        const double d = dist(cur, rp);
                        const bool direct = whole.segment_inside(cur, rp);
                        if (d < 1.0) {
                            continue; // même position : rien à émettre
                        }
                        if (!(direct && d <= sewMax)) {
                            jump = true;
                            if (params.hidden_underpath) {
                                if (direct && d <= underpathCap) {
                                    emitTravel({cur, rp});
                                    jump = false;
                                } else if (auto route = routeHighway(cur, rp); !route.empty()) {
                                    emitTravel(route);
                                    jump = false;
                                }
                            }
                        }
                    }
                }
                out.push_back({to_um(rp), jump, false});
                cur = rp;
                hasCur = true;
            }
        }
    }
    return out;
}

std::vector<std::vector<Vec2um>>
directional_underlay(const geometry::PathSet& region,
                     const document::DirectionalFillParams& params) {
    if (!params.underlay_edge && !params.underlay_parallel) {
        return {};
    }
    document::TatamiParams tp;
    tp.row_spacing = params.row_spacing;
    tp.stitch_length = Micrometers{static_cast<std::int32_t>(std::lround(
        std::clamp(static_cast<double>(params.stitch_length.value), kMinTarget, kMaxTarget)))};
    tp.inset = Micrometers{0};
    tp.underlay_edge = params.underlay_edge;
    tp.underlay_parallel = params.underlay_parallel;
    tp.underlay_inset = params.underlay_inset;
    tp.underlay_spacing = params.underlay_spacing;
    // Direction MOYENNE du champ (moyenne des vecteurs doublés sur une grille
    // intérieure) : `tatami_underlay` pose ses rangées à +90° de cet angle.
    if (params.underlay_parallel) {
        const auto sectors = build_sectors(region, params);
        P2 acc{0.0, 0.0};
        const double step = std::max(1'000.0, clamp_spacing(params));
        for (const auto& s : sectors) {
            P2 lo;
            P2 hi;
            bounds(s.polys, lo, hi);
            for (double y = lo.y + step / 2.0; y < hi.y; y += step) {
                for (double x = lo.x + step / 2.0; x < hi.x; x += step) {
                    if (s.own.inside({x, y})) {
                        acc = acc + s.field.doubled_at({x, y});
                    }
                }
            }
        }
        tp.angle = Angle{norm(acc) > 1e-9 ? 0.5 * std::atan2(acc.y, acc.x) : 0.0};
    }
    return tatami_underlay(region, tp);
}

std::vector<std::optional<Angle>>
directional_field_at(const geometry::PathSet& region, const document::DirectionalFillParams& params,
                     const std::vector<Vec2um>& points) {
    std::vector<std::optional<Angle>> out;
    out.reserve(points.size());
    const auto sectors = build_sectors(region, params);
    for (const Vec2um v : points) {
        const P2 p = to_p2(v);
        std::optional<Angle> a;
        for (const auto& s : sectors) {
            if (s.own.inside(p)) {
                double r = std::fmod(s.field.angle(p), kPi);
                if (r < 0.0) {
                    r += kPi;
                }
                a = Angle{r};
                break;
            }
        }
        out.push_back(a);
    }
    return out;
}

std::vector<DirectionTick> directional_field_preview(const geometry::PathSet& region,
                                                     const document::DirectionalFillParams& params,
                                                     Micrometers step) {
    std::vector<DirectionTick> out;
    const double g = static_cast<double>(std::max<std::int32_t>(200, step.value));
    const auto sectors = build_sectors(region, params);
    for (const auto& s : sectors) {
        P2 lo;
        P2 hi;
        bounds(s.polys, lo, hi);
        // Grille alignée sur des multiples de `step` : l'aperçu ne « glisse »
        // pas quand la forme change légèrement.
        for (double y = std::ceil(lo.y / g) * g; y < hi.y; y += g) {
            for (double x = std::ceil(lo.x / g) * g; x < hi.x; x += g) {
                const P2 p{x, y};
                if (!s.own.inside(p)) {
                    continue;
                }
                double r = std::fmod(s.field.angle(p), kPi);
                if (r < 0.0) {
                    r += kPi;
                }
                out.push_back({to_um(p), Angle{r}});
            }
        }
    }
    return out;
}

document::DirectionalFillParams directional_from_tatami(const document::TatamiParams& tatami,
                                                        const std::vector<geometry::PathSet>& shape,
                                                        std::uint32_t seed) {
    document::DirectionalFillParams d;
    d.row_spacing = tatami.row_spacing;
    d.stitch_length = Micrometers{static_cast<std::int32_t>(std::lround(
        std::clamp(static_cast<double>(tatami.stitch_length.value), kMinTarget, kMaxTarget)))};
    d.inset = tatami.inset;
    d.stagger = tatami.stagger;
    d.underlay_edge = tatami.underlay_edge;
    d.underlay_parallel = tatami.underlay_parallel;
    d.underlay_inset = tatami.underlay_inset;
    d.underlay_spacing = tatami.underlay_spacing;
    d.hidden_underpath = tatami.hidden_underpath;
    d.seed = seed;

    std::vector<P2> nodes;
    for (const auto& set : shape) {
        for (const auto& n : set.outer.nodes) {
            nodes.push_back(to_p2(n.pos));
        }
    }
    if (nodes.empty()) {
        return d;
    }
    P2 lo = nodes.front();
    P2 hi = nodes.front();
    for (const P2 p : nodes) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
    }
    const P2 c = (lo + hi) * 0.5;
    const P2 dir = from_angle(tatami.angle.radians);
    double pmin = 0.0;
    double pmax = 0.0;
    for (const P2 p : nodes) {
        const double t = dot(p - c, dir);
        pmin = std::min(pmin, t);
        pmax = std::max(pmax, t);
    }
    if (pmax - pmin < 1'000.0) {
        return d; // forme dégénérée : repli sur l'axe principal
    }
    geometry::Path guide;
    guide.closed = false;
    guide.nodes = {{to_um(c + dir * (0.8 * pmin)), geometry::NodeType::Corner, {}, {}},
                   {to_um(c + dir * (0.8 * pmax)), geometry::NodeType::Corner, {}, {}}};
    d.guides.push_back(std::move(guide));
    return d;
}

} // namespace openstitch::stitch_generation
