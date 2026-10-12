// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stipple/tour.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>

namespace openstitch::stipple {

namespace {

double dist(const PointD& a, const PointD& b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

// Indice de Hilbert d'une cellule (x, y) d'une grille 2^order x 2^order
// (algorithme classique xy -> d).
std::uint64_t hilbert_index(std::uint32_t x, std::uint32_t y, int order) {
    std::uint64_t d = 0;
    for (std::uint32_t s = 1U << (order - 1); s > 0; s >>= 1) {
        const std::uint32_t rx = (x & s) ? 1U : 0U;
        const std::uint32_t ry = (y & s) ? 1U : 0U;
        d += static_cast<std::uint64_t>(s) * s * ((3U * rx) ^ ry);
        if (ry == 0) {
            if (rx == 1) {
                x = s - 1 - (x & (s - 1));
                y = s - 1 - (y & (s - 1));
            } else {
                x &= s - 1;
                y &= s - 1;
            }
            std::swap(x, y);
        } else {
            x &= s - 1;
            y &= s - 1;
        }
    }
    return d;
}

std::vector<std::size_t> hilbert_order(const std::vector<PointD>& pts) {
    const std::size_t n = pts.size();
    double minX = std::numeric_limits<double>::max(), minY = minX;
    double maxX = std::numeric_limits<double>::lowest(), maxY = maxX;
    for (const PointD& p : pts) {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }
    const double span = std::max({maxX - minX, maxY - minY, 1e-9});
    constexpr int kOrder = 16;
    constexpr double kCells = static_cast<double>((1U << kOrder) - 1U);
    std::vector<std::pair<std::uint64_t, std::size_t>> keyed(n);
    for (std::size_t i = 0; i < n; ++i) {
        const auto cx = static_cast<std::uint32_t>((pts[i].x - minX) / span * kCells);
        const auto cy = static_cast<std::uint32_t>((pts[i].y - minY) / span * kCells);
        keyed[i] = {hilbert_index(cx, cy, kOrder), i};
    }
    std::sort(keyed.begin(), keyed.end());
    std::vector<std::size_t> order(n);
    for (std::size_t i = 0; i < n; ++i) {
        order[i] = keyed[i].second;
    }
    return order;
}

// K plus proches voisins de chaque point (grille uniforme), triés par distance
// puis indice.
std::vector<std::vector<std::uint32_t>> nearest_neighbors(const std::vector<PointD>& pts, int k) {
    const std::size_t n = pts.size();
    double minX = std::numeric_limits<double>::max(), minY = minX;
    double maxX = std::numeric_limits<double>::lowest(), maxY = maxX;
    for (const PointD& p : pts) {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }
    const double w = std::max(maxX - minX, 1e-9);
    const double h = std::max(maxY - minY, 1e-9);
    const double cell =
        std::max(std::sqrt(w * h / std::max<double>(1.0, static_cast<double>(n) / 2.0)),
                 std::max(w, h) / 4096.0);
    const int gx = std::max(1, static_cast<int>(w / cell) + 1);
    const int gy = std::max(1, static_cast<int>(h / cell) + 1);
    std::vector<std::vector<std::uint32_t>> grid(static_cast<std::size_t>(gx) *
                                                 static_cast<std::size_t>(gy));
    const auto cell_of = [&](const PointD& p) {
        return std::pair{std::clamp(static_cast<int>((p.x - minX) / cell), 0, gx - 1),
                         std::clamp(static_cast<int>((p.y - minY) / cell), 0, gy - 1)};
    };
    for (std::size_t i = 0; i < n; ++i) {
        const auto [cx, cy] = cell_of(pts[i]);
        grid[static_cast<std::size_t>(cy) * static_cast<std::size_t>(gx) +
             static_cast<std::size_t>(cx)]
            .push_back(static_cast<std::uint32_t>(i));
    }
    const std::size_t want = std::min<std::size_t>(static_cast<std::size_t>(k), n - 1);
    std::vector<std::vector<std::uint32_t>> out(n);
    std::vector<std::pair<double, std::uint32_t>> cand;
    for (std::size_t i = 0; i < n; ++i) {
        const auto [cx, cy] = cell_of(pts[i]);
        cand.clear();
        for (int r = 0;; ++r) {
            for (int yy = cy - r; yy <= cy + r; ++yy) {
                for (int xx = cx - r; xx <= cx + r; ++xx) {
                    if (xx < 0 || yy < 0 || xx >= gx || yy >= gy) {
                        continue;
                    }
                    if (std::max(std::abs(xx - cx), std::abs(yy - cy)) != r) {
                        continue; // anneau seulement
                    }
                    for (const std::uint32_t j :
                         grid[static_cast<std::size_t>(yy) * static_cast<std::size_t>(gx) +
                              static_cast<std::size_t>(xx)]) {
                        if (j != i) {
                            cand.emplace_back(dist(pts[i], pts[j]), j);
                        }
                    }
                }
            }
            if (cand.size() >= want) {
                std::sort(cand.begin(), cand.end());
                // Tout point non encore examiné est à plus de r * cell.
                if (cand[want - 1].first <= static_cast<double>(r) * cell ||
                    (cx - r <= 0 && cy - r <= 0 && cx + r >= gx - 1 && cy + r >= gy - 1)) {
                    break;
                }
            } else if (cx - r <= 0 && cy - r <= 0 && cx + r >= gx - 1 && cy + r >= gy - 1) {
                std::sort(cand.begin(), cand.end());
                break;
            }
        }
        for (std::size_t q = 0; q < want && q < cand.size(); ++q) {
            out[i].push_back(cand[q].second);
        }
    }
    return out;
}

class Tour {
public:
    Tour(const std::vector<PointD>& pts, std::vector<std::size_t> order)
        : pts_(pts), n_(order.size()), t_(order.size()), pos_(order.size()) {
        for (std::size_t i = 0; i < n_; ++i) {
            t_[i] = static_cast<std::uint32_t>(order[i]);
            pos_[t_[i]] = static_cast<std::uint32_t>(i);
        }
    }

    [[nodiscard]] std::uint32_t succ(std::uint32_t c) const { return t_[(pos_[c] + 1) % n_]; }
    [[nodiscard]] std::uint32_t pred(std::uint32_t c) const { return t_[(pos_[c] + n_ - 1) % n_]; }
    [[nodiscard]] double d(std::uint32_t a, std::uint32_t b) const {
        return dist(pts_[a], pts_[b]);
    }

    // Inverse le chemin de positions i..j (cyclique). Si le complément est plus
    // court, c'est lui qu'on inverse : le tour (cycle) est le même, orienté à l'envers.
    void reverse_path(std::size_t i, std::size_t j) {
        std::size_t len = (j + n_ - i) % n_ + 1;
        if (len * 2 > n_) {
            const std::size_t ni = (j + 1) % n_;
            const std::size_t nj = (i + n_ - 1) % n_;
            i = ni;
            j = nj;
            len = n_ - len;
        }
        for (std::size_t s = 0; s < len / 2; ++s) {
            const std::size_t a = (i + s) % n_;
            const std::size_t b = (j + n_ - s) % n_;
            std::swap(t_[a], t_[b]);
            pos_[t_[a]] = static_cast<std::uint32_t>(a);
            pos_[t_[b]] = static_cast<std::uint32_t>(b);
        }
    }

    // 2-opt : améliore autour de `a` ; renvoie vrai si un échange a été fait.
    bool two_opt_city(std::uint32_t a, const std::vector<std::vector<std::uint32_t>>& neigh,
                      std::vector<char>& dont_look) {
        for (int dir = 0; dir < 2; ++dir) {
            const std::uint32_t b = dir == 0 ? succ(a) : pred(a);
            const double dab = d(a, b);
            for (const std::uint32_t c : neigh[a]) {
                const double dac = d(a, c);
                if (dac >= dab) {
                    break;
                }
                const std::uint32_t dd = dir == 0 ? succ(c) : pred(c);
                if (c == b || dd == a) {
                    continue;
                }
                const double delta = dac + d(b, dd) - dab - d(c, dd);
                if (delta < -1e-9) {
                    if (dir == 0) {
                        reverse_path(pos_[b], pos_[c]);
                    } else {
                        reverse_path(pos_[a], pos_[dd]);
                    }
                    for (const std::uint32_t v : {a, b, c, dd}) {
                        dont_look[v] = 0;
                    }
                    return true;
                }
            }
        }
        return false;
    }

    // Or-opt : déplace un segment de 1 à 3 points consécutifs (positions i..j,
    // sans passer par la jonction fin/début) entre deux points voisins.
    bool or_opt_city(std::uint32_t a, const std::vector<std::vector<std::uint32_t>>& neigh,
                     std::vector<char>& dont_look) {
        if (n_ < 8) {
            return false;
        }
        for (std::size_t len = 1; len <= 3; ++len) {
            for (int side = 0; side < 2; ++side) {
                std::size_t i;
                std::size_t j;
                if (side == 0) {
                    i = pos_[a];
                    j = i + len - 1;
                } else {
                    j = pos_[a];
                    if (j + 1 < len) {
                        continue;
                    }
                    i = j + 1 - len;
                }
                if (j >= n_ || (side == 1 && len == 1)) {
                    continue;
                }
                const std::uint32_t s1 = t_[i];
                const std::uint32_t s2 = t_[j];
                const std::uint32_t p = t_[(i + n_ - 1) % n_];
                const std::uint32_t nx = t_[(j + 1) % n_];
                const double gain = d(p, s1) + d(s2, nx) - d(p, nx);
                if (gain <= 1e-9) {
                    continue;
                }
                double best = -1e-9;
                std::size_t bestK = n_;
                bool bestReversed = false;
                for (const std::uint32_t end : {s1, s2}) {
                    for (const std::uint32_t c : neigh[end]) {
                        if (d(end, c) >= gain) {
                            break;
                        }
                        const std::size_t pc = pos_[c];
                        if (pc >= i && pc <= j) {
                            continue;
                        }
                        for (const std::size_t k : {pc, (pc + n_ - 1) % n_}) {
                            // Arête (t[k], t[k+1]) ; refuse celles qui touchent le segment.
                            const std::size_t lo = (i + n_ - 1) % n_;
                            const bool touches =
                                (lo <= j) ? (k >= lo && k <= j) : (k >= lo || k <= j);
                            if (touches) {
                                continue;
                            }
                            const std::uint32_t u = t_[k];
                            const std::uint32_t w = t_[(k + 1) % n_];
                            const double base = d(u, w);
                            const double fwd = d(u, s1) + d(s2, w) - base;
                            const double rev = d(u, s2) + d(s1, w) - base;
                            const double delta_f = fwd - gain;
                            const double delta_r = rev - gain;
                            if (delta_f < best) {
                                best = delta_f;
                                bestK = k;
                                bestReversed = false;
                            }
                            if (delta_r < best) {
                                best = delta_r;
                                bestK = k;
                                bestReversed = true;
                            }
                        }
                    }
                }
                if (bestK == n_) {
                    continue;
                }
                apply_or_move(i, j, bestK, bestReversed);
                for (const std::uint32_t v : {s1, s2, p, nx, t_[bestK], t_[(bestK + 1) % n_]}) {
                    dont_look[v] = 0;
                }
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::vector<std::size_t> order() const {
        return std::vector<std::size_t>(t_.begin(), t_.end());
    }

    [[nodiscard]] double length() const {
        double total = 0.0;
        for (std::size_t i = 0; i < n_; ++i) {
            total += d(t_[i], t_[(i + 1) % n_]);
        }
        return total;
    }

private:
    // Déplace le bloc de positions [i, j] juste après la position k (k hors de
    // [i-1, j]) ; `reversed` inverse le bloc dans sa nouvelle place.
    void apply_or_move(std::size_t i, std::size_t j, std::size_t k, bool reversed) {
        const auto first = t_.begin();
        std::size_t from;
        std::size_t to;
        if (k > j) {
            std::rotate(first + static_cast<std::ptrdiff_t>(i),
                        first + static_cast<std::ptrdiff_t>(j + 1),
                        first + static_cast<std::ptrdiff_t>(k + 1));
            from = i;
            to = k;
            if (reversed) {
                const std::size_t len = j - i + 1;
                std::reverse(first + static_cast<std::ptrdiff_t>(k + 1 - len),
                             first + static_cast<std::ptrdiff_t>(k + 1));
            }
        } else {
            // k < i - 1 (ou k = n-1 avec i = 0 exclu plus haut) : le bloc passe avant.
            std::rotate(first + static_cast<std::ptrdiff_t>(k + 1),
                        first + static_cast<std::ptrdiff_t>(i),
                        first + static_cast<std::ptrdiff_t>(j + 1));
            from = k + 1;
            to = j;
            if (reversed) {
                const std::size_t len = j - i + 1;
                std::reverse(first + static_cast<std::ptrdiff_t>(k + 1),
                             first + static_cast<std::ptrdiff_t>(k + 1 + len));
            }
        }
        for (std::size_t q = from; q <= to; ++q) {
            pos_[t_[q]] = static_cast<std::uint32_t>(q);
        }
    }

    const std::vector<PointD>& pts_;
    std::size_t n_;
    std::vector<std::uint32_t> t_;
    std::vector<std::uint32_t> pos_;
};

} // namespace

double tour_length(const std::vector<PointD>& points, const std::vector<std::size_t>& order) {
    double total = 0.0;
    for (std::size_t i = 0; i < order.size(); ++i) {
        total += dist(points[order[i]], points[order[(i + 1) % order.size()]]);
    }
    return total;
}

TourResult optimize_tour(const std::vector<PointD>& points, const TourOptions& options) {
    TourResult res;
    const std::size_t n = points.size();
    if (n == 0) {
        return res;
    }
    res.order = hilbert_order(points);
    res.initial_length = tour_length(points, res.order);
    res.length = res.initial_length;
    if (n < 4) {
        return res;
    }
    const int k = std::clamp(options.neighbors, 2, 32);
    const auto neigh = nearest_neighbors(points, k);
    Tour tour(points, res.order);
    std::vector<char> dont_look(n, 0);
    for (int pass = 0; pass < std::max(1, options.max_passes); ++pass) {
        bool any = false;
        for (std::size_t i = 0; i < n; ++i) {
            const auto a = static_cast<std::uint32_t>(i);
            if (dont_look[a]) {
                continue;
            }
            bool improved = false;
            while (tour.two_opt_city(a, neigh, dont_look) ||
                   tour.or_opt_city(a, neigh, dont_look)) {
                improved = true;
                any = true;
            }
            if (!improved) {
                dont_look[a] = 1;
            }
        }
        res.passes = pass + 1;
        if (!any) {
            break;
        }
    }
    res.order = tour.order();
    res.length = tour.length();
    return res;
}

} // namespace openstitch::stipple
