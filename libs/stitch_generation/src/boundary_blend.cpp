// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/boundary_blend.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <future>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <numbers>
#include <set>
#include <utility>
#include <vector>

#include "openstitch/document/boundary_blend.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/frontier.hpp"
#include "openstitch/geometry/offset.hpp"
#include "openstitch/geometry/polyline.hpp"
#include "openstitch/stitch_generation/directional_fill.hpp"
#include "openstitch/stitch_generation/pull_auto.hpp"
#include "openstitch/stitch_generation/row_mask.hpp"

#include "chunk_cache.hpp"

namespace openstitch::stitch_generation {

namespace {

constexpr std::int32_t kMinWidth = 500;    // 0,5 mm
constexpr std::int32_t kMaxWidth = 15'000; // 15 mm

double seg_distance(Vec2um p, Vec2um a, Vec2um b) {
    const double px = p.x.value;
    const double py = p.y.value;
    const double ax = a.x.value;
    const double ay = a.y.value;
    const double dx = static_cast<double>(b.x.value) - ax;
    const double dy = static_cast<double>(b.y.value) - ay;
    const double len2 = dx * dx + dy * dy;
    double t = len2 > 0.0 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    const double qx = ax + t * dx - px;
    const double qy = ay + t * dy - py;
    return std::sqrt(qx * qx + qy * qy);
}

// --- Index spatial du champ (performance, résultats identiques au bit près) ----------------------

// Test pair-impair (rayon horizontal vers +x) sur des contours fermés, accéléré par tranches
// horizontales : une arête ne peut basculer la parité qu'à une ordonnée de [min(ay, by) ;
// max(ay, by)) ; chaque tranche liste les arêtes dont l'intervalle la touche. La parité ne
// dépend pas de l'ordre des arêtes et chaque arête est évaluée par la même expression que le
// parcours complet : même verdict, au bit près.
class EvenOddIndex {
public:
    void add_polygon(const std::vector<Vec2um>& poly) {
        const std::size_t m = poly.size();
        for (std::size_t i = 0, j = m - 1; i < m; j = i++) {
            const Edge e{static_cast<double>(poly[i].x.value), static_cast<double>(poly[i].y.value),
                         static_cast<double>(poly[j].x.value),
                         static_cast<double>(poly[j].y.value)};
            if (e.ay != e.by) { // horizontale : ne bascule jamais la parité
                edges_.push_back(e);
            }
        }
    }

    // À appeler une fois tous les contours ajoutés.
    void build() {
        if (edges_.empty()) {
            return;
        }
        double y0 = std::numeric_limits<double>::max();
        double y1 = std::numeric_limits<double>::lowest();
        for (const Edge& e : edges_) {
            y0 = std::min({y0, e.ay, e.by});
            y1 = std::max({y1, e.ay, e.by});
        }
        y0_ = y0;
        // Environ deux arêtes par tranche en moyenne, au plus 1 << 16 tranches.
        const std::size_t want = std::clamp<std::size_t>(edges_.size() / 2, 1, 1U << 16);
        slab_ = std::max(1.0, (y1 - y0) / static_cast<double>(want));
        slabs_ = static_cast<std::size_t>(std::floor((y1 - y0) / slab_)) + 1;
        std::vector<std::uint32_t> counts(slabs_ + 1, 0);
        for (const Edge& e : edges_) {
            const std::size_t lo = slab_of(std::min(e.ay, e.by));
            const std::size_t hi = slab_of(std::max(e.ay, e.by));
            for (std::size_t s = lo; s <= hi; ++s) {
                ++counts[s + 1];
            }
        }
        for (std::size_t s = 0; s < slabs_; ++s) {
            counts[s + 1] += counts[s];
        }
        start_ = counts;
        members_.resize(start_.back());
        for (std::uint32_t k = 0; k < edges_.size(); ++k) {
            const Edge& e = edges_[k];
            const std::size_t lo = slab_of(std::min(e.ay, e.by));
            const std::size_t hi = slab_of(std::max(e.ay, e.by));
            for (std::size_t s = lo; s <= hi; ++s) {
                members_[counts[s]++] = k;
            }
        }
    }

    [[nodiscard]] bool contains(Vec2um p) const {
        const double px = p.x.value;
        const double py = p.y.value;
        if (slabs_ == 0 || py < y0_) {
            return false;
        }
        const double rel = std::floor((py - y0_) / slab_);
        if (rel >= static_cast<double>(slabs_)) {
            return false; // au-dessus de toutes les arêtes : aucune ne bascule la parité
        }
        const auto s = static_cast<std::size_t>(rel);
        bool inside = false;
        for (std::uint32_t k = start_[s]; k < start_[s + 1]; ++k) {
            const Edge& e = edges_[members_[k]];
            if ((e.ay > py) != (e.by > py) &&
                px < (e.bx - e.ax) * (py - e.ay) / (e.by - e.ay) + e.ax) {
                inside = !inside;
            }
        }
        return inside;
    }

private:
    struct Edge {
        double ax, ay, bx, by; // a = sommet i, b = sommet j (même convention que le parcours)
    };

    [[nodiscard]] std::size_t slab_of(double y) const {
        return std::min(slabs_ - 1, static_cast<std::size_t>(std::floor((y - y0_) / slab_)));
    }

    std::vector<Edge> edges_;
    double y0_{0.0};
    double slab_{1.0};
    std::size_t slabs_{0};
    std::vector<std::uint32_t> start_;
    std::vector<std::uint32_t> members_;
};

// Segments de la frontière rangés dans une grille uniforme couvrant la boîte du champ (cases
// d'environ un segment, chaque segment dans toutes les cases de sa boîte). Distance minimale par
// anneaux de cases autour du point : un segment absent des anneaux 0..k n'a aucun point dans ces
// cases, donc est à plus de k × case du point ; on s'arrête dès que le meilleur candidat est plus
// proche (marge de 1 µm contre l'arrondi). Le minimum de valeurs calculées par la même fonction
// ne dépend pas de l'ordre : même résultat que le parcours complet, au bit près.
class SegmentGrid {
public:
    struct Segment {
        Vec2um a;
        Vec2um b;
        // Précalculés pour `boundary_blend_cross_angle`, par les mêmes expressions : longueur,
        // cos et sin du double de la direction.
        double len{0.0};
        double cos2{0.0};
        double sin2{0.0};
    };

    // `margin` : élargissement de la grille autour de la boîte de la frontière (µm).
    SegmentGrid(const std::vector<geometry::Polyline>& lines, double margin) {
        double x0 = std::numeric_limits<double>::max();
        double y0 = x0;
        double x1 = std::numeric_limits<double>::lowest();
        double y1 = x1;
        double total = 0.0;
        for (const auto& line : lines) {
            for (std::size_t i = 1; i < line.points.size(); ++i) {
                Segment s{line.points[i - 1], line.points[i]};
                const double dx = static_cast<double>(s.b.x.value) - s.a.x.value;
                const double dy = static_cast<double>(s.b.y.value) - s.a.y.value;
                s.len = std::hypot(dx, dy);
                const double theta = std::atan2(dy, dx);
                s.cos2 = std::cos(2.0 * theta);
                s.sin2 = std::sin(2.0 * theta);
                segments_.push_back(s);
                total += s.len;
            }
            for (const Vec2um p : line.points) {
                x0 = std::min(x0, static_cast<double>(p.x.value));
                y0 = std::min(y0, static_cast<double>(p.y.value));
                x1 = std::max(x1, static_cast<double>(p.x.value));
                y1 = std::max(y1, static_cast<double>(p.y.value));
            }
        }
        if (segments_.empty()) {
            return;
        }
        const double n = static_cast<double>(segments_.size());
        const double fw = std::max(1.0, x1 - x0);
        const double fh = std::max(1.0, y1 - y0);
        // Case ≈ une par segment sur la boîte de la frontière, jamais plus fine qu'un segment
        // moyen ni que 50 µm ; grille élargie de la marge bornée à ~4 cases par segment.
        cell_ = std::max({50.0, std::sqrt(fw * fh / n), total / n});
        x0_ = x0 - margin;
        y0_ = y0 - margin;
        const double w = fw + 2.0 * margin;
        const double h = fh + 2.0 * margin;
        while ((w / cell_ + 1.0) * (h / cell_ + 1.0) > 4.0 * n + 256.0) {
            cell_ *= 1.25;
        }
        nx_ = static_cast<std::int64_t>(std::floor(w / cell_)) + 1;
        ny_ = static_cast<std::int64_t>(std::floor(h / cell_)) + 1;
        std::vector<std::uint32_t> counts(static_cast<std::size_t>(nx_ * ny_) + 1, 0);
        const auto forCells = [&](const Segment& s, auto&& f) {
            const std::int64_t cx0 = clamped(std::min(s.a.x.value, s.b.x.value), x0_, nx_);
            const std::int64_t cx1 = clamped(std::max(s.a.x.value, s.b.x.value), x0_, nx_);
            const std::int64_t cy0 = clamped(std::min(s.a.y.value, s.b.y.value), y0_, ny_);
            const std::int64_t cy1 = clamped(std::max(s.a.y.value, s.b.y.value), y0_, ny_);
            for (std::int64_t cy = cy0; cy <= cy1; ++cy) {
                for (std::int64_t cx = cx0; cx <= cx1; ++cx) {
                    f(static_cast<std::size_t>(cy * nx_ + cx));
                }
            }
        };
        for (const Segment& s : segments_) {
            forCells(s, [&](std::size_t c) { ++counts[c + 1]; });
        }
        for (std::size_t c = 0; c + 1 < counts.size(); ++c) {
            counts[c + 1] += counts[c];
        }
        start_ = counts;
        members_.resize(start_.back());
        for (std::uint32_t k = 0; k < segments_.size(); ++k) {
            forCells(segments_[k], [&](std::size_t c) { members_[counts[c]++] = k; });
        }
    }

