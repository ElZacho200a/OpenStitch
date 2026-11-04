// SPDX-License-Identifier: Apache-2.0
#include "edit_batch.hpp"

#include <algorithm>
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

bool pivotOk(PointMm p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::abs(p.x) <= 1000.0 &&
           std::abs(p.y) <= 1000.0;
}

std::string pivotJson(PointMm p) {
    return R"(,"center_mm":[)" + num(p.x) + "," + num(p.y) + "]";
}

} // namespace

std::string batchRemove(const std::vector<std::uint64_t>& ids) {
    if (ids.empty()) {
        return {};
    }
    std::string ops;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        ops += std::string(i == 0 ? "" : ",") + R"({"op":"remove_object","object":)" +
               std::to_string(ids[i]) + "}";
    }
    return wrap(ops);
}

std::string batchDuplicate(const std::vector<std::uint64_t>& ids, PointMm offset) {
    if (ids.empty() || !pivotOk(offset)) {
        return {};
    }
    std::string ops;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        ops += std::string(i == 0 ? "" : ",") + R"({"op":"duplicate_object","object":)" +
               std::to_string(ids[i]) + R"(,"offset_mm":[)" + num(offset.x) + "," + num(offset.y) +
               "]}";
    }
    return wrap(ops);
}

std::string batchRotate(const std::vector<std::uint64_t>& ids, double angleDeg, PointMm pivot) {
    if (ids.empty() || !std::isfinite(angleDeg) || std::abs(angleDeg) > 360.0 ||
        std::abs(angleDeg) < 1e-6 || !pivotOk(pivot)) {
        return {};
    }
    std::string ops;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        ops += std::string(i == 0 ? "" : ",") + R"({"op":"rotate","object":)" +
               std::to_string(ids[i]) + R"(,"angle_deg":)" + num(angleDeg) + pivotJson(pivot) + "}";
    }
    return wrap(ops);
}

std::string batchMirror(const std::vector<std::uint64_t>& ids, MirrorAxis axis, PointMm pivot) {
    if (ids.empty() || !pivotOk(pivot)) {
        return {};
    }
    const char* name = axis == MirrorAxis::Horizontal ? "horizontal" : "vertical";
    std::string ops;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        ops += std::string(i == 0 ? "" : ",") + R"({"op":"mirror","object":)" +
               std::to_string(ids[i]) + R"(,"axis":")" + name + "\"" + pivotJson(pivot) + "}";
    }
    return wrap(ops);
}

std::string batchStack(const std::vector<std::uint64_t>& ids, std::string_view move) {
    if (ids.empty() ||
        (move != "front" && move != "back" && move != "forward" && move != "backward")) {
        return {};
    }
    std::string list;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        list += std::string(i == 0 ? "" : ",") + std::to_string(ids[i]);
    }
    return wrap(R"({"op":"stack_order","objects":[)" + list + R"(],"move":")" + std::string(move) +
                "\"}");
}

std::vector<std::uint64_t> createdObjectIds(const project_ops::BatchReport& report) {
    std::vector<std::uint64_t> out;
    for (const auto& o : report.outcomes) {
        project_ops::BatchReport one;
        one.outcomes.push_back(o);
        if (const auto id = createdObjectId(one)) {
            out.push_back(*id);
        }
    }
    return out;
}

} // namespace openstitch::desktop_v2
