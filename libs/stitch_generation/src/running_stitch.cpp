// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/running_stitch.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "openstitch/geometry/polyline.hpp"

namespace openstitch::stitch_generation {

namespace {

double dot(Vec2um a, Vec2um b) {
    return static_cast<double>(a.x.value) * static_cast<double>(b.x.value) +
           static_cast<double>(a.y.value) * static_cast<double>(b.y.value);
}

// Angle de rotation (rad) au sommet i entre l'arête entrante et sortante.
double turn_angle(Vec2um prev, Vec2um cur, Vec2um next) {
    const Vec2um in = cur - prev;
    const Vec2um out = next - cur;
    const double li = length_um(in);
    const double lo = length_um(out);
    if (li == 0.0 || lo == 0.0) {
        return 0.0;
    }
    const double c = std::clamp(dot(in, out) / (li * lo), -1.0, 1.0);
    return std::acos(c);
}

// Fait démarrer une boucle fermée à l'abscisse curviligne s0 (rotation).
std::vector<Vec2um> rotate_loop_to(const std::vector<Vec2um>& pts, double s0) {
    const auto cum = geometry::cumulative_lengths(pts);
    const double L = cum.back();
    if (L == 0.0) {
        return pts;
    }
    s0 = std::fmod(std::fmod(s0, L) + L, L);
    const Vec2um startPt = geometry::point_at_length(pts, cum, s0);
    // Index du premier sommet situé après s0.
    std::size_t k = 0;
    while (k < cum.size() && cum[k] <= s0) {
        ++k;
    }
    std::vector<Vec2um> out;
    out.reserve(pts.size() + 1);
    out.push_back(startPt);
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const std::size_t idx = (k + i) % pts.size();
        if (out.back() != pts[idx]) {
            out.push_back(pts[idx]);
        }
    }
    return out;
}

// Arc-length de la projection du point q sur la polyligne (sommet le plus proche
// suffisant ici comme approximation stable et déterministe).
double nearest_arc_length(const std::vector<Vec2um>& pts, const std::vector<double>& cum,
                          Vec2um q) {
    double best = 0.0;
    double bestDist = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const double d = length_um(pts[i] - q);
        if (d < bestDist) {
            bestDist = d;
            best = cum[i];
        }
    }
    return best;
}

// Découpe une polyligne (ouverte ou fermée) en tronçons entre coins vifs, puis
// ré-échantillonne chaque tronçon par longueur d'arc et concatène.
std::vector<Vec2um> sample_by_runs(const std::vector<Vec2um>& poly, bool closed, Micrometers target,
                                   double cornerThreshold) {
    std::vector<Vec2um> out;
    const std::size_t m = poly.size();
    if (m < 2) {
        return poly;
    }

    // Indices des coins.
    std::vector<std::size_t> corners;
    if (!closed) {
        corners.push_back(0);
        for (std::size_t i = 1; i + 1 < m; ++i) {
            if (turn_angle(poly[i - 1], poly[i], poly[i + 1]) > cornerThreshold) {
                corners.push_back(i);
            }
        }
        corners.push_back(m - 1);
    } else {
        for (std::size_t i = 0; i < m; ++i) {
            const Vec2um prev = poly[(i + m - 1) % m];
            const Vec2um next = poly[(i + 1) % m];
            if (turn_angle(prev, poly[i], next) > cornerThreshold) {
                corners.push_back(i);
            }
        }
    }

    const auto appendRun = [&](const std::vector<Vec2um>& run) {
        const auto sampled = geometry::resample_run(run, target);
        for (const Vec2um& p : sampled) {
            if (out.empty() || out.back() != p) {
                out.push_back(p);
            }
        }
    };

    if (!closed) {
        for (std::size_t c = 0; c + 1 < corners.size(); ++c) {
            std::vector<Vec2um> run(poly.begin() + static_cast<std::ptrdiff_t>(corners[c]),
                                    poly.begin() + static_cast<std::ptrdiff_t>(corners[c + 1]) + 1);
            appendRun(run);
        }
        return out;
    }

    // Fermé sans coin : boucle unique ré-échantillonnée (fermeture incluse).
    if (corners.empty()) {
        std::vector<Vec2um> loop = poly;
        loop.push_back(poly.front());  // fermeture
        appendRun(loop);
        return out;
    }

    // Fermé avec coins : tronçons autour de la boucle, du coin c au coin suivant.
    for (std::size_t c = 0; c < corners.size(); ++c) {
        const std::size_t a = corners[c];
        const std::size_t b = corners[(c + 1) % corners.size()];
        std::vector<Vec2um> run;
        std::size_t i = a;
        run.push_back(poly[i]);
        do {
            i = (i + 1) % m;
            run.push_back(poly[i]);
        } while (i != b);
        appendRun(run);
    }
    return out;
}

void fill_stats(RunningResult& result) {
    RunningStats& s = result.stats;
    s.stitches = result.points.size();
    s.min_segment_um = std::numeric_limits<double>::max();
    for (std::size_t i = 1; i < result.points.size(); ++i) {
        const double d = length_um(result.points[i] - result.points[i - 1]);
        s.length_um += d;
        s.min_segment_um = std::min(s.min_segment_um, d);
        s.max_segment_um = std::max(s.max_segment_um, d);
    }
    if (result.points.size() < 2) {
        s.min_segment_um = 0.0;
    }
}

}  // namespace

