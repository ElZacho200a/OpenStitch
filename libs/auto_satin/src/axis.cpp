// SPDX-License-Identifier: Apache-2.0
#include "axis.hpp"

#include <algorithm>
#include <cmath>

namespace openstitch::auto_satin::detail {

namespace {

std::vector<double> cumulative_length(const std::vector<P2>& pts) {
    std::vector<double> cum(pts.size(), 0.0);
    for (std::size_t i = 1; i < pts.size(); ++i) {
        cum[i] = cum[i - 1] + norm(pts[i] - pts[i - 1]);
    }
    return cum;
}

// Point d'abscisse `s` sur la polyligne (s borné à [0, total]).
P2 point_at(const std::vector<P2>& pts, const std::vector<double>& cum, double s) {
    if (s <= 0.0) {
        return pts.front();
    }
    if (s >= cum.back()) {
        return pts.back();
    }
    const auto it = std::upper_bound(cum.begin(), cum.end(), s);
    const std::size_t hi = static_cast<std::size_t>(it - cum.begin());
    const std::size_t lo = hi - 1;
    const double seg = cum[hi] - cum[lo];
    const double f = seg > 1e-12 ? (s - cum[lo]) / seg : 0.0;
    return pts[lo] + (pts[hi] - pts[lo]) * f;
}

} // namespace

Axis Axis::build(const std::vector<P2>& raw, const AxisParams& params) {
    Axis axis;
    axis.tangent_window_um_ = std::max(params.resample_step_um, params.tangent_window_um);
    if (raw.size() < 2) {
        return axis;
    }
    const auto rawCum = cumulative_length(raw);
    const double total = rawCum.back();
    if (total < 1e-6) {
        return axis;
    }

    // Rééchantillonnage uniforme.
    const double step = std::max(1.0, params.resample_step_um);
    const int n = std::max(2, static_cast<int>(std::ceil(total / step)) + 1);
    std::vector<P2> pts;
    pts.reserve(static_cast<std::size_t>(n));
    for (int k = 0; k < n; ++k) {
        pts.push_back(point_at(raw, rawCum, total * k / (n - 1)));
    }

    // Moyenne mobile symétrique tronquée : les extrémités ne bougent pas, aucun
    // biais n'est introduit près d'elles (la fenêtre rétrécit des deux côtés).
    const double actualStep = total / (n - 1);
    const int radius =
        std::max(0, static_cast<int>(std::lround(params.smooth_radius_um / actualStep)));
    for (int pass = 0; pass < params.smooth_passes && radius > 0; ++pass) {
        std::vector<P2> next(pts.size());
        const int last = static_cast<int>(pts.size()) - 1;
        for (int i = 0; i <= last; ++i) {
            const int r = std::min({radius, i, last - i});
            P2 acc{0.0, 0.0};
            for (int j = i - r; j <= i + r; ++j) {
                acc = acc + pts[static_cast<std::size_t>(j)];
            }
            next[static_cast<std::size_t>(i)] = acc * (1.0 / (2 * r + 1));
        }
        pts = std::move(next);
    }

    axis.points_ = std::move(pts);
    axis.cumulative_ = cumulative_length(axis.points_);
    return axis;
}

Axis Axis::build_closed(const std::vector<P2>& raw, const AxisParams& params) {
    Axis axis;
    axis.tangent_window_um_ = std::max(params.resample_step_um, params.tangent_window_um);
    if (raw.size() < 3) {
        return axis;
    }
    // Polyligne fermée : on ajoute le premier point pour le calcul des longueurs.
    std::vector<P2> loop = raw;
    loop.push_back(raw.front());
    const auto loopCum = cumulative_length(loop);
    const double total = loopCum.back();
    if (total < 1e-6) {
        return axis;
    }
    const double step = std::max(1.0, params.resample_step_um);
    const int n = std::max(3, static_cast<int>(std::ceil(total / step))); // points distincts
    std::vector<P2> pts;
    pts.reserve(static_cast<std::size_t>(n));
    for (int k = 0; k < n; ++k) {
        pts.push_back(point_at(loop, loopCum, total * k / n));
    }
    // Moyenne mobile circulaire (fenêtre complète : pas de bord).
    const double actualStep = total / n;
    const int radius =
        std::min(std::max(0, static_cast<int>(std::lround(params.smooth_radius_um / actualStep))),
                 (n - 1) / 2);
    for (int pass = 0; pass < params.smooth_passes && radius > 0; ++pass) {
        std::vector<P2> next(pts.size());
        for (int i = 0; i < n; ++i) {
            P2 acc{0.0, 0.0};
            for (int j = -radius; j <= radius; ++j) {
                acc = acc + pts[static_cast<std::size_t>(((i + j) % n + n) % n)];
            }
            next[static_cast<std::size_t>(i)] = acc * (1.0 / (2 * radius + 1));
        }
        pts = std::move(next);
    }
    pts.push_back(pts.front()); // clôture
    axis.points_ = std::move(pts);
    axis.cumulative_ = cumulative_length(axis.points_);
    axis.closed_ = true;
    return axis;
}

P2 Axis::position(double s) const {
    if (closed_) {
        const double total = cumulative_.back();
        s = std::fmod(s, total);
        if (s < 0.0) {
            s += total;
        }
    }
    return point_at(points_, cumulative_, s);
}

double Axis::alpha(double s) const {
    if (closed_) {
        const double half = tangent_window_um_ * 0.5;
        const P2 d = position(s + half) - position(s - half);
        return std::atan2(d.y, d.x);
    }
    const double total = length();
    const double sc = std::clamp(s, 0.0, total);
    // Fenêtre symétrique, réduite près des extrémités mais jamais nulle.
    const double half = tangent_window_um_ * 0.5;
    double lo = std::max(0.0, sc - half);
    double hi = std::min(total, sc + half);
    if (hi - lo < 1e-9) {
        lo = std::max(0.0, sc - half);
        hi = std::min(total, lo + tangent_window_um_);
    }
    const P2 d = position(hi) - position(lo);
    return std::atan2(d.y, d.x);
}

} // namespace openstitch::auto_satin::detail
