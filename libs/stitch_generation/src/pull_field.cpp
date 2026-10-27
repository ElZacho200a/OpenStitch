// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/pull_field.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace openstitch::stitch_generation {

namespace {

constexpr double kUmPerMm = 1000.0;
constexpr double kCutoffSigmas = 3.0;
constexpr double kOverlapPenalty = 0.15; // confiance / (1 + k · somme des poids)

std::int32_t round_um(double v) {
    return static_cast<std::int32_t>(std::lround(v));
}

FabricResponse sanitized(FabricResponse f) {
    f.gain = std::clamp(f.gain, 0.0, 0.2);
    f.poisson = std::clamp(f.poisson, 0.0, 1.0);
    f.sigma_along_mm = std::max(f.sigma_along_mm, 0.05);
    f.sigma_across_mm = std::max(f.sigma_across_mm, 0.05);
    f.grain_anisotropy = std::clamp(f.grain_anisotropy, 0.25, 4.0);
    f.max_displacement_mm = std::clamp(f.max_displacement_mm, 0.0, 3.0);
    f.calibrated_confidence = std::clamp(f.calibrated_confidence, 0.0, 1.0);
    f.uncalibrated_confidence = std::clamp(f.uncalibrated_confidence, 0.0, 1.0);
    return f;
}

} // namespace

PullField::PullField(std::vector<PullSource> sources, const FabricResponse& fabric)
    : sources_(std::move(sources)), fabric_(sanitized(fabric)) {
    cutoff_um_ =
        kCutoffSigmas * std::max(fabric_.sigma_along_mm, fabric_.sigma_across_mm) * kUmPerMm;
    cell_um_ = cutoff_um_;
    for (std::size_t i = 0; i < sources_.size(); ++i) {
        const PullSource& s = sources_[i];
        if (s.a == s.b || !(s.weight > 0.0)) {
            continue; // segment nul ou sans poids : aucune traction
        }
        const double x0 = std::min(s.a.x.value, s.b.x.value) - cutoff_um_;
        const double x1 = std::max(s.a.x.value, s.b.x.value) + cutoff_um_;
        const double y0 = std::min(s.a.y.value, s.b.y.value) - cutoff_um_;
        const double y1 = std::max(s.a.y.value, s.b.y.value) + cutoff_um_;
        const auto cx0 = static_cast<std::int64_t>(std::floor(x0 / cell_um_));
        const auto cx1 = static_cast<std::int64_t>(std::floor(x1 / cell_um_));
        const auto cy0 = static_cast<std::int64_t>(std::floor(y0 / cell_um_));
        const auto cy1 = static_cast<std::int64_t>(std::floor(y1 / cell_um_));
        for (std::int64_t cy = cy0; cy <= cy1; ++cy) {
            for (std::int64_t cx = cx0; cx <= cx1; ++cx) {
                grid_[key_of(cx, cy)].push_back(static_cast<std::uint32_t>(i));
            }
        }
    }
}

std::int64_t PullField::key_of(std::int64_t cx, std::int64_t cy) const {
    // Clé ordonnée et sans collision tant que |cx|, |cy| < 2^30 (cellules d'au moins 1 µm).
    return (cy + (std::int64_t{1} << 30)) * (std::int64_t{1} << 31) +
           (cx + (std::int64_t{1} << 30));
}

PullSample PullField::sample(Vec2um p) const {
    PullSample out;
    const auto cx = static_cast<std::int64_t>(std::floor(p.x.value / cell_um_));
    const auto cy = static_cast<std::int64_t>(std::floor(p.y.value / cell_um_));
    const auto it = grid_.find(key_of(cx, cy));
    if (it == grid_.end()) {
        return out;
    }

    const double sig_a = fabric_.sigma_along_mm * kUmPerMm;
    const double sig_n = fabric_.sigma_across_mm * kUmPerMm;
    double ux = 0.0;
    double uy = 0.0;
    double overlap = 0.0;
    std::uint32_t count = 0;

    for (const std::uint32_t idx : it->second) { // indices croissants : ordre déterministe
        const PullSource& s = sources_[idx];
        const double ax = s.a.x.value;
        const double ay = s.a.y.value;
        const double dx = s.b.x.value - ax;
        const double dy = s.b.y.value - ay;
        const double len = std::hypot(dx, dy);
        const double h = len * 0.5;
        const double dirx = dx / len;
        const double diry = dy / len;
        const double rx = p.x.value - (ax + dx * 0.5);
        const double ry = p.y.value - (ay + dy * 0.5);
        const double along = rx * dirx + ry * diry;   // s : abscisse le long du segment
        const double across = -rx * diry + ry * dirx; // t : abscisse perpendiculaire

        const double over = std::max(std::abs(along) - h, 0.0);
        const double w = s.weight * std::exp(-over * over / (2.0 * sig_a * sig_a)) *
                         std::exp(-across * across / (2.0 * sig_n * sig_n));
        if (w < 1e-4) {
            continue;
        }
        // Contraction axiale vers le centre + poussée latérale (Poisson), de même ordre de
        // grandeur.
        const double axial = -fabric_.gain * std::clamp(along, -h, h) * w;
        const double lateral = fabric_.poisson * fabric_.gain * h * std::tanh(across / sig_n) * w;
        ux += axial * dirx - lateral * diry;
        uy += axial * diry + lateral * dirx;
        overlap += w;
        ++count;
    }
    if (count == 0) {
        return out;
    }

    // Anisotropie : le déplacement perpendiculaire aux fils de chaîne est multiplié par la
    // souplesse relative de la trame.
    const double gx = std::cos(fabric_.grain_angle_rad);
    const double gy = std::sin(fabric_.grain_angle_rad);
    const double ug = ux * gx + uy * gy;
    const double up = (-ux * gy + uy * gx) * fabric_.grain_anisotropy;
    ux = ug * gx - up * gy;
    uy = ug * gy + up * gx;

    const double cap = fabric_.max_displacement_mm * kUmPerMm;
    const double norm = std::hypot(ux, uy);
    if (norm > cap && norm > 0.0) {
        ux *= cap / norm;
        uy *= cap / norm;
    }

    const double base =
        fabric_.calibrated ? fabric_.calibrated_confidence : fabric_.uncalibrated_confidence;
    out.displacement = {{round_um(ux)}, {round_um(uy)}};
    out.confidence = base / (1.0 + kOverlapPenalty * overlap);
    out.contributors = count;
    return out;
}

Vec2um PullField::compensation_at(Vec2um p) const {
    const PullSample s = sample(p);
    return {{round_um(-static_cast<double>(s.displacement.x.value) * s.confidence)},
            {round_um(-static_cast<double>(s.displacement.y.value) * s.confidence)}};
}

} // namespace openstitch::stitch_generation
