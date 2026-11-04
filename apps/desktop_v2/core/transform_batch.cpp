// SPDX-License-Identifier: Apache-2.0
#include "transform_batch.hpp"

#include <cmath>
#include <cstdio>

namespace openstitch::desktop_v2 {

namespace {

std::string num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.4f", v);
    return buf;
}

std::string wrap(const std::string& ops) {
    return R"({"schema":"openstitch.batch/1","operations":[)" + ops + "]}";
}

} // namespace

std::string batchTranslate(const std::vector<std::uint64_t>& ids, double dxMm, double dyMm) {
    if (ids.empty() || !std::isfinite(dxMm) || !std::isfinite(dyMm) || std::abs(dxMm) > 2000.0 ||
        std::abs(dyMm) > 2000.0 || (std::abs(dxMm) < 1e-4 && std::abs(dyMm) < 1e-4)) {
        return {};
    }
    std::string ops;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        ops += std::string(i == 0 ? "" : ",") + R"({"op":"translate","object":)" +
               std::to_string(ids[i]) + R"(,"dx_mm":)" + num(dxMm) + R"(,"dy_mm":)" + num(dyMm) + "}";
    }
    return wrap(ops);
}

std::string batchScale(const std::vector<std::uint64_t>& ids, double factor, PointMm pivot) {
    if (ids.empty() || !std::isfinite(factor) || factor < 0.05 || factor > 20.0 ||
        std::abs(factor - 1.0) < 1e-4 || !std::isfinite(pivot.x) || !std::isfinite(pivot.y) ||
        std::abs(pivot.x) > 1000.0 || std::abs(pivot.y) > 1000.0) {
        return {};
    }
    std::string ops;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        ops += std::string(i == 0 ? "" : ",") + R"({"op":"scale","object":)" +
               std::to_string(ids[i]) + R"(,"factor":)" + num(factor) + R"(,"center_mm":[)" +
               num(pivot.x) + "," + num(pivot.y) + "]}";
    }
    return wrap(ops);
}

} // namespace openstitch::desktop_v2
