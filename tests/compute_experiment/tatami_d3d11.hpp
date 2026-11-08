// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "compute.hpp"

#include <string>

namespace openstitch::compute_experiment {

struct TatamiBatchJob {
    double x;
    double y;
    double dx;
    double dy;
    std::uint32_t first_row;
    std::uint32_t row_count;
    std::uint32_t output_offset;
    std::uint32_t padding{0};
};
static_assert(sizeof(double) == 8);
static_assert(sizeof(TatamiBatchJob) == 48);
static_assert(offsetof(TatamiBatchJob, first_row) == 32);
static_assert(offsetof(TatamiBatchJob, padding) == 44);

struct TatamiD3d11Adapter {
    std::string name;
    // Jobs cover consecutive, disjoint output ranges. An empty optional requests CPU fallback.
    std::function<std::optional<std::vector<double>>(std::span<const double> rows,
                                                     std::span<const TatamiBatchJob> jobs,
                                                     std::size_t output_count)>
        run;
};

[[nodiscard]] std::expected<TatamiD3d11Adapter, Error> make_tatami_d3d11_adapter();

} // namespace openstitch::compute_experiment
