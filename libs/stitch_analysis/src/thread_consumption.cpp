// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_analysis/thread_consumption.hpp"

#include <algorithm>
#include <cmath>

namespace openstitch::stitch_analysis {

ThreadConsumptionReport thread_consumption(const std::vector<ThreadUsage>& usage,
                                           const ThreadConsumptionOptions& options) {
    ThreadConsumptionReport report;
    const double factor = std::max(0.0, options.top_path_factor);
    const double waste_m = std::max(0.0, options.waste_per_block_mm) / 1000.0;
    const double ratio = std::max(0.0, options.bobbin_per_top_ratio);
    for (const ThreadUsage& u : usage) {
        ThreadConsumption c;
        c.identity = u.identity;
        c.sewn_m = u.length_mm / 1000.0;
        c.top_m = c.sewn_m * factor + waste_m * static_cast<double>(u.color_blocks);
        c.bobbin_m = c.top_m * ratio;
        report.total_top_m += c.top_m;
        report.total_bobbin_m += c.bobbin_m;
        report.per_thread.push_back(std::move(c));
    }
    if (options.bobbin_capacity_m > 0.0 && report.total_bobbin_m > 0.0) {
        const double bobbins = report.total_bobbin_m / options.bobbin_capacity_m;
        report.bobbins_needed = static_cast<std::size_t>(std::ceil(bobbins));
        report.last_bobbin_fill = bobbins - std::floor(bobbins);
        if (report.last_bobbin_fill == 0.0) {
            report.last_bobbin_fill = 1.0; // pile N canettes pleines
        }
    }
    return report;
}

ThreadConsumptionReport thread_consumption(const document::Project& project,
                                           const stitch::StitchSequence& sequence,
                                           const ThreadConsumptionOptions& options) {
    return thread_consumption(thread_usage(project, sequence), options);
}

} // namespace openstitch::stitch_analysis