    [[nodiscard]] const std::vector<Segment>& segments() const { return segments_; }

    // Plus petite distance de p aux segments (max() si aucun) : même valeur que le parcours
    // complet par `seg_distance`.
    [[nodiscard]] double min_distance(Vec2um p) const {
        double best = std::numeric_limits<double>::max();
        const auto [cx, cy] = cell_of(p);
        if (!in_grid(cx, cy)) {
            for (const Segment& s : segments_) {
                best = std::min(best, seg_distance(p, s.a, s.b));
            }
            return best;
        }
        const std::int64_t kmax = std::max({cx, cy, nx_ - 1 - cx, ny_ - 1 - cy});
        for (std::int64_t k = 0; k <= kmax; ++k) {
            ring(cx, cy, k, [&](std::uint32_t idx) {
                const Segment& s = segments_[idx];
                best = std::min(best, seg_distance(p, s.a, s.b));
            });
            if (best <= static_cast<double>(k) * cell_ - 1.0) {
                break;
            }
        }
        return best;
    }

    // Indices (croissants, sans doublon) d'un sur-ensemble des segments à distance <= r de p.
    // Faux si p est hors de la grille : l'appelant parcourt alors tous les segments.
    [[nodiscard]] bool near(Vec2um p, double r, std::vector<std::uint32_t>& out) const {
        out.clear();
        const auto [cx, cy] = cell_of(p);
        if (!in_grid(cx, cy)) {
            return false;
        }
        const auto reach = static_cast<std::int64_t>(std::floor((r + 1.0) / cell_)) + 1;
        for (std::int64_t yy = std::max<std::int64_t>(0, cy - reach);
             yy <= std::min(ny_ - 1, cy + reach); ++yy) {
            for (std::int64_t xx = std::max<std::int64_t>(0, cx - reach);
                 xx <= std::min(nx_ - 1, cx + reach); ++xx) {
                const auto c = static_cast<std::size_t>(yy * nx_ + xx);
                out.insert(out.end(), members_.begin() + start_[c],
                           members_.begin() + start_[c + 1]);
            }
        }
        std::ranges::sort(out);
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return true;
    }

private:
    [[nodiscard]] std::int64_t clamped(std::int32_t v, double origin, std::int64_t count) const {
        return std::clamp<std::int64_t>(static_cast<std::int64_t>(std::floor((v - origin) / cell_)),
                                        0, count - 1);
    }
    [[nodiscard]] bool in_grid(std::int64_t cx, std::int64_t cy) const {
        return !segments_.empty() && cx >= 0 && cy >= 0 && cx < nx_ && cy < ny_;
    }
    // Case de p sans bornage (hors grille possible).
    [[nodiscard]] std::pair<std::int64_t, std::int64_t> cell_of(Vec2um p) const {
        constexpr double kLim = 1e12;
        const double fx = std::clamp(std::floor((p.x.value - x0_) / cell_), -kLim, kLim);
        const double fy = std::clamp(std::floor((p.y.value - y0_) / cell_), -kLim, kLim);
        return {static_cast<std::int64_t>(fx), static_cast<std::int64_t>(fy)};
    }

    // Cases à distance de Tchebychev exactement k de (cx, cy), dans la grille.
    template <class F> void ring(std::int64_t cx, std::int64_t cy, std::int64_t k, F&& f) const {
        const auto visit = [&](std::int64_t xx, std::int64_t yy) {
            if (xx < 0 || yy < 0 || xx >= nx_ || yy >= ny_) {
                return;
            }
            const auto c = static_cast<std::size_t>(yy * nx_ + xx);
            for (std::uint32_t m = start_[c]; m < start_[c + 1]; ++m) {
                f(members_[m]);
            }
        };
        if (k == 0) {
            visit(cx, cy);
            return;
        }
        for (std::int64_t xx = cx - k; xx <= cx + k; ++xx) {
            visit(xx, cy - k);
            visit(xx, cy + k);
        }
        for (std::int64_t yy = cy - k + 1; yy <= cy + k - 1; ++yy) {
            visit(cx - k, yy);
            visit(cx + k, yy);
        }
    }

    std::vector<Segment> segments_;
    double x0_{0.0};
    double y0_{0.0};
    double cell_{1.0};
    std::int64_t nx_{0};
    std::int64_t ny_{0};
    std::vector<std::uint32_t> start_;
    std::vector<std::uint32_t> members_;
};

// --- Cache des plans et des couches (performance) -----------------------------------------------
//
// Les deux zones d'un couple génèrent chacune leur couche à partir du MÊME calcul (plan et deux
// couches, sens canonique) ; sans cache, chaque zone le refaisait entièrement et jetait la moitié
// du résultat, et toute régénération du document recalculait tous les fondus. Le résultat est une
// fonction pure de la clé (zones cousues, paramètres résolus, réglage du couple) : clé EXACTE,
// comparée intégralement (operator== des types du document), jamais une empreinte. Une zone qui
// demande un calcul déjà en cours dans un autre fil attend son résultat au lieu de le refaire.
// Taille bornée (LRU) ; suit `chunk_cache_enabled()` et se vide avec `clear_chunk_cache()`.
template <class Key, class Value, std::size_t Capacity> class ExactCache {
public:
    template <class Compute> std::shared_ptr<const Value> get(const Key& key, Compute&& compute) {
        if (!chunk_cache_enabled()) {
            return std::make_shared<const Value>(compute());
        }
        std::promise<std::shared_ptr<const Value>> promise;
        std::shared_future<std::shared_ptr<const Value>> future;
        bool owner = false;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            const auto it =
                std::ranges::find_if(entries_, [&key](const Entry& e) { return e.key == key; });
            if (it != entries_.end()) {
                entries_.splice(entries_.begin(), entries_, it);
                future = it->value;
            } else {
                future = promise.get_future().share();
                entries_.push_front(Entry{key, future});
                if (entries_.size() > Capacity) {
                    entries_.pop_back(); // une attente en cours garde sa propre copie du futur
                }
                owner = true;
            }
        }
        if (owner) {
            try {
                promise.set_value(std::make_shared<const Value>(compute()));
            } catch (...) {
                promise.set_exception(std::current_exception());
                const std::lock_guard<std::mutex> lock(mutex_);
                std::erase_if(entries_, [&key](const Entry& e) { return e.key == key; });
            }
        }
        return future.get();
    }

    void clear() {
        const std::lock_guard<std::mutex> lock(mutex_);
        entries_.clear();
    }

private:
    struct Entry {
        Key key;
        std::shared_future<std::shared_ptr<const Value>> value;
    };
    std::mutex mutex_;
    std::list<Entry> entries_; // front = plus récent
};

struct FillsKey {
    std::vector<geometry::PathSet> zones_a;
    BoundaryBlendFillParams params_a;
    std::vector<geometry::PathSet> zones_b;
    BoundaryBlendFillParams params_b;
    BoundaryBlendSpec spec;

    bool operator==(const FillsKey&) const = default;
};

struct PlanKey {
    std::vector<geometry::PathSet> zones_a;
    std::vector<geometry::PathSet> zones_b;
    BoundaryBlendSpec spec;
    Micrometers margin;

    bool operator==(const PlanKey&) const = default;
};

ExactCache<FillsKey, Result<BoundaryBlendFills>, 32>& fills_cache() {
    static ExactCache<FillsKey, Result<BoundaryBlendFills>, 32> instance;
    return instance;
}

ExactCache<PlanKey, Result<BoundaryBlendPlan>, 64>& plan_cache() {
    static ExactCache<PlanKey, Result<BoundaryBlendPlan>, 64> instance;
    return instance;
}

// Bande autour de la frontière (`geometry::polyline_band`, coûteuse sur une longue frontière
// ondulée : décalage arrondi puis réunion de ses recouvrements). Un fondu en demande plusieurs
// fois la même (plan, découpe de la bande, sous-couche, chaque zone du couple) : mémorisée par
// (frontière, demi-largeur), clé exacte.
struct BandKey {
    std::vector<geometry::Polyline> lines;
    Micrometers width;

