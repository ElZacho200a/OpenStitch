// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "compute.hpp"

#include <string>

namespace openstitch::compute_experiment {

struct D3d11Adapter {
    std::string name;
    GpuBackend run;
};

[[nodiscard]] std::expected<D3d11Adapter, Error> make_d3d11_adapter();

} // namespace openstitch::compute_experiment
