// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace openstitch::stitch_generation::experimental {

// Rotated geometry in the same scanline coordinates as the CPU reference.
struct TatamiEdge {
    double x0;
    double y0;
    double x1;
    double y1;
};

using TatamiRows = std::vector<std::vector<double>>;
struct TatamiScan {
    std::function<std::optional<TatamiRows>(std::span<const double>, std::span<const TatamiEdge>)>
        batch;
    std::function<void()> rejected;
};

// Benchmark-only hook. Set between synchronous sequence generations and keep the callback
// alive until all worker tasks have joined. The callback may run concurrently for several objects.
const TatamiScan* set_tatami_scan(const TatamiScan* scan) noexcept;

} // namespace openstitch::stitch_generation::experimental
