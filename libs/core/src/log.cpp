// SPDX-License-Identifier: Apache-2.0
#include "openstitch/core/log.hpp"

#include <spdlog/sinks/stderr_color_sinks.h>

namespace openstitch {

void init_logging(bool verbose) {
    auto logger = spdlog::stderr_color_mt("openstitch");
    spdlog::set_default_logger(std::move(logger));
    spdlog::set_level(verbose ? spdlog::level::debug : spdlog::level::info);
    spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
}

}  // namespace openstitch
