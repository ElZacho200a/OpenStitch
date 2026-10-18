// SPDX-License-Identifier: Apache-2.0
#include "openstitch/auto_satin/satin_strategy.hpp"

#include <algorithm>
#include <cmath>

#include "openstitch/auto_satin/medial_axis_satin.hpp"
#include "openstitch/auto_satin/parallel_sweep.hpp"

namespace openstitch::auto_satin {

namespace {

double length_mm(Vec2um a, Vec2um b) {
    const double dx = static_cast<double>(b.x.value) - static_cast<double>(a.x.value);
    const double dy = static_cast<double>(b.y.value) - static_cast<double>(a.y.value);
    return std::hypot(dx, dy) / 1000.0;
}

// Intersection stricte de deux traversées (marges de 2 % aux extrémités : les voisines
// partagent des sommets et se touchent sans se couper).
bool crosses(const SkeletonSatinCrossing& p, const SkeletonSatinCrossing& q) {
    const double x1 = p.a.x.value, y1 = p.a.y.value;
    const double rx = p.b.x.value - x1, ry = p.b.y.value - y1;
    const double x3 = q.a.x.value, y3 = q.a.y.value;
    const double wx = q.b.x.value - x3, wy = q.b.y.value - y3;
    const double den = rx * wy - ry * wx;
    if (std::abs(den) < 1e-9) {
        return false;
    }
    const double t = ((x3 - x1) * wy - (y3 - y1) * wx) / den;
    const double u = ((x3 - x1) * ry - (y3 - y1) * rx) / den;
    return t > 0.02 && t < 0.98 && u > 0.02 && u < 0.98;
}

} // namespace

std::string_view strategy_id(SatinStrategy strategy) {
    switch (strategy) {
    case SatinStrategy::Skeleton:
        return "skeleton";
    case SatinStrategy::ParallelSweep:
        return "parallel_sweep";
    case SatinStrategy::MedialAxis:
        return "medial_axis";
    }
    return "skeleton";
}

std::string_view strategy_label(SatinStrategy strategy) {
    switch (strategy) {
    case SatinStrategy::Skeleton:
        return "Squelette";
    case SatinStrategy::ParallelSweep:
        return "Balayage parallèle";
    case SatinStrategy::MedialAxis:
        return "Axe médian vectoriel";
    }
    return "Squelette";
}

std::vector<SatinStrategy> available_strategies() {
    return {SatinStrategy::Skeleton, SatinStrategy::ParallelSweep, SatinStrategy::MedialAxis};
}

Result<SkeletonSatinResult> generate_satin(const geometry::PathSet& region,
                                           const SkeletonSatinParameters& params) {
    switch (params.strategy) {
    case SatinStrategy::Skeleton:
        return generate_skeleton_satin(region, params);
    case SatinStrategy::ParallelSweep:
        return generate_parallel_sweep(region, params);
    case SatinStrategy::MedialAxis:
        return generate_medial_axis_satin(region, params);
    }
    return generate_skeleton_satin(region, params);
}

Result<SkeletonSatinResult> generate_satin(SatinStrategy strategy, const geometry::PathSet& region,
                                           SkeletonSatinParameters params) {
    params.strategy = strategy;
    return generate_satin(region, params);
}

SatinMetrics measure_result(const SkeletonSatinResult& result) {
    SatinMetrics m;
    m.columns = static_cast<int>(result.columns.size());
    double sum = 0.0;
    double lo = 1e18;
    for (const auto& column : result.columns) {
        const auto& c = column.crossings;
        m.crossings += static_cast<int>(c.size());
        for (std::size_t i = 0; i < c.size(); ++i) {
            const double len = length_mm(c[i].a, c[i].b);
            sum += len;
            lo = std::min(lo, len);
            m.max_crossing_mm = std::max(m.max_crossing_mm, len);
            if (i + 1 < c.size()) {
                m.max_return_mm = std::max(m.max_return_mm, length_mm(c[i].b, c[i + 1].a));
            }
            for (std::size_t j = i + 1; j < c.size(); ++j) {
                if (crosses(c[i], c[j])) {
                    ++m.crossing_pairs;
                }
            }
        }
    }
    if (m.crossings > 0) {
        m.min_crossing_mm = lo;
        m.mean_crossing_mm = sum / static_cast<double>(m.crossings);
    }
    const auto& d = result.diagnostics;
    if (d.coverage_measured) {
        m.coverage_ratio = d.coverage_ratio;
        m.overlap_ratio = d.overlap_ratio;
        m.overlap_peak = d.overlap_peak;
        m.uncovered_area_mm2 = d.uncovered_area_mm2;
        m.uncovered_min = d.uncovered_min;
        m.uncovered_max = d.uncovered_max;
    }
    m.refusals = static_cast<int>(d.messages.size());
    return m;
}

Result<SatinMetrics> measure_satin(SatinStrategy strategy, const geometry::PathSet& region,
                                   SkeletonSatinParameters params) {
    params.measure_coverage = true;
    auto result = generate_satin(strategy, region, params);
    if (!result) {
        return std::unexpected(result.error());
    }
    return measure_result(*result);
}

} // namespace openstitch::auto_satin