    bool operator==(const BandKey& o) const {
        return width == o.width &&
               std::ranges::equal(lines, o.lines,
                                  [](const geometry::Polyline& l, const geometry::Polyline& r) {
                                      return l.closed == r.closed && l.points == r.points;
                                  });
    }
};

ExactCache<BandKey, Result<std::vector<geometry::PathSet>>, 64>& band_cache() {
    static ExactCache<BandKey, Result<std::vector<geometry::PathSet>>, 64> instance;
    return instance;
}

Result<std::vector<geometry::PathSet>> cached_band(const std::vector<geometry::Polyline>& lines,
                                                   Micrometers half_width) {
    return *band_cache().get(BandKey{lines, half_width},
                             [&] { return geometry::polyline_band(lines, half_width); });
}

// Densité minimale d'une couche dans la bande : en dessous, la couche ne coud rien (au-dessus de
// 1 - kMinLayerDensity, elle coud tout). Une rangée gardée sur ≈ 1/d : sans plancher, une couche
// presque vide relie des rangées distantes de plusieurs millimètres par de longues liaisons
// visibles (recette Studio). Avec 0,2, au plus 5 rangées sautées (cf. `kMaskedRowGap`).
constexpr double kMinLayerDensity = 0.2;
// Superposition (grilles différentes) : chaque couche est cousue un peu plus dense que sa part,
// sinon les creux des deux couches indépendantes laissent le tissu nu par endroits.
constexpr double kOverlapBoost = 1.5;
// Mode Interlaced : les rangées en travers d'une frontière courbe s'écartent en éventail du côté
// convexe. Le traceur sème une nouvelle rangée dès que l'écart dépasse 2 × kInterlacedFillSeed ×
// l'écart voulu (au lieu de 1,8 ×) : densité qui s'adapte localement, sans jour entre rangées.
// Écart nominal : fraction du plus petit écart des deux zones ; régularité d'écart du traceur.
constexpr double kInterlacedFillSeed = 0.5;
constexpr double kInterlacedSpacing = 1.0;
constexpr double kInterlacedRegularity = 1.0;

double floored_density(double dA) {
    if (dA < kMinLayerDensity) {
        return 0.0;
    }
    if (dA > 1.0 - kMinLayerDensity) {
        return 1.0;
    }
    return dA;
}

// Masque d'une des deux couches : hors de la boîte de la frontière, le point est dans le cœur
// de la zone de la couche (la prolongation ne sort pas de la bande) : aplat.
class BoundaryRowMask final : public RowMask {
public:
    // `floored` : plancher de densité (cf. `kMinLayerDensity`). Sans objet en mode Interlaced :
    // les rangées traversent la bande, une couche clairsemée n'y relie aucune rangée lointaine, et
    // le plancher alignerait les bouts de 20 % des rangées sur une même ligne.
    BoundaryRowMask(const BoundaryBlendField& field, bool sideA, bool overlap, bool floored = true)
        : field_(field), sideA_(sideA), overlap_(overlap), floored_(floored) {}

    [[nodiscard]] bool keeps(Vec2um p, double threshold) const override {
        if (!field_.near_frontier(p)) {
            return true;
        }
        const double dA = density(p);
        if (overlap_) {
            // Superposition : chaque couche garde la tranche basse de sa part renforcée.
            const double own = sideA_ ? dA : 1.0 - dA;
            return own > 0.0 && threshold < std::min(1.0, own * kOverlapBoost);
        }
        // Partition exacte : A garde [0 ; d_A), B [d_A ; 1) (largeur d_B = 1 - d_A).
        return sideA_ ? threshold < dA : threshold >= dA;
    }

    [[nodiscard]] double coverage(Vec2um p) const override {
        if (!field_.near_frontier(p)) {
            return 1.0;
        }
        const double dA = density(p);
        const double own = sideA_ ? dA : 1.0 - dA;
        return overlap_ && own > 0.0 ? std::min(1.0, own * kOverlapBoost) : own;
    }

private:
    [[nodiscard]] double density(Vec2um p) const {
        const double dA = field_.density_a(p);
        return floored_ ? floored_density(dA) : dA;
    }

    const BoundaryBlendField& field_;
    bool sideA_;
    bool overlap_;
    bool floored_;
};

// Les deux couches partagent la même grille de rangées (ancrage absolu) : tatamis de même angle
// et même écart. Sinon (angles, écarts différents, ou un remplissage directionnel) : superposition.
bool same_row_grid(const BoundaryBlendFillParams& a, const BoundaryBlendFillParams& b) {
    const auto* ta = std::get_if<document::TatamiParams>(&a);
    const auto* tb = std::get_if<document::TatamiParams>(&b);
    return ta != nullptr && tb != nullptr && ta->row_spacing == tb->row_spacing &&
           std::abs(ta->angle.radians - tb->angle.radians) < 1e-9;
}

Result<std::vector<geometry::PathSet>> extended(const std::vector<geometry::PathSet>& own,
                                                const std::vector<geometry::PathSet>& other,
                                                const std::vector<geometry::Polyline>& frontier,
                                                Micrometers otherWidth, Micrometers tolerance) {
    auto band = cached_band(frontier, otherWidth);
    if (!band) {
        return std::unexpected(band.error());
    }
    // Les zones reçues sont rentrées de leur retrait de bord : un couloir de tissu sépare A et B.
    // La fermeture de A ∪ B (écart <= tolérance de contact) le comble, sinon aucune des deux
    // couches ne le coud et un filet de tissu nu suit la frontière (recette Studio).
    std::vector<geometry::PathSet> both = own;
    both.insert(both.end(), other.begin(), other.end());
    auto merged = geometry::union_polygons_closing(both, tolerance);
    if (!merged) {
        return std::unexpected(merged.error());
    }
    auto reach = geometry::intersect_polygons(*merged, *band);
    if (!reach) {
        return std::unexpected(reach.error());
    }
    std::vector<geometry::PathSet> parts = own;
    parts.insert(parts.end(), reach->begin(), reach->end());
    return geometry::union_polygons(parts);
}

void append_fill(std::vector<FillStitch>& out, std::vector<FillStitch> part) {
    if (part.empty()) {
        return;
    }
    if (!out.empty()) {
        part.front().jump = true;
        part.front().travel = false;
    }
    out.insert(out.end(), part.begin(), part.end());
}

// --- Sens des rangées dans la bande (§6.3) ---------------------------------------------------

// Écart d'angles de rangée ramené dans ]-π/2 ; π/2] (une rangée et son opposée sont la même).
double wrap_half_turn(double d) {
    constexpr double kPi = std::numbers::pi;
    double r = std::fmod(d, kPi);
    if (r > 0.5 * kPi) {
        r -= kPi;
    } else if (r <= -0.5 * kPi) {
        r += kPi;
    }
    return r;
}

// Découpe d'un fondu Shared / Rotating : cœur de chaque zone (privé de sa demi-bande) et bande
// commune (élargie du recouvrement sur les cœurs), cousue par les deux couches.
struct BandSplit {
    std::vector<geometry::PathSet> core_a;
    std::vector<geometry::PathSet> core_b;
    std::vector<geometry::PathSet> band;
};

Result<BandSplit> split_band(const std::vector<geometry::PathSet>& a,
                             const std::vector<geometry::PathSet>& b,
                             const std::vector<geometry::Polyline>& frontier,
                             const BoundaryBlendSpec& spec) {
    const std::int32_t wA = std::clamp(spec.width_a.value, kMinWidth, kMaxWidth);
    const std::int32_t wB = std::clamp(spec.width_b.value, kMinWidth, kMaxWidth);
    auto bandA = cached_band(frontier, Micrometers{wA});
    auto bandB = cached_band(frontier, Micrometers{wB});
    auto growA = cached_band(frontier, Micrometers{wA + kBoundaryBlendSeamOverlap});
    auto growB = cached_band(frontier, Micrometers{wB + kBoundaryBlendSeamOverlap});
    if (!bandA) {
        return std::unexpected(bandA.error());
    }
    if (!bandB) {
        return std::unexpected(bandB.error());
    }
    if (!growA) {
        return std::unexpected(growA.error());
    }
    if (!growB) {
        return std::unexpected(growB.error());
    }
    BandSplit split;
    auto coreA = geometry::difference_polygons(a, *bandA);
    if (!coreA) {
        return std::unexpected(coreA.error());
    }
    auto coreB = geometry::difference_polygons(b, *bandB);
    if (!coreB) {
        return std::unexpected(coreB.error());
    }
    split.core_a = std::move(*coreA);
    split.core_b = std::move(*coreB);
    // Côté B : fermeture de A ∪ B privée de A (B et le couloir de retrait entre les zones).
    std::vector<geometry::PathSet> both = a;
    both.insert(both.end(), b.begin(), b.end());
    auto merged = geometry::union_polygons_closing(both, spec.tolerance);
    if (!merged) {
        return std::unexpected(merged.error());
    }
    auto restB = geometry::difference_polygons(*merged, a);
    if (!restB) {
        return std::unexpected(restB.error());
    }
    auto partA = geometry::intersect_polygons(a, *growA);
    if (!partA) {
        return std::unexpected(partA.error());
    }
    auto partB = geometry::intersect_polygons(*restB, *growB);
    if (!partB) {
        return std::unexpected(partB.error());
    }
    std::vector<geometry::PathSet> parts = std::move(*partA);
    parts.insert(parts.end(), partB->begin(), partB->end());
    auto band = geometry::union_polygons(parts);
    if (!band) {
        return std::unexpected(band.error());
    }
    split.band = std::move(*band);
    return split;
}

// Remplissage de la bande commune : tatami à l'angle commun (Shared), directionnel à champ
// pivotant (Rotating) ou en travers de la frontière (Interlaced), mêmes rangées pour les deux
// couches quel que soit le masque.
class BandFiller {
public:
    BandFiller(std::shared_ptr<const BoundaryBlendField> field, const document::TatamiParams& pa,
               const document::TatamiParams& pb, document::BoundaryBlendRows rows)
        : field_(std::move(field)), rows_(rows), angleA_(pa.angle.radians),
          angleB_(pb.angle.radians), tatami_(pa) {
        tatami_.angle = Angle{boundary_blend_shared_angle(angleA_, angleB_)};
        tatami_.row_spacing = Micrometers{std::min(pa.row_spacing.value, pb.row_spacing.value)};
        tatami_.entry_point.reset();
        directional_.row_spacing = tatami_.row_spacing;
        directional_.stitch_length = pa.stitch_length;
        directional_.stagger = pa.stagger;
        directional_.inset = Micrometers{0};
        directional_.pull_compensation = pa.pull_compensation;
        directional_.hidden_underpath = false;
        if (rows == document::BoundaryBlendRows::Interlaced) {
            // Rangées en travers d'une frontière courbe : cf. `kInterlacedSpacing`.
            directional_.row_spacing = Micrometers{static_cast<std::int32_t>(
                std::lround(kInterlacedSpacing * directional_.row_spacing.value))};
            directional_.spacing_regularity = kInterlacedRegularity;
        }
    }