RunningResult run_stitch(const geometry::Path& path, const RunningConfig& config) {
    RunningResult result;
    if (path.nodes.empty()) {
        result.warnings.push_back({WarningCode::PathEmpty, "Chemin vide", {}});
        return result;
    }
    if (config.target_length.value <= 0) {
        result.warnings.push_back(
            {WarningCode::DegenerateGeometry, "Longueur de point invalide", {}});
        return result;
    }

    geometry::Polyline poly = geometry::flatten(path, config.flatten_tolerance);
    if (poly.points.size() < 2) {
        result.warnings.push_back(
            {WarningCode::PathTooShort, "Chemin trop court pour être cousu",
             path.nodes.front().pos});
        result.points = poly.points;
        fill_stats(result);
        return result;
    }

    // Sens de parcours.
    if (config.reverse) {
        std::reverse(poly.points.begin(), poly.points.end());
    }

    // Boucle fermée : départ imposé (projection) ou phase.
    if (poly.closed) {
        const auto cum = geometry::cumulative_lengths(poly.points);
        const double L = cum.back();
        double s0 = 0.0;
        if (config.start) {
            s0 = nearest_arc_length(poly.points, cum, *config.start);
        } else if (config.phase != 0.0) {
            s0 = std::fmod(config.phase, 1.0) * static_cast<double>(config.target_length.value);
            s0 = std::fmod(s0, L);
        }
        if (s0 != 0.0) {
            poly.points = rotate_loop_to(poly.points, s0);
        }
    }

    const double totalLen = geometry::polyline_length(poly.points);
    if (totalLen < static_cast<double>(config.target_length.value)) {
        result.warnings.push_back(
            {WarningCode::PathTooShort,
             "Chemin plus court qu'une longueur de point : un seul segment",
             poly.points.front()});
    }

    result.points = sample_by_runs(poly.points, poly.closed, config.target_length,
                                   config.corner_threshold.radians);

    // Diagnostic longueurs (aucune suppression aveugle : on avertit).
    for (std::size_t i = 1; i < result.points.size(); ++i) {
        const double d = length_um(result.points[i] - result.points[i - 1]);
        if (d > static_cast<double>(config.max_length.value)) {
            result.warnings.push_back(
                {WarningCode::StitchTooLong, "Point trop long", result.points[i]});
        } else if (d > 0.0 && d < static_cast<double>(config.min_length.value)) {
            result.warnings.push_back(
                {WarningCode::StitchTooShort, "Point court (coin serré)", result.points[i]});
        }
    }

    fill_stats(result);
    return result;
}

std::vector<Vec2um> sample_path(const geometry::Path& path, Micrometers target_length,
                                Micrometers min_length) {
    RunningConfig cfg;
    cfg.target_length = target_length;
    cfg.min_length = min_length;
    return run_stitch(path, cfg).points;
}

std::vector<Vec2um> apply_repeat_mode(const std::vector<Vec2um>& points, RepeatMode mode,
                                      int passes) {
    if (points.size() < 2 || mode == RepeatMode::SinglePass) {
        return points;
    }

    const auto push = [](std::vector<Vec2um>& v, Vec2um p) {
        if (v.empty() || v.back() != p) {  // supprime les mouvements nuls
            v.push_back(p);
        }
    };

    std::vector<Vec2um> out;
    switch (mode) {
    case RepeatMode::SinglePass:
        return points;
    case RepeatMode::BackAndForth: {
        out = points;
        for (std::size_t i = points.size() - 1; i-- > 0;) {
            push(out, points[i]);
        }
        return out;
    }
    case RepeatMode::BeanStitch: {
        // Chaque intervalle A->B parcouru `passes` fois (impair) : A B A B A ...
        const int p = std::max(3, passes % 2 == 0 ? passes + 1 : passes);
        push(out, points[0]);
        for (std::size_t i = 1; i < points.size(); ++i) {
            for (int k = 0; k < p; ++k) {
                push(out, (k % 2 == 0) ? points[i] : points[i - 1]);
            }
        }
        return out;
    }
    case RepeatMode::Backstitch: {
        // Progression avec recouvrement : P0 P1 P0 P2 P1 P3 P2 ...
        push(out, points[0]);
        for (std::size_t i = 1; i < points.size(); ++i) {
            push(out, points[i]);
            push(out, points[i - 1]);
        }
        // Termine proprement sur le dernier point.
        push(out, points.back());
        return out;
    }
    }
    return points;
}

std::vector<Vec2um> apply_repeats(const std::vector<Vec2um>& points, int repeats) {
    if (repeats <= 1) {
        return apply_repeat_mode(points, RepeatMode::SinglePass);
    }
    if (repeats == 2) {
        return apply_repeat_mode(points, RepeatMode::BackAndForth);
    }
    return apply_repeat_mode(points, RepeatMode::BeanStitch, repeats);
}

}  // namespace openstitch::stitch_generation
