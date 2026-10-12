// SPDX-License-Identifier: Apache-2.0
//
// Stippling de Voronoï pondéré d'après A. Secord, « Weighted Voronoi
// Stippling », NPAR 2002 (relaxation de Lloyd avec centroïdes pondérés par la
// densité), réimplémenté à partir de la publication.
#include "openstitch/stipple/stipple.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace openstitch::stipple {

namespace {

// Générateur xorshift64* : suite identique sur toutes les plateformes (les
// distributions de <random> ne sont pas portables d'une bibliothèque à l'autre).
class Rng {
public:
    explicit Rng(std::uint32_t seed)
        : s_(0x9E3779B97F4A7C15ULL ^ (static_cast<std::uint64_t>(seed) << 17 | seed)) {
        if (s_ == 0) {
            s_ = 0x2545F4914F6CDD1DULL;
        }
        for (int i = 0; i < 8; ++i) {
            next();
        }
    }
    std::uint64_t next() {
        s_ ^= s_ >> 12;
        s_ ^= s_ << 25;
        s_ ^= s_ >> 27;
        return s_ * 0x2545F4914F6CDD1DULL;
    }
    // Uniforme dans [0, 1).
    double uniform() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }

private:
    std::uint64_t s_;
};

// Tirage initial par rejet selon la densité ; repli : pixel non nul le plus
// proche d'un tirage uniforme (la densité n'est jamais nulle partout, vérifié par l'appelant).
std::vector<PointD> initial_points(const DensityMap& d, int count, Rng& rng) {
    std::vector<PointD> pts;
    pts.reserve(static_cast<std::size_t>(count));
    std::int64_t attempts = 0;
    const std::int64_t maxAttempts = static_cast<std::int64_t>(count) * 400 + 1000;
    while (static_cast<int>(pts.size()) < count && attempts < maxAttempts) {
        ++attempts;
        const double x = rng.uniform() * d.width;
        const double y = rng.uniform() * d.height;
        const float v = d.at(std::min(d.width - 1, static_cast<int>(x)),
                             std::min(d.height - 1, static_cast<int>(y)));
        if (rng.uniform() < static_cast<double>(v)) {
            pts.push_back({x, y});
        }
    }
    // Densité très faible : complète sur les pixels non nuls, au hasard.
    while (static_cast<int>(pts.size()) < count) {
        const int x = std::min(d.width - 1, static_cast<int>(rng.uniform() * d.width));
        const int y = std::min(d.height - 1, static_cast<int>(rng.uniform() * d.height));
        if (d.at(x, y) > 0.0F) {
            pts.push_back({x + rng.uniform(), y + rng.uniform()});
        }
    }
    return pts;
}

} // namespace