    [[nodiscard]] std::vector<FillStitch> fill(const geometry::PathSet& band,
                                               const RowMask& mask) const {
        const BoundaryBlendField& field = *field_;
        if (rows_ == document::BoundaryBlendRows::Interlaced) {
            return fill_directional_field(
                band, directional_,
                [&field](Vec2um p) { return boundary_blend_cross_angle(field, p); }, &mask,
                kInterlacedFillSeed);
        }
        if (rows_ != document::BoundaryBlendRows::Rotating) {
            return fill_tatami(band, tatami_, &mask);
        }
        const double a = angleA_;
        const double b = angleB_;
        return fill_directional_field(
            band, directional_,
            [&field, a, b](Vec2um p) { return boundary_blend_row_angle(field, a, b, p); }, &mask);
    }

private:
    std::shared_ptr<const BoundaryBlendField> field_;
    document::BoundaryBlendRows rows_;
    double angleA_;
    double angleB_;
    document::TatamiParams tatami_;
    document::DirectionalFillParams directional_;
};

Micrometers blend_margin(const BoundaryBlendFillParams& a, const BoundaryBlendFillParams& b) {
    const auto stitchLength = [](const BoundaryBlendFillParams& p) {
        return std::visit([](const auto& v) { return v.stitch_length.value; }, p);
    };
    // Une case de pénétrations au-delà de la demi-bande (cf. `plan_boundary_blend`), et au moins
    // le recouvrement cœur-bande : la bande commune reste dans la boîte du champ.
    return Micrometers{std::max({stitchLength(a), stitchLength(b), kBoundaryBlendSeamOverlap})};
}

} // namespace

void clear_boundary_blend_cache() {
    fills_cache().clear();
    plan_cache().clear();
    band_cache().clear();
}

// Pair-impair sur A (extérieur et trous, aplatie à 20 µm) et grille des segments de la frontière.
struct BoundaryBlendField::Index {
    Index(const std::vector<geometry::Polyline>& lines, double margin) : frontier(lines, margin) {}

    EvenOddIndex inside_a;
    SegmentGrid frontier;
};

double boundary_blend_shared_angle(double angle_a, double angle_b) {
    return angle_a + 0.5 * wrap_half_turn(angle_b - angle_a);
}

double boundary_blend_row_angle(const BoundaryBlendField& field, double angle_a, double angle_b,
                                Vec2um p) {
    double t = 0.0;
    if (field.near_frontier(p)) {
        const double s = field.signed_distance(p);
        t = std::clamp((s + field.width_a()) / (field.width_a() + field.width_b()), 0.0, 1.0);
    } else {
        t = field.density_a(p) >= 0.5 ? 0.0 : 1.0; // aplat : cœur de A ou de B
    }
    const double u = t * t * (3.0 - 2.0 * t);
    return angle_a + wrap_half_turn(angle_b - angle_a) * u;
}

double boundary_blend_cross_angle(const BoundaryBlendField& field, Vec2um p) {
    // Perpendiculaire à la tangente LISSÉE de la frontière autour de p : moyenne des directions
    // des segments (angles doublés : un segment et son opposé comptent pareil), pondérée par la
    // longueur et par exp(-((d - d_min) / σ)²), σ = demi-largeur moyenne de la bande. La simple
    // perpendiculaire au point le plus proche convergeait du côté concave d'une frontière courbe
    // (rangées croisées en éventail, recette Studio) et suivait chaque cran d'une frontière
    // vectorisée ; ce champ est continu partout et exact le long d'une frontière droite.
    const double sigma = std::max(1.0, 0.5 * (field.width_a() + field.width_b()));
    const SegmentGrid& grid = field.index().frontier;
    const double dmin = grid.min_distance(p);
    if (dmin == std::numeric_limits<double>::max()) {
        return 0.0;
    }
    // Seuls les segments à au plus dmin + 4σ pèsent (au-delà : poids < 1e-7, ignorés) ; ils sont
    // parcourus dans l'ordre de la frontière, comme le parcours complet : même somme au bit près.
    thread_local std::vector<std::uint32_t> candidates;
    const bool indexed = grid.near(p, dmin + 4.0 * sigma, candidates);
    const auto& segments = grid.segments();
    const std::size_t count = indexed ? candidates.size() : segments.size();
    double c2 = 0.0;
    double s2 = 0.0;
    for (std::size_t k = 0; k < count; ++k) {
        const auto& seg = segments[indexed ? candidates[k] : k];
        if (seg.len <= 0.0) {
            continue;
        }
        const double u = (seg_distance(p, seg.a, seg.b) - dmin) / sigma;
        if (u > 4.0) {
            continue; // poids < 1e-7
        }
        const double w = seg.len * std::exp(-u * u);
        c2 += w * seg.cos2;
        s2 += w * seg.sin2;
    }
    return 0.5 * std::atan2(s2, c2) + 0.5 * std::numbers::pi;
}

BoundaryBlendField::BoundaryBlendField(const std::vector<geometry::PathSet>& a,
                                       std::vector<geometry::Polyline> frontier,
                                       const BoundaryBlendSpec& spec, Micrometers reach_margin)
    : frontier_(std::move(frontier)),
      widthA_(static_cast<double>(std::clamp(spec.width_a.value, kMinWidth, kMaxWidth))),
      widthB_(static_cast<double>(std::clamp(spec.width_b.value, kMinWidth, kMaxWidth))),
      curve_(spec.curve) {
    auto index = std::make_shared<Index>(
        frontier_, std::max(widthA_, widthB_) +
                       static_cast<double>(std::max<std::int32_t>(0, reach_margin.value)) + 1.0);
    const auto addPoly = [&](const geometry::Path& path) {
        std::vector<Vec2um> pts;
        pts.reserve(path.nodes.size());
        for (const auto& node : path.nodes) {
            pts.push_back(node.pos);
        }
        if (pts.size() >= 3) {
            index->inside_a.add_polygon(pts);
        }
    };
    // Composantes disjointes : le test pair-impair sur la réunion de leurs contours reste juste.
    for (const auto& component : a) {
        const geometry::PathSet flat = geometry::flatten_to_polygon(component, Micrometers{20});
        addPoly(flat.outer);
        for (const auto& hole : flat.holes) {
            addPoly(hole);
        }
    }
    index->inside_a.build();
    index_ = std::move(index);
    double x0 = std::numeric_limits<double>::max();
    double y0 = x0;
    double x1 = std::numeric_limits<double>::lowest();
    double y1 = x1;
    for (const auto& line : frontier_) {
        for (const Vec2um p : line.points) {
            x0 = std::min(x0, static_cast<double>(p.x.value));
            y0 = std::min(y0, static_cast<double>(p.y.value));
            x1 = std::max(x1, static_cast<double>(p.x.value));
            y1 = std::max(y1, static_cast<double>(p.y.value));
        }
    }
    if (x0 <= x1) {
        const double margin = std::max(widthA_, widthB_) +
                              static_cast<double>(std::max<std::int32_t>(0, reach_margin.value)) +
                              1.0;
        boxX0_ = x0 - margin;
        boxY0_ = y0 - margin;
        boxX1_ = x1 + margin;
        boxY1_ = y1 + margin;
    }
}

bool BoundaryBlendField::near_frontier(Vec2um p) const {
    const double x = p.x.value;
    const double y = p.y.value;
    return x >= boxX0_ && x <= boxX1_ && y >= boxY0_ && y <= boxY1_;
}

bool BoundaryBlendField::inside_a(Vec2um p) const {
    return index_->inside_a.contains(p);
}

double BoundaryBlendField::signed_distance(Vec2um p) const {
    const double best = index_->frontier.min_distance(p);
    return inside_a(p) ? -best : best;
}

double BoundaryBlendField::density_a(Vec2um p) const {
    if (!near_frontier(p)) {
        return inside_a(p) ? 1.0 : 0.0;
    }
    const double s = signed_distance(p);
    const double t = (s + widthA_) / (widthA_ + widthB_);
    return 1.0 - shape_density_curve(curve_, t);
}

