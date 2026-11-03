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
    f.compression = std::clamp(f.compression, 0.0, 2.0);
    f.max_displacement_mm = std::clamp(f.max_displacement_mm, 0.0, 3.0);
    f.calibrated_confidence = std::clamp(f.calibrated_confidence, 0.0, 1.0);
    f.uncalibrated_confidence = std::clamp(f.uncalibrated_confidence, 0.0, 1.0);
    f.compensation_strength = std::clamp(f.compensation_strength, 0.0, 6.0);
    f.compensation_passes = std::clamp(f.compensation_passes, 1, 6);
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

PullSample PullField::sample(Vec2um p, std::uint32_t min_order) const {
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
    // Direction des fils de chaîne : la souplesse du tissu dépend de l'angle de chaque segment.
    const double gx = std::cos(fabric_.grain_angle_rad);
    const double gy = std::sin(fabric_.grain_angle_rad);

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
        // Tout segment voisin, cousu avant ou après, raidit le tissu (densité de fil et de
        // trous d'aiguille) : il compte dans le tassement et la confiance.
        overlap += w;
        if (s.order < min_order) {
            continue; // cousu avant le point : le tissu avait déjà bougé quand il a été posé
        }
        // Souplesse selon l'angle du segment par rapport à la chaîne : le tissu cède dans le sens
        // de la trame (facteur `grain_anisotropy`) et résiste dans celui de la chaîne. L'axe du
        // segment et sa normale ne voient pas la même rigidité.
        const double along_cos = dirx * gx + diry * gy;
        const double c2 = along_cos * along_cos;
        const double k_axial = c2 + fabric_.grain_anisotropy * (1.0 - c2);
        const double k_lateral = (1.0 - c2) + fabric_.grain_anisotropy * c2;
        // Contraction axiale vers le centre + poussée latérale (Poisson), de même ordre de
        // grandeur.
        const double axial = -fabric_.gain * k_axial * std::clamp(along, -h, h) * w;
        const double lateral =
            fabric_.poisson * fabric_.gain * k_lateral * h * std::tanh(across / sig_n) * w;
        ux += axial * dirx - lateral * diry;
        uy += axial * diry + lateral * dirx;
        ++count;
    }
    if (count == 0) {
        return out;
    }

    // Tassement : la matière comprimée sous un amas de segments résiste de plus en plus, donc le
    // déplacement cumulé sature au lieu de croître linéairement avec la densité.
    const double packing = 1.0 / (1.0 + fabric_.compression * overlap);
    ux *= packing;
    uy *= packing;

    // Plafond doux : tanh(norme / plafond) · plafond, sans écrêtage brutal.
    const double cap = fabric_.max_displacement_mm * kUmPerMm;
    const double norm = std::hypot(ux, uy);
    if (norm > 0.0) {
        const double soft = cap > 0.0 ? cap * std::tanh(norm / cap) : 0.0;
        ux *= soft / norm;
        uy *= soft / norm;
    }

    const double base =
        fabric_.calibrated ? fabric_.calibrated_confidence : fabric_.uncalibrated_confidence;
    out.displacement = {{round_um(ux)}, {round_um(uy)}};
    out.confidence = base / (1.0 + kOverlapPenalty * overlap);
    out.contributors = count;
    return out;
}

Vec2um PullField::compensation_at(Vec2um p, std::uint32_t min_order) const {
    const PullSample s = sample(p, min_order);
    const double scale = s.confidence * fabric_.compensation_strength;
    double x = -static_cast<double>(s.displacement.x.value) * scale;
    double y = -static_cast<double>(s.displacement.y.value) * scale;
    // Renforcer Predicted sans déborder du plafond absolu, même avec beaucoup
    // de sources ou une intensité demandée très élevée.
    const double cap = fabric_.max_displacement_mm * kUmPerMm;
    const double norm = std::hypot(x, y);
    if (norm > cap && norm > 0.0) {
        x *= cap / norm;
        y *= cap / norm;
    }
    return {{round_um(x)}, {round_um(y)}};
}

} // namespace openstitch::stitch_generation