Result<StippleResult> stipple_points(const DensityMap& density, const StippleOptions& options) {
    if (density.width <= 0 || density.height <= 0 ||
        density.values.size() !=
            static_cast<std::size_t>(density.width) * static_cast<std::size_t>(density.height)) {
        return fail(ErrorCategory::Internal, "Carte de densité invalide");
    }
    if (options.point_count < 1 || options.point_count > 200'000) {
        return fail(ErrorCategory::UserInput, "Nombre de points invalide (1 à 200000)");
    }
    if (options.iterations < 0 || options.iterations > 200) {
        return fail(ErrorCategory::UserInput, "Nombre d'itérations invalide (0 à 200)");
    }
    double total = 0.0;
    for (const float v : density.values) {
        total += static_cast<double>(v);
    }
    if (total <= 0.0) {
        return fail(ErrorCategory::OperationImpossible,
                    "L'image est entièrement vide ou transparente : aucun point à placer");
    }

    Rng rng(options.seed);
    StippleResult result;
    result.points = initial_points(density, options.point_count, rng);
    const std::size_t n = result.points.size();

    // --- Relaxations de Lloyd pondérées -----------------------------------------
    // Grille d'accélération pour « point le plus proche de chaque pixel ».
    const double cell =
        std::max(1.0, std::sqrt(static_cast<double>(density.width) * density.height /
                                std::max<double>(1.0, static_cast<double>(n))));
    const int gx = std::max(1, static_cast<int>(std::ceil(density.width / cell)));
    const int gy = std::max(1, static_cast<int>(std::ceil(density.height / cell)));
    std::vector<std::vector<std::uint32_t>> grid(static_cast<std::size_t>(gx) *
                                                 static_cast<std::size_t>(gy));
    std::vector<double> sw(n), sx(n), sy(n);

    for (int it = 0; it < options.iterations; ++it) {
        for (auto& g : grid) {
            g.clear();
        }
        for (std::size_t i = 0; i < n; ++i) {
            const int cx = std::clamp(static_cast<int>(result.points[i].x / cell), 0, gx - 1);
            const int cy = std::clamp(static_cast<int>(result.points[i].y / cell), 0, gy - 1);
            grid[static_cast<std::size_t>(cy) * static_cast<std::size_t>(gx) +
                 static_cast<std::size_t>(cx)]
                .push_back(static_cast<std::uint32_t>(i));
        }
        std::fill(sw.begin(), sw.end(), 0.0);
        std::fill(sx.begin(), sx.end(), 0.0);
        std::fill(sy.begin(), sy.end(), 0.0);

        for (int y = 0; y < density.height; ++y) {
            for (int x = 0; x < density.width; ++x) {
                const double w = static_cast<double>(density.at(x, y));
                if (w <= 0.0) {
                    continue;
                }
                const double px = x + 0.5;
                const double py = y + 0.5;
                const int cx = std::clamp(static_cast<int>(px / cell), 0, gx - 1);
                const int cy = std::clamp(static_cast<int>(py / cell), 0, gy - 1);
                double best = std::numeric_limits<double>::max();
                std::uint32_t bestI = 0;
                for (int r = 0;; ++r) {
                    for (int yy = cy - r; yy <= cy + r; ++yy) {
                        for (int xx = cx - r; xx <= cx + r; ++xx) {
                            if (xx < 0 || yy < 0 || xx >= gx || yy >= gy) {
                                continue;
                            }
                            if (std::max(std::abs(xx - cx), std::abs(yy - cy)) != r) {
                                continue;
                            }
                            for (const std::uint32_t i :
                                 grid[static_cast<std::size_t>(yy) * static_cast<std::size_t>(gx) +
                                      static_cast<std::size_t>(xx)]) {
                                const double dx = result.points[i].x - px;
                                const double dy = result.points[i].y - py;
                                const double d2 = dx * dx + dy * dy;
                                if (d2 < best || (d2 == best && i < bestI)) {
                                    best = d2;
                                    bestI = i;
                                }
                            }
                        }
                    }
                    const double reach = static_cast<double>(r) * cell;
                    if (best <= reach * reach) {
                        break;
                    }
                    if (cx - r <= 0 && cy - r <= 0 && cx + r >= gx - 1 && cy + r >= gy - 1) {
                        break;
                    }
                }
                sw[bestI] += w;
                sx[bestI] += w * px;
                sy[bestI] += w * py;
            }
        }

        double moved = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            if (sw[i] <= 1e-12) {
                continue; // cellule sans densité : le point reste en place
            }
            const PointD target{sx[i] / sw[i], sy[i] / sw[i]};
            moved += std::hypot(target.x - result.points[i].x, target.y - result.points[i].y);
            result.points[i] = target;
        }
        result.lloyd_iterations = it + 1;
        result.last_move = moved / static_cast<double>(n);
        if (result.last_move < 0.01) {
            break; // convergé
        }
    }
    return result;
}

StitchPath build_stitch_path(const StippleResult& stipple, const DensityMap& density,
                             int image_width, int image_height, const PathOptions& path,
                             const TourOptions& tour) {
    StitchPath out;
    const std::size_t n = stipple.points.size();
    if (n == 0) {
        return out;
    }
    const TourResult t = optimize_tour(stipple.points, tour);
    out.tour_length_px = t.length;
    out.initial_tour_length_px = t.initial_length;

    // Coupe le cycle à son arête la plus longue : chemin ouvert.
    std::size_t cut = 0;
    double longest = -1.0;
    for (std::size_t i = 0; i < n; ++i) {
        const PointD& a = stipple.points[t.order[i]];
        const PointD& b = stipple.points[t.order[(i + 1) % n]];
        const double l = std::hypot(a.x - b.x, a.y - b.y);
        if (l > longest) {
            longest = l;
            cut = i;
        }
    }
    const double maxLinkPx = path.max_link.value > 0 ? static_cast<double>(path.max_link.value) /
                                                           1000.0 / path.mm_per_px.value
                                                     : std::numeric_limits<double>::max();

    const auto to_model = [&](const PointD& p) {
        // Pixel de carte -> pixel d'image -> µm (origine au centre, Y vers le haut).
        const double ix = p.x * density.scale;
        const double iy = p.y * density.scale;
        const double xMm = (ix - image_width / 2.0) * path.mm_per_px.value;
        const double yMm = (image_height / 2.0 - iy) * path.mm_per_px.value;
        return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(xMm * 1000.0))},
                      Micrometers{static_cast<std::int32_t>(std::lround(yMm * 1000.0))}};
    };

    std::vector<Vec2um> current;
    for (std::size_t s = 0; s < n; ++s) {
        const std::size_t idx = (cut + 1 + s) % n;
        const PointD& p = stipple.points[t.order[idx]];
        if (!current.empty() && s > 0) {
            const PointD& prev = stipple.points[t.order[(idx + n - 1) % n]];
            if (std::hypot(p.x - prev.x, p.y - prev.y) * density.scale > maxLinkPx) {
                if (current.size() >= 2) {
                    out.polylines.push_back(std::move(current));
                }
                current.clear();
            }
        }
        const Vec2um v = to_model(p);
        if (current.empty() || current.back() != v) {
            current.push_back(v);
        }
    }
    if (current.size() >= 2) {
        out.polylines.push_back(std::move(current));
    }
    return out;
}

} // namespace openstitch::stipple
