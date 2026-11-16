// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/crosshatch_fill.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <tuple>

namespace openstitch::stitch_generation {

namespace {

struct Dir {
    double x;
    double y;
};

// Décalages (projection sur la normale `n`) des rangées réellement cousues d'une couche de
// direction `d` : seuls les points cousus parallèles à la rangée comptent ; les liaisons entre
// rangées et les sauts sont ignorés. Triés, sans doublon (tolérance 50 µm).
std::vector<double> row_offsets(const std::vector<FillStitch>& part, Dir d) {
    std::vector<double> offs;
    for (std::size_t i = 1; i < part.size(); ++i) {
        if (part[i].jump || part[i].travel) {
            continue;
        }
        const double vx = double(part[i].pos.x.value - part[i - 1].pos.x.value);
        const double vy = double(part[i].pos.y.value - part[i - 1].pos.y.value);
        const double len = std::hypot(vx, vy);
        if (len < 200.0 || std::abs(vx * d.y - vy * d.x) > 0.02 * len) {
            continue;
        }
        const double mx = 0.5 * double(part[i].pos.x.value + part[i - 1].pos.x.value);
        const double my = 0.5 * double(part[i].pos.y.value + part[i - 1].pos.y.value);
        offs.push_back(-d.y * mx + d.x * my);
    }
    // total-order-ok: valeurs scalaires : les egalites sont indiscernables
    std::ranges::sort(offs);
    std::vector<double> out;
    for (const double o : offs) {
        if (out.empty() || o - out.back() > 50.0) {
            out.push_back(o);
        }
    }
    return out;
}

Vec2um at(double x, double y) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(x))},
                  Micrometers{static_cast<std::int32_t>(std::lround(y))}};
}

// Treillis attaché : une petite barre diagonale cousue à chaque croisement des rangées des deux
// premières couches, reliée à la suivante en suivant le fil posé de la première couche.
void append_tie_downs(const geometry::PathSet& region, const std::vector<FillStitch>& first, Dir d0,
                      const std::vector<FillStitch>& second, Dir d1, double half, double stitchLen,
                      std::vector<FillStitch>& out) {
    const auto offs0 = row_offsets(first, d0);
    const auto offs1 = row_offsets(second, d1);
    const double det = d0.x * d1.y - d0.y * d1.x; // sin(θ1 − θ0)
    if (offs0.empty() || offs1.empty() || std::abs(det) < 0.2) {
        return;
    }
    double ux = d0.x + d1.x;
    double uy = d0.y + d1.y;
    const double ul = std::hypot(ux, uy);
    ux /= ul;
    uy /= ul;
    RegionSegmentTester inside(region);
    bool forward = true;
    for (const double a : offs0) {
        std::vector<std::pair<double, Vec2um>> ties; // (abscisse le long de d0, croisement)
        for (const double b : offs1) {
            // n0·p = a, n1·p = b avec n = (−d.y, d.x).
            const double px = (a * d1.x - b * d0.x) / det;
            const double py = (a * d1.y - b * d0.y) / det;
            const Vec2um lo = at(px - half * ux, py - half * uy);
            const Vec2um hi = at(px + half * ux, py + half * uy);
            if (inside.stays_inside(lo, hi)) {
                ties.emplace_back(px * d0.x + py * d0.y, at(px, py));
            }
        }
        if (ties.empty()) {
            continue;
        }
        // total-order-ok: cle (abscisse, x, y) totale
        std::ranges::sort(ties, [](const auto& l, const auto& r) {
            return std::tie(l.first, l.second.x.value, l.second.y.value) <
                   std::tie(r.first, r.second.x.value, r.second.y.value);
        });
        if (!forward) {
            std::ranges::reverse(ties);
        }
        forward = !forward;
        for (std::size_t k = 0; k < ties.size(); ++k) {
            const Vec2um p = ties[k].second;
            if (k == 0 || !inside.stays_inside(ties[k - 1].second, p)) {
                out.push_back({p, true, false});
            } else {
                // Liaison cousue le long du fil posé : invisible sous lui, aucune coupe.
                const Vec2um q = ties[k - 1].second;
                const double dx = double(p.x.value - q.x.value);
                const double dy = double(p.y.value - q.y.value);
                const int n = std::max(1, int(std::ceil(std::hypot(dx, dy) / stitchLen)));
                for (int s = 1; s <= n; ++s) {
                    const double t = double(s) / double(n);
                    out.push_back({at(q.x.value + t * dx, q.y.value + t * dy), false, false});
                }
            }
            const double cx = p.x.value;
            const double cy = p.y.value;
            out.push_back({at(cx + half * ux, cy + half * uy), false, false});
            out.push_back({at(cx - half * ux, cy - half * uy), false, false});
            out.push_back({p, false, false});
        }
    }
}

} // namespace

std::vector<FillStitch> fill_crosshatch(const geometry::PathSet& region,
                                        const document::CrosshatchFillParams& params) {
    const int layers = std::clamp(params.layers, 2, 3);
    std::vector<FillStitch> out;
    std::vector<std::vector<FillStitch>> parts;
    std::vector<Dir> dirs;
    const double stitchLen = double(std::clamp(params.stitch_length.value, 1'000, 7'000));
    for (int k = 0; k < layers; ++k) {
        document::TatamiParams layer;
        layer.angle = Angle{params.angle.radians + std::numbers::pi * double(k) / double(layers)};
        layer.row_spacing = Micrometers{std::clamp(params.row_spacing.value, 300, 10'000)};
        layer.stitch_length = Micrometers{static_cast<std::int32_t>(stitchLen)};
        layer.inset = Micrometers{0};
        layer.hidden_underpath = params.hidden_underpath;
        layer.pull_compensation = params.pull_compensation; // HP-STI-033
        std::vector<FillStitch> part = fill_tatami(region, layer);
        parts.push_back(part);
        dirs.push_back({std::cos(layer.angle.radians), std::sin(layer.angle.radians)});
        if (part.empty()) {
            continue;
        }
        if (!out.empty()) {
            part.front().jump = true;
            part.front().travel = false;
        }
        out.insert(out.end(), part.begin(), part.end());
    }
    // STCH-CROS-01 : attaches aux croisements (treillis). Rangées d'au moins 1 mm seulement :
    // en deçà le croisé est un aplat, et le nombre de croisements croîtrait sans borne utile.
    if (params.tie_down.value > 0 && params.row_spacing.value >= 1'000 && !out.empty()) {
        const double half = 0.5 * double(std::clamp(params.tie_down.value, 1'200, 4'000));
        std::vector<FillStitch> ties;
        append_tie_downs(region, parts[0], dirs[0], parts[1], dirs[1], half, stitchLen, ties);
        out.insert(out.end(), ties.begin(), ties.end());
    }
    return out;
}

} // namespace openstitch::stitch_generation
