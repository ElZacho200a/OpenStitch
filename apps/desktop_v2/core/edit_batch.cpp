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

std::string batchSplit(const std::vector<std::uint64_t>& ids, PointMm p0, PointMm p1) {
    if (ids.empty() || !pivotOk(p0) || !pivotOk(p1) ||
        std::hypot(p1.x - p0.x, p1.y - p0.y) < 1e-3) {
        return {};
    }
    std::string ops;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        ops += std::string(i == 0 ? "" : ",") + R"({"op":"split","object":)" +
               std::to_string(ids[i]) + R"(,"line_mm":[[)" + num(p0.x) + "," + num(p0.y) + "],[" +
               num(p1.x) + "," + num(p1.y) + "]]}";
    }
    return wrap(ops);
}

std::vector<PointMm> simplifyPath(const std::vector<PointMm>& path, double toleranceMm) {
    if (path.size() < 3 || toleranceMm <= 0.0) {
        return path;
    }
    std::vector<bool> keep(path.size(), false);
    keep.front() = true;
    keep.back() = true;
    // Pile explicite : un tracé de plusieurs milliers de points ne doit pas récurser.
    std::vector<std::pair<std::size_t, std::size_t>> stack{{0, path.size() - 1}};
    while (!stack.empty()) {
        const auto [lo, hi] = stack.back();
        stack.pop_back();
        const PointMm a = path[lo];
        const PointMm b = path[hi];
        const double len = std::hypot(b.x - a.x, b.y - a.y);
        double worst = 0.0;
        std::size_t at = lo;
        for (std::size_t i = lo + 1; i < hi; ++i) {
            const double d =
                len < 1e-12
                    ? std::hypot(path[i].x - a.x, path[i].y - a.y)
                    : std::abs((b.x - a.x) * (a.y - path[i].y) - (a.x - path[i].x) * (b.y - a.y)) /
                          len;
            if (d > worst) {
                worst = d;
                at = i;
            }
        }
        if (worst > toleranceMm) {
            keep[at] = true;
            stack.emplace_back(lo, at);
            stack.emplace_back(at, hi);
        }
    }
    std::vector<PointMm> out;
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (keep[i]) {
            out.push_back(path[i]);
        }
    }
    return out;
}

std::string batchSplitPath(const std::vector<std::uint64_t>& ids,
                           const std::vector<PointMm>& path) {
    // Au plus 2000 points côté moteur ; un tracé plus long est refusé plutôt que tronqué.
    if (ids.empty() || path.size() < 2 || path.size() > 2000) {
        return {};
    }
    double length = 0.0;
    std::string points;
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (!pivotOk(path[i])) {
            return {};
        }
        if (i > 0) {
            length += std::hypot(path[i].x - path[i - 1].x, path[i].y - path[i - 1].y);
        }
        points +=
            std::string(i == 0 ? "" : ",") + "[" + num(path[i].x) + "," + num(path[i].y) + "]";
    }
    if (length < 1e-3) {
        return {};
    }
    std::string ops;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        ops += std::string(i == 0 ? "" : ",") + R"({"op":"split_path","object":)" +
               std::to_string(ids[i]) + R"(,"path_mm":[)" + points + "]}";
    }
    return wrap(ops);
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