Result<BoundaryBlendPlan> plan_boundary_blend(const geometry::PathSet& a,
                                              const geometry::PathSet& b,
                                              const BoundaryBlendSpec& spec,
                                              Micrometers reach_margin) {
    return plan_boundary_blend(std::vector<geometry::PathSet>{a}, std::vector<geometry::PathSet>{b},
                               spec, reach_margin);
}

Result<BoundaryBlendPlan> plan_boundary_blend(const std::vector<geometry::PathSet>& a,
                                              const std::vector<geometry::PathSet>& b,
                                              const BoundaryBlendSpec& spec,
                                              Micrometers reach_margin) {
    for (const Micrometers w : {spec.width_a, spec.width_b}) {
        if (w.value < kMinWidth || w.value > kMaxWidth) {
            return fail_coded(ErrorCategory::UserInput, "E_BLEND_WIDTH_RANGE",
                              "Largeur de fondu hors limites",
                              "Choisir une demi-bande entre 0,5 et 15 mm de chaque côté.");
        }
    }
    std::vector<geometry::Polyline> frontier;
    const bool manual = !spec.manual_frontier.empty();
    if (manual) {
        // Frontière tracée : prise telle quelle (les zones peuvent ne pas se toucher, ou se
        // chevaucher). Au moins deux points et `min_frontier` de long.
        if (spec.manual_frontier.size() < 2 || geometry::polyline_length(spec.manual_frontier) <
                                                   static_cast<double>(spec.min_frontier.value)) {
            return fail_coded(ErrorCategory::UserInput, "E_BLEND_FRONTIER_SHORT",
                              "Frontière tracée trop courte",
                              "Tracer une ligne d'au moins 1 mm entre les deux zones.");
        }
        frontier.push_back(geometry::Polyline{spec.manual_frontier, false});
    } else {
        // Zones en plusieurs composantes : réunion des frontières de chaque couple, dans l'ordre.
        for (const auto& ai : a) {
            for (const auto& bj : b) {
                auto part = geometry::shared_frontier(ai, bj, spec.tolerance, spec.min_frontier);
                if (!part) {
                    return std::unexpected(part.error());
                }
                frontier.insert(frontier.end(), part->begin(), part->end());
            }
        }
    }
    if (frontier.empty()) {
        return fail_coded(ErrorCategory::OperationImpossible, "E_BLEND_NO_FRONTIER",
                          "Les deux zones ne se touchent pas",
                          "Rapprocher les zones ou dessiner la frontière à la main.");
    }
    if (manual) {
        // Chaque zone doit être atteinte par sa demi-bande : sinon le fondu n'aurait aucun effet
        // de ce côté (frontière tracée loin d'une des zones).
        const auto reaches = [&](const std::vector<geometry::PathSet>& zones, Micrometers width) {
            const auto band = cached_band(frontier, width);
            if (!band) {
                return false;
            }
            const auto common = geometry::intersect_polygons(zones, *band);
            if (!common) {
                return false;
            }
            double area = 0.0;
            for (const auto& set : *common) {
                area += geometry::path_set_area_um2(set);
            }
            return area > 0.0;
        };
        if (!reaches(a, spec.width_a) || !reaches(b, spec.width_b)) {
            return fail_coded(ErrorCategory::UserInput, "E_BLEND_FRONTIER_FAR",
                              "La frontière tracée est trop loin d'une des deux zones",
                              "Tracer la frontière entre les deux zones, ou élargir la bande.");
        }
    }
    BoundaryBlendPlan plan;
    plan.frontier = frontier;
    const std::int32_t margin = std::clamp<std::int32_t>(reach_margin.value, 0, 10'000);
    auto regionA =
        extended(a, b, plan.frontier, Micrometers{spec.width_b.value + margin}, spec.tolerance);
    if (!regionA) {
        return std::unexpected(regionA.error());
    }
    auto regionB =
        extended(b, a, plan.frontier, Micrometers{spec.width_a.value + margin}, spec.tolerance);
    if (!regionB) {
        return std::unexpected(regionB.error());
    }
    plan.region_a = std::move(*regionA);
    plan.region_b = std::move(*regionB);
    plan.field = std::make_shared<const BoundaryBlendField>(a, std::move(frontier), spec,
                                                            Micrometers{margin});
    return plan;
}

Result<BoundaryBlendFills> fill_boundary_blend_tatami(const geometry::PathSet& a,
                                                      const document::TatamiParams& params_a,
                                                      const geometry::PathSet& b,
                                                      const document::TatamiParams& params_b,
                                                      const BoundaryBlendSpec& spec) {
    return fill_boundary_blend(a, BoundaryBlendFillParams{params_a}, b,
                               BoundaryBlendFillParams{params_b}, spec);
}

Result<BoundaryBlendFills> fill_boundary_blend(const geometry::PathSet& a,
                                               const BoundaryBlendFillParams& params_a,
                                               const geometry::PathSet& b,
                                               const BoundaryBlendFillParams& params_b,
                                               const BoundaryBlendSpec& spec) {
    return fill_boundary_blend(std::vector<geometry::PathSet>{a}, params_a,
                               std::vector<geometry::PathSet>{b}, params_b, spec);
}

Result<BoundaryBlendFills> fill_boundary_blend(const std::vector<geometry::PathSet>& a,
                                               const BoundaryBlendFillParams& params_a,
                                               const std::vector<geometry::PathSet>& b,
                                               const BoundaryBlendFillParams& params_b,
                                               const BoundaryBlendSpec& spec) {
    auto plan = plan_boundary_blend(a, b, spec, blend_margin(params_a, params_b));
    if (!plan) {
        return std::unexpected(plan.error());
    }
    const auto* tatamiA = std::get_if<document::TatamiParams>(&params_a);
    const auto* tatamiB = std::get_if<document::TatamiParams>(&params_b);
    // Shared / Rotating : cœurs à leur angle, bande commune partagée (§6.3). Une zone
    // directionnelle a ses propres lignes de courant : repli sur PerZone.
    if (spec.rows != document::BoundaryBlendRows::PerZone && tatamiA != nullptr &&
        tatamiB != nullptr) {
        auto split = split_band(a, b, plan->frontier, spec);
        if (!split) {
            return std::unexpected(split.error());
        }
        const BandFiller band(plan->field, *tatamiA, *tatamiB, spec.rows);
        const bool floored = spec.rows != document::BoundaryBlendRows::Interlaced;
        const BoundaryRowMask lower(*plan->field, true, false, floored);
        const BoundaryRowMask upper(*plan->field, false, false, floored);
        BoundaryBlendFills fills;
        fills.frontier = plan->frontier;
        for (const auto& core : split->core_a) {
            append_fill(fills.a, fill_tatami(core, *tatamiA));
        }
        for (const auto& part : split->band) {
            append_fill(fills.a, band.fill(part, lower));
        }
        for (const auto& part : split->band) {
            append_fill(fills.b, band.fill(part, upper));
        }
        for (const auto& core : split->core_b) {
            append_fill(fills.b, fill_tatami(core, *tatamiB));
        }
        return fills;
    }
    const bool overlap = !same_row_grid(params_a, params_b);
    const BoundaryRowMask maskA(*plan->field, true, overlap);
    const BoundaryRowMask maskB(*plan->field, false, overlap);
    const auto fillLayer = [](const geometry::PathSet& region, const BoundaryBlendFillParams& p,
                              const RowMask& mask) {
        if (const auto* tatami = std::get_if<document::TatamiParams>(&p)) {
            return fill_tatami(region, *tatami, &mask);
        }
        return fill_directional(region, std::get<document::DirectionalFillParams>(p), &mask);
    };
    BoundaryBlendFills fills;
    fills.frontier = plan->frontier;
    for (const auto& region : plan->region_a) {
        append_fill(fills.a, fillLayer(region, params_a, maskA));
    }
    for (const auto& region : plan->region_b) {
        append_fill(fills.b, fillLayer(region, params_b, maskB));
    }
    return fills;
}

Result<std::vector<FillStitch>> boundary_blend_band_rows(const geometry::PathSet& a,
                                                         const document::TatamiParams& params_a,
                                                         const geometry::PathSet& b,
                                                         const document::TatamiParams& params_b,
                                                         const BoundaryBlendSpec& spec) {
    std::vector<FillStitch> out;
    if (spec.rows == document::BoundaryBlendRows::PerZone) {
        return out;
    }
    const std::vector<geometry::PathSet> za{a};
    const std::vector<geometry::PathSet> zb{b};
    auto plan = plan_boundary_blend(
        za, zb, spec,
        blend_margin(BoundaryBlendFillParams{params_a}, BoundaryBlendFillParams{params_b}));
    if (!plan) {
        return std::unexpected(plan.error());
    }
    auto split = split_band(za, zb, plan->frontier, spec);
    if (!split) {
        return std::unexpected(split.error());
    }
    const BandFiller band(plan->field, params_a, params_b, spec.rows);
    const DensityRowMask full([](Vec2um) { return 1.0; }, DensityRowMask::Slice::Lower);
    for (const auto& part : split->band) {
        append_fill(out, band.fill(part, full));
    }
    return out;
}

std::vector<geometry::PathSet> boundary_blend_zone(const document::Project& project,
                                                   const document::EmbroideryObject& object) {
    std::vector<geometry::PathSet> zones;
    Micrometers inset{0};
    if (const auto* t = std::get_if<document::TatamiParams>(&object.params)) {
        inset = t->inset;
    } else if (const auto* d = std::get_if<document::DirectionalFillParams>(&object.params)) {
        inset = d->inset;
    } else {
        return zones;
    }
    const auto* source = project.findObject(object.source_vector);
    if (source == nullptr) {
        return zones;
    }
    // Même préparation que `fill_regions_in_sewing_order` (generate.cpp) : aplatissement à
    // 50 µm, retrait de bord, forme brute si le retrait la fait disparaître.
    for (const geometry::PathSet& rawSet : source->paths) {
        const geometry::PathSet set = geometry::flatten_to_polygon(rawSet, Micrometers{50});
        std::vector<geometry::PathSet> filled;
        if (inset.value > 0) {
            if (auto shrunk = geometry::inset_path_set(set, inset); shrunk && !shrunk->empty()) {
                filled = std::move(*shrunk);
            }
        }
        if (filled.empty()) {
            filled.push_back(set);
        }
        zones.insert(zones.end(), filled.begin(), filled.end());
    }
    return zones;
}

namespace {

// Paramètres de remplissage d'une zone du fondu, compensation de tirage résolue sur `region`.
BoundaryBlendFillParams resolved_fill_params(const document::EmbroideryObject& o,
                                             const geometry::PathSet& region) {
    if (const auto* t = std::get_if<document::TatamiParams>(&o.params)) {
        return resolve_pull_compensation(*t, region);
    }
    return resolve_pull_compensation(std::get<document::DirectionalFillParams>(o.params), region);
}

// Paramètres bruts (sans résolution) : seule la longueur de point en est lue (marge).
BoundaryBlendFillParams raw_fill_params(const document::EmbroideryObject& o) {
    if (const auto* t = std::get_if<document::TatamiParams>(&o.params)) {
        return *t;
    }
    return std::get<document::DirectionalFillParams>(o.params);
}

// Spécification du couple (first, second), first = identifiant le plus petit (A). Liens
// réciproques garantis par `document::find_boundary_blend`.
BoundaryBlendSpec couple_spec(const document::EmbroideryObject& first,
                              const document::EmbroideryObject& second) {
    const auto* linkFirst = document::boundary_blend_link(first, second.id);
    const auto* linkSecond = document::boundary_blend_link(second, first.id);
    BoundaryBlendSpec spec;
    spec.width_a = linkFirst->width_self;
    spec.width_b = linkSecond->width_self;
    spec.curve = linkFirst->curve;
    spec.manual_frontier = linkFirst->manual_frontier;
    spec.rows = linkFirst->rows;
    return spec;
}

// Couche d'un objet dont le groupe ne compte qu'un couple utile : comportement des lots L1-L4.
std::optional<BoundaryBlendLayer> single_pair_layer(const document::Project& project,
                                                    const document::BoundaryBlendPair* pair) {
    const auto* self = project.findEmbroidery(pair->self);
    const auto* partner = project.findEmbroidery(pair->partner);
    if (self == nullptr || partner == nullptr) {
        return std::nullopt;
    }
    // Sens canonique : la zone d'identifiant le plus petit est A, quelle que soit la zone
    // générée. Les deux couches sortent ainsi du MÊME plan (frontière, champ, régions).
    const bool selfFirst = self->id.value < partner->id.value;
    const document::EmbroideryObject& first = selfFirst ? *self : *partner;
    const document::EmbroideryObject& second = selfFirst ? *partner : *self;
    const auto zonesFirst = boundary_blend_zone(project, first);
    const auto zonesSecond = boundary_blend_zone(project, second);
    if (zonesFirst.empty() || zonesSecond.empty()) {
        return std::nullopt;
    }
    const auto* linkSelf = document::boundary_blend_link(*self, partner->id);
    const BoundaryBlendSpec spec = couple_spec(first, second);
    // Même calcul pour les deux zones du couple : mémorisé (cf. `ExactCache`), chaque zone en
    // prend sa couche.
    FillsKey key{zonesFirst, resolved_fill_params(first, zonesFirst.front()), zonesSecond,
                 resolved_fill_params(second, zonesSecond.front()), spec};
    const auto cached = fills_cache().get(key, [&key] {
        return fill_boundary_blend(key.zones_a, key.params_a, key.zones_b, key.params_b, key.spec);
    });
    const Result<BoundaryBlendFills>& fills = *cached;
    if (!fills) {
        return std::nullopt;
    }
    BoundaryBlendLayer layer;
    layer.stitches = selfFirst ? fills->a : fills->b;
    const auto& ownZones = selfFirst ? zonesFirst : zonesSecond;
    const auto band = cached_band(fills->frontier, linkSelf->width_self);
    if (band) {
        if (auto core = geometry::difference_polygons(ownZones, *band)) {
            layer.underlay_zones = std::move(*core);
        }
    } else {
        layer.underlay_zones = ownZones; // bande introuvable : sous-couche habituelle
    }
    return layer;
}

// --- Plusieurs fondus par zone (§6.4) --------------------------------------------------------

// Boîte englobante entière (µm, int64 : élargissements sans débordement).
struct Box {
    std::int64_t x0{std::numeric_limits<std::int64_t>::max()};
    std::int64_t y0{std::numeric_limits<std::int64_t>::max()};
    std::int64_t x1{std::numeric_limits<std::int64_t>::min()};
    std::int64_t y1{std::numeric_limits<std::int64_t>::min()};

    [[nodiscard]] bool empty() const { return x0 > x1; }
    void add(Vec2um p) {
        x0 = std::min<std::int64_t>(x0, p.x.value);
        y0 = std::min<std::int64_t>(y0, p.y.value);
        x1 = std::max<std::int64_t>(x1, p.x.value);
        y1 = std::max<std::int64_t>(y1, p.y.value);
    }
    void add(const Box& b) {
        if (!b.empty()) {
            x0 = std::min(x0, b.x0);
            y0 = std::min(y0, b.y0);
            x1 = std::max(x1, b.x1);
            y1 = std::max(y1, b.y1);
        }
    }
    void grow(std::int64_t d) {
        if (!empty()) {
            x0 -= d;
            y0 -= d;
            x1 += d;
            y1 += d;
        }
    }
    [[nodiscard]] bool intersects(const Box& b) const {
        return !empty() && !b.empty() && x0 <= b.x1 && b.x0 <= x1 && y0 <= b.y1 && b.y0 <= y1;
    }
};

// Boîte de la forme source brute (nœuds et poignées : une courbe de Bézier reste dans l'enveloppe
// de ses points de contrôle ; le retrait de bord ne fait que rétrécir la zone).
Box source_box(const document::Project& project, const document::EmbroideryObject& object) {
    Box box;
    const auto* source = project.findObject(object.source_vector);
    if (source == nullptr) {
        return box;
    }
    const auto addPath = [&box](const geometry::Path& path) {
        for (const auto& node : path.nodes) {
            box.add(node.pos);
            if (node.tan_in) {
                box.add(Vec2um{node.pos.x + node.tan_in->x, node.pos.y + node.tan_in->y});
            }
            if (node.tan_out) {
                box.add(Vec2um{node.pos.x + node.tan_out->x, node.pos.y + node.tan_out->y});
            }
        }
    };
    for (const auto& set : source->paths) {
        addPath(set.outer);
        for (const auto& hole : set.holes) {
            addPath(hole);
        }
    }
    return box;
}

// Appartenance d'un point à une liste de régions disjointes (pair-impair sur tous les contours,
// comme `BoundaryBlendField::inside_a`). Immuable : lecture concurrente sûre.
class RegionTester {
public:
    explicit RegionTester(const std::vector<geometry::PathSet>& regions) {
        const auto addPoly = [&](const geometry::Path& path) {
            std::vector<Vec2um> pts;
            pts.reserve(path.nodes.size());
            for (const auto& node : path.nodes) {
                pts.push_back(node.pos);
                box_.add(node.pos);
            }
            if (pts.size() >= 3) {
                polys_.add_polygon(pts);
            }
        };
        for (const auto& component : regions) {
            const geometry::PathSet flat = geometry::flatten_to_polygon(component, Micrometers{20});
            addPoly(flat.outer);
            for (const auto& hole : flat.holes) {
                addPoly(hole);
            }
        }
        polys_.build();
    }

    [[nodiscard]] bool contains(Vec2um p) const {
        if (p.x.value < box_.x0 || p.x.value > box_.x1 || p.y.value < box_.y0 ||
            p.y.value > box_.y1) {
            return false;
        }
        return polys_.contains(p);
    }

private:
    EvenOddIndex polys_;
    Box box_;
};

// Un couple du groupe, planifié dans son sens canonique (lo = A, hi = B).
struct GroupCouple {
    ObjectId lo;
    ObjectId hi;
    BoundaryBlendFillParams params_lo;
    BoundaryBlendFillParams params_hi;
    BoundaryBlendSpec spec;
    BoundaryBlendPlan plan;
    double margin{0.0}; // prolongation au-delà des demi-bandes (µm, celle du plan)
    // Sens des rangées effectif : celui du lien si les deux zones sont des tatamis, sinon PerZone.
    document::BoundaryBlendRows mode{document::BoundaryBlendRows::PerZone};
    bool overlap{false}; // grilles différentes (PerZone) : superposition
    // Mode Shared : grille des rangées de la bande commune (angle commun, écart, longueur de point
    // et décalage de la zone A, qui fixent aussi les cases de décision du masque).
    std::optional<document::TatamiParams> shared_grid;
    std::unique_ptr<RegionTester> reach_a;
    std::unique_ptr<RegionTester> reach_b;

    [[nodiscard]] bool contains(ObjectId id) const { return id == lo || id == hi; }
};

// Règle de la frontière la plus proche : en p, le couple qui gouverne est, parmi ceux dont p est
// dans la bande (|s| <= demi-bande du côté de p + marge) ET dans une des deux régions prolongées,
// celui dont |s| est minimal ; égalité : premier couple dans l'ordre (lo, hi). -1 : aucun.
class Governance {
public:
    struct Verdict {
        int governor{-1}; // -1 : aucun couple ne gouverne
        double s{0.0};    // distance signée au couple qui gouverne
        double abs{std::numeric_limits<double>::max()};
    };

    explicit Governance(const std::vector<GroupCouple>& couples) : couples_(couples) {}

    [[nodiscard]] Verdict governor(Vec2um p) const {
        Verdict v;
        for (std::size_t k = 0; k < couples_.size(); ++k) {
            const auto s = qualified(k, p, v.abs);
            if (s) {
                v.governor = static_cast<int>(k);
                v.s = *s;
                v.abs = std::abs(*s);
            }
        }
        return v;
    }

    // Distance signée au couple `k` s'il est candidat en p (bande et régions) avec |s| < `below`.
    [[nodiscard]] std::optional<double>
    qualified(std::size_t k, Vec2um p, double below = std::numeric_limits<double>::max()) const {
        const GroupCouple& c = couples_[k];
        const BoundaryBlendField& field = *c.plan.field;
        if (!field.near_frontier(p)) {
            return std::nullopt; // boîte de la bande élargie de la marge : test bon marché d'abord
        }
        const double s = field.signed_distance(p);
        const double half = s < 0.0 ? field.width_a() : field.width_b();
        const double d = std::abs(s);
        if (d > half + c.margin || d >= below) {
            return std::nullopt;
        }
        if (!c.reach_a->contains(p) && !c.reach_b->contains(p)) {
            return std::nullopt;
        }
        return s;
    }

private:
    const std::vector<GroupCouple>& couples_;
};

// Recouvrement des bandes communes de deux couples voisins (µm) : la bande d'un couple coud aussi
// les points où sa frontière n'est plus loin que de cette marge que celle du couple qui gouverne.
// Deux couples « bande » n'ont pas la même grille de rangées : sans recouvrement, la coupure
// tombe au milieu des morceaux de rangée (décidés en leur milieu) et laisse une rangée nue le
// long de la limite de gouvernance (mesuré : 1,6 % de tissu nu à la jonction en T).
constexpr double kGroupBandOverlap = 300.0;

// Deux bandes Shared de même grille (absolue) décident aux mêmes points : partition exacte entre
// elles, sans recouvrement (il doublerait des rangées identiques).
bool same_band_grid(const GroupCouple& a, const GroupCouple& b) {
    if (!a.shared_grid || !b.shared_grid) {
        return false;
    }
    const auto& ga = *a.shared_grid;
    const auto& gb = *b.shared_grid;
    return std::abs(ga.angle.radians - gb.angle.radians) < 1e-9 &&
           ga.row_spacing == gb.row_spacing && ga.stitch_length == gb.stitch_length &&
           ga.stagger == gb.stagger;
}

// Masque d'une couche dans un groupe de plusieurs couples.
// - Couche principale (`band` < 0) : aucun couple ne gouverne -> aplat ; un couple sans la zone
//   gouverne -> rien ; un couple PerZone de la zone gouverne -> masque de ce couple ; un couple
//   « bande » de la zone gouverne -> cœur seulement (du côté de la zone, au-delà de sa
//   demi-bande : la bande commune y coud le recouvrement).
// - Bande commune du couple `band` : son masque là où il gouverne, rien ailleurs.
class GroupRowMask final : public RowMask {
public:
    GroupRowMask(const std::vector<GroupCouple>& couples, const Governance& governance,
                 const std::vector<std::optional<BoundaryRowMask>>& own, ObjectId self, int band)
        : couples_(couples), governance_(governance), own_(own), self_(self), band_(band) {}

    [[nodiscard]] bool keeps(Vec2um p, double threshold) const override {
        int g = -1;
        const Decision d = decide(p, g);
        return d == Decision::Mask ? own_[static_cast<std::size_t>(g)]->keeps(p, threshold)
                                   : d == Decision::All;
    }

    [[nodiscard]] double coverage(Vec2um p) const override {
        int g = -1;
        const Decision d = decide(p, g);
        if (d == Decision::Mask) {
            return own_[static_cast<std::size_t>(g)]->coverage(p);
        }
        return d == Decision::All ? 1.0 : 0.0;
    }

private:
    enum class Decision : std::uint8_t { None, All, Mask };

    // `g` reçoit le couple dont le masque s'applique (Decision::Mask).
    [[nodiscard]] Decision decide(Vec2um p, int& g) const {
        const Governance::Verdict v = governance_.governor(p);
        g = v.governor;
        const double s = v.s;
        if (band_ >= 0) {
            if (g == band_) {
                return Decision::Mask;
            }
            const auto own = governance_.qualified(static_cast<std::size_t>(band_), p);
            if (own &&
                !same_band_grid(couples_[static_cast<std::size_t>(band_)],
                                couples_[static_cast<std::size_t>(g)]) &&
                std::abs(*own) <= v.abs + kGroupBandOverlap) {
                g = band_;
                return Decision::Mask;
            }
            return Decision::None;
        }
        if (g < 0) {
            return Decision::All;
        }
        const GroupCouple& c = couples_[static_cast<std::size_t>(g)];
        if (!c.contains(self_)) {
            return Decision::None;
        }
        if (c.mode == document::BoundaryBlendRows::PerZone) {
            return Decision::Mask;
        }
        const bool selfA = self_ == c.lo;
        const bool onSelfSide = selfA ? s < 0.0 : s >= 0.0;
        const double half = selfA ? c.plan.field->width_a() : c.plan.field->width_b();
        // Le cœur est déjà découpé géométriquement ; le masque y décide au milieu de la case des
        // pénétrations RAMENÉ dans le morceau, donc souvent exactement sur le bord de bande (et à
        // quelques µm près pour un bord polygonal) : tolérance du recouvrement cœur-bande.
        return onSelfSide && std::abs(s) > half - kBoundaryBlendSeamOverlap ? Decision::All
                                                                            : Decision::None;
    }

    const std::vector<GroupCouple>& couples_;
    const Governance& governance_;
    const std::vector<std::optional<BoundaryRowMask>>& own_;
    ObjectId self_;
    int band_;
};

using Couple = std::pair<std::uint64_t, std::uint64_t>;

std::vector<Couple> component_couples(const document::Project& project, ObjectId id) {
    std::set<Couple> couples;
    std::set<std::uint64_t> seen{id.value};
    std::deque<ObjectId> queue{id};
    while (!queue.empty()) {
        const ObjectId cur = queue.front();
        queue.pop_front();
        for (const auto& pair : document::find_boundary_blends(project, cur)) {
            couples.emplace(std::min(cur.value, pair.partner.value),
                            std::max(cur.value, pair.partner.value));
            if (seen.insert(pair.partner.value).second) {
                queue.push_back(pair.partner);
            }
        }
    }
    return {couples.begin(), couples.end()};
}

std::optional<BoundaryBlendLayer> group_layer(const document::Project& project,
                                              const document::EmbroideryObject& object,
                                              const std::vector<Couple>& relevant) {
    std::map<std::uint64_t, std::vector<geometry::PathSet>> zones;
    const auto zonesOf =
        [&](const document::EmbroideryObject& o) -> const std::vector<geometry::PathSet>& {
        auto it = zones.find(o.id.value);
        if (it == zones.end()) {
            it = zones.emplace(o.id.value, boundary_blend_zone(project, o)).first;
        }
        return it->second;
    };
    std::vector<GroupCouple> couples;
    for (const auto& [loId, hiId] : relevant) {
        const auto* lo = project.findEmbroidery(ObjectId{loId});
        const auto* hi = project.findEmbroidery(ObjectId{hiId});
        if (lo == nullptr || hi == nullptr) {
            continue;
        }
        const auto& zLo = zonesOf(*lo);
        const auto& zHi = zonesOf(*hi);
        if (zLo.empty() || zHi.empty()) {
            continue;
        }
        GroupCouple c;
        c.lo = lo->id;
        c.hi = hi->id;
        c.params_lo = resolved_fill_params(*lo, zLo.front());
        c.params_hi = resolved_fill_params(*hi, zHi.front());
        c.spec = couple_spec(*lo, *hi);
        const Micrometers margin = blend_margin(c.params_lo, c.params_hi);
        // Chaque zone du groupe planifie les mêmes couples : plans mémorisés (cf. `ExactCache`).
        const auto cachedPlan = plan_cache().get(PlanKey{zLo, zHi, c.spec, margin}, [&] {
            return plan_boundary_blend(zLo, zHi, c.spec, margin);
        });
        auto plan = *cachedPlan;
        if (!plan) {
            continue; // frontière disparue : couple sans effet, pour toutes les couches
        }
        c.plan = std::move(*plan);
        c.margin = static_cast<double>(std::clamp<std::int32_t>(margin.value, 0, 10'000));
        const bool tatamis = std::holds_alternative<document::TatamiParams>(c.params_lo) &&
                             std::holds_alternative<document::TatamiParams>(c.params_hi);
        c.mode = tatamis ? c.spec.rows : document::BoundaryBlendRows::PerZone;
        c.overlap = !same_row_grid(c.params_lo, c.params_hi);
        if (c.mode == document::BoundaryBlendRows::Shared) {
            document::TatamiParams grid = std::get<document::TatamiParams>(c.params_lo);
            const auto& hiTatami = std::get<document::TatamiParams>(c.params_hi);
            grid.angle =
                Angle{boundary_blend_shared_angle(grid.angle.radians, hiTatami.angle.radians)};
            grid.row_spacing =
                Micrometers{std::min(grid.row_spacing.value, hiTatami.row_spacing.value)};
            c.shared_grid = grid;
        }
        c.reach_a = std::make_unique<RegionTester>(c.plan.region_a);
        c.reach_b = std::make_unique<RegionTester>(c.plan.region_b);
        couples.push_back(std::move(c));
    }
    const ObjectId self = object.id;
    const bool selfInAny =
        std::ranges::any_of(couples, [self](const GroupCouple& c) { return c.contains(self); });
    if (!selfInAny) {
        return std::nullopt;
    }
    if (couples.size() == 1) {
        const document::BoundaryBlendPair pair{
            self, couples.front().lo == self ? couples.front().hi : couples.front().lo};
        return single_pair_layer(project, &pair);
    }
    const auto& ownZones = zonesOf(object);
    const Governance governance(couples);
    // Masques de la zone dans chacun de ses couples (mêmes masques que pour un couple seul).
    std::vector<std::optional<BoundaryRowMask>> own(couples.size());
    for (std::size_t k = 0; k < couples.size(); ++k) {
        const GroupCouple& c = couples[k];
        if (!c.contains(self)) {
            continue;
        }
        const bool sideA = c.lo == self;
        if (c.mode == document::BoundaryBlendRows::PerZone) {
            own[k].emplace(*c.plan.field, sideA, c.overlap);
        } else {
            own[k].emplace(*c.plan.field, sideA, false,
                           c.mode != document::BoundaryBlendRows::Interlaced);
        }
    }
    // Région de la couche principale : le CŒUR de la zone (privée de ses demi-bandes « bande »,
    // découpe géométrique comme pour un couple seul : un morceau de rangée à cheval sur le bord de
    // bande, décidé en son milieu, laisserait sinon un jour périodique entre cœur et bande),
    // plus, pour chaque couple PerZone, sa région prolongée près de SA frontière (la tranche de
    // la zone qu'il gouverne dans une demi-bande « bande » voisine, et la prolongation chez le
    // partenaire). Les couples « bande » cousent leur bande à part.
    std::vector<geometry::PathSet> coreZone = ownZones;
    for (const GroupCouple& c : couples) {
        if (!c.contains(self) || c.mode == document::BoundaryBlendRows::PerZone) {
            continue;
        }
        const Micrometers width = c.lo == self ? c.spec.width_a : c.spec.width_b;
        const auto halfBand = cached_band(c.plan.frontier, width);
        if (!halfBand) {
            return std::nullopt;
        }
        auto rest = geometry::difference_polygons(coreZone, *halfBand);
        if (!rest) {
            return std::nullopt;
        }
        coreZone = std::move(*rest);
    }
    std::vector<geometry::PathSet> parts = coreZone;
    for (const GroupCouple& c : couples) {
        if (!c.contains(self) || c.mode != document::BoundaryBlendRows::PerZone) {
            continue;
        }
        const auto& ownRegion = c.lo == self ? c.plan.region_a : c.plan.region_b;
        const auto reach = cached_band(
            c.plan.frontier,
            Micrometers{static_cast<std::int32_t>(
                std::max(c.plan.field->width_a(), c.plan.field->width_b()) + c.margin)});
        if (!reach) {
            return std::nullopt;
        }
        auto near = geometry::intersect_polygons(ownRegion, *reach);
        if (!near) {
            return std::nullopt;
        }
        parts.insert(parts.end(), near->begin(), near->end());
    }
    auto region = geometry::union_polygons(parts);
    if (!region) {
        return std::nullopt;
    }
    const BoundaryBlendFillParams selfParams = resolved_fill_params(object, ownZones.front());
    BoundaryBlendLayer layer;
    const GroupRowMask mainMask(couples, governance, own, self, -1);
    for (const auto& part : *region) {
        if (const auto* tatami = std::get_if<document::TatamiParams>(&selfParams)) {
            append_fill(layer.stitches, fill_tatami(part, *tatami, &mainMask));
        } else {
            append_fill(layer.stitches,
                        fill_directional(part,
                                         std::get<document::DirectionalFillParams>(selfParams),
                                         &mainMask));
        }
    }
    // Bandes communes des couples « bande » de la zone, chacune limitée à ce qu'elle gouverne.
    for (std::size_t k = 0; k < couples.size(); ++k) {
        const GroupCouple& c = couples[k];
        if (!c.contains(self) || c.mode == document::BoundaryBlendRows::PerZone) {
            continue;
        }
        auto split = split_band(zonesOf(*project.findEmbroidery(c.lo)),
                                zonesOf(*project.findEmbroidery(c.hi)), c.plan.frontier, c.spec);
        if (!split) {
            continue;
        }
        const BandFiller band(c.plan.field, std::get<document::TatamiParams>(c.params_lo),
                              std::get<document::TatamiParams>(c.params_hi), c.mode);
        const GroupRowMask bandMask(couples, governance, own, self, static_cast<int>(k));
        for (const auto& part : split->band) {
            append_fill(layer.stitches, band.fill(part, bandMask));
        }
    }
    // Sous-couche : la zone privée de toutes ses demi-bandes.
    std::vector<geometry::PathSet> core = ownZones;
    for (const GroupCouple& c : couples) {
        if (!c.contains(self)) {
            continue;
        }
        const Micrometers width = c.lo == self ? c.spec.width_a : c.spec.width_b;
        const auto halfBand = cached_band(c.plan.frontier, width);
        if (!halfBand) {
            continue;
        }
        auto rest = geometry::difference_polygons(core, *halfBand);
        if (rest) {
            core = std::move(*rest);
        }
    }
    layer.underlay_zones = std::move(core);
    return layer;
}

} // namespace

std::vector<document::BoundaryBlendPair> boundary_blend_group(const document::Project& project,
                                                              ObjectId id) {
    std::vector<document::BoundaryBlendPair> out;
    for (const auto& [lo, hi] : component_couples(project, id)) {
        out.push_back(document::BoundaryBlendPair{ObjectId{lo}, ObjectId{hi}});
    }
    return out;
}

std::optional<BoundaryBlendLayer> boundary_blend_layer(const document::Project& project,
                                                       const document::EmbroideryObject& object) {
    const auto pairs = document::find_boundary_blends(project, object.id);
    if (pairs.empty()) {
        return std::nullopt;
    }
    const auto couples = component_couples(project, object.id);
    if (couples.size() == 1) {
        return single_pair_layer(project, &pairs.front()); // lots L1-L4, octet pour octet
    }
    // Couples utiles : ceux de la zone, et ceux dont la bande peut atteindre sa région. Un couple
    // ne gouverne qu'à l'intérieur de la boîte de son champ, elle-même dans la boîte de ses deux
    // zones élargie de 2 demi-bandes (frontière tracée à au plus une demi-bande d'une zone),
    // de la tolérance de contact et de la marge. Toute couche voit ainsi tous les couples qui
    // peuvent gouverner un point de sa région : même partition, sans planifier tout le groupe.
    const auto reachBox = [&](const Couple& c) {
        const auto* lo = project.findEmbroidery(ObjectId{c.first});
        const auto* hi = project.findEmbroidery(ObjectId{c.second});
        Box box;
        if (lo == nullptr || hi == nullptr) {
            return box;
        }
        box = source_box(project, *lo);
        box.add(source_box(project, *hi));
        const BoundaryBlendSpec spec = couple_spec(*lo, *hi);
        const std::int64_t w = std::max(spec.width_a.value, spec.width_b.value);
        const std::int64_t margin = std::clamp<std::int32_t>(
            blend_margin(raw_fill_params(*lo), raw_fill_params(*hi)).value, 0, 10'000);
        box.grow(w + std::max<std::int64_t>(w, spec.tolerance.value) + margin + 1);
        return box;
    };
    std::vector<Box> boxes;
    boxes.reserve(couples.size());
    Box selfReach;
    for (const auto& c : couples) {
        boxes.push_back(reachBox(c));
        if (c.first == object.id.value || c.second == object.id.value) {
            selfReach.add(boxes.back());
        }
    }
    std::vector<Couple> relevant;
    for (std::size_t k = 0; k < couples.size(); ++k) {
        const auto& c = couples[k];
        if (c.first == object.id.value || c.second == object.id.value ||
            boxes[k].intersects(selfReach)) {
            relevant.push_back(c);
        }
    }
    if (relevant.size() == 1) {
        return single_pair_layer(project, &pairs.front());
    }
    return group_layer(project, object, relevant);
}

} // namespace openstitch::stitch_generation
