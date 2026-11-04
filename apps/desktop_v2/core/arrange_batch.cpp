// SPDX-License-Identifier: Apache-2.0
#include "arrange_batch.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

#include "openstitch/commands/placement_ops.hpp"

namespace openstitch::desktop_v2 {

namespace {

struct Box {
    std::uint64_t id{0};
    double cx{0}, cy{0}, w{0}, h{0}; // mm
    [[nodiscard]] double minX() const { return cx - w / 2; }
    [[nodiscard]] double maxX() const { return cx + w / 2; }
    [[nodiscard]] double minY() const { return cy - h / 2; }
    [[nodiscard]] double maxY() const { return cy + h / 2; }
};

std::string num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.4f", v);
    return buf;
}

std::string wrap(const std::string& ops) {
    return R"({"schema":"openstitch.batch/1","operations":[)" + ops + "]}";
}

std::vector<Box> boxesOf(const document::Project& project, const std::vector<std::uint64_t>& ids) {
    std::vector<Box> boxes;
    for (const auto id : ids) {
        const auto* emb = project.findEmbroidery(ObjectId{id});
        if (emb == nullptr) {
            continue;
        }
        const auto geometry = commands::selection_geometry(project, {emb->source_vector});
        if (!geometry) {
            continue;
        }
        boxes.push_back(
            {id, to_millimeters(geometry->center.x).value, to_millimeters(geometry->center.y).value,
             to_millimeters(geometry->width).value, to_millimeters(geometry->height).value});
    }
    return boxes;
}

std::string placeOp(std::uint64_t id, std::optional<double> x, std::optional<double> y) {
    std::string op = R"({"op":"place","objects":[)" + std::to_string(id) + "]";
    if (x) {
        op += R"(,"x":)" + num(*x);
    }
    if (y) {
        op += R"(,"y":)" + num(*y);
    }
    return op + "}";
}

bool moved(double from, double to) {
    return std::abs(from - to) > 1e-3;
}

} // namespace

std::string batchAlign(const document::Project& project, const std::vector<std::uint64_t>& ids,
                       AlignMode mode) {
    const auto boxes = boxesOf(project, ids);
    if (boxes.size() < 2) {
        return {};
    }
    double minX = boxes[0].minX(), maxX = boxes[0].maxX();
    double minY = boxes[0].minY(), maxY = boxes[0].maxY();
    for (const auto& b : boxes) {
        minX = std::min(minX, b.minX());
        maxX = std::max(maxX, b.maxX());
        minY = std::min(minY, b.minY());
        maxY = std::max(maxY, b.maxY());
    }
    std::string ops;
    for (const auto& b : boxes) {
        std::optional<double> x;
        std::optional<double> y;
        switch (mode) {
        case AlignMode::Left:
            x = minX + b.w / 2;
            break;
        case AlignMode::CenterH:
            x = (minX + maxX) / 2;
            break;
        case AlignMode::Right:
            x = maxX - b.w / 2;
            break;
        case AlignMode::Top:
            y = maxY - b.h / 2;
            break;
        case AlignMode::CenterV:
            y = (minY + maxY) / 2;
            break;
        case AlignMode::Bottom:
            y = minY + b.h / 2;
            break;
        }
        if ((x && !moved(b.cx, *x)) || (y && !moved(b.cy, *y))) {
            continue;
        }
        ops += (ops.empty() ? "" : ",") + placeOp(b.id, x, y);
    }
    return ops.empty() ? std::string{} : wrap(ops);
}

std::string batchDistribute(const document::Project& project, const std::vector<std::uint64_t>& ids,
                            DistributeAxis axis) {
    auto boxes = boxesOf(project, ids);
    if (boxes.size() < 3) {
        return {};
    }
    const bool horizontal = axis == DistributeAxis::Horizontal;
    std::stable_sort(boxes.begin(), boxes.end(), [&](const Box& a, const Box& b) {
        return horizontal ? a.cx < b.cx : a.cy < b.cy;
    });
    const double first = horizontal ? boxes.front().minX() : boxes.front().minY();
    const double last = horizontal ? boxes.back().maxX() : boxes.back().maxY();
    double occupied = 0;
    for (const auto& b : boxes) {
        occupied += horizontal ? b.w : b.h;
    }
    const double gap = (last - first - occupied) / static_cast<double>(boxes.size() - 1);
    std::string ops;
    double cursor = first;
    for (const auto& b : boxes) {
        const double size = horizontal ? b.w : b.h;
        const double centre = cursor + size / 2;
        if (moved(horizontal ? b.cx : b.cy, centre)) {
            ops += (ops.empty() ? "" : ",") +
                   placeOp(b.id, horizontal ? std::optional<double>(centre) : std::nullopt,
                           horizontal ? std::nullopt : std::optional<double>(centre));
        }
        cursor += size + gap;
    }
    return ops.empty() ? std::string{} : wrap(ops);
}

std::string batchGroup(const std::vector<std::uint64_t>& ids, const std::string& name) {
    if (ids.size() < 2) {
        return {};
    }
    std::string list;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        list += (i == 0 ? "" : ",") + std::to_string(ids[i]);
    }
    std::string safe;
    for (const char c : name) {
        if (c != '"' && c != '\\' && static_cast<unsigned char>(c) >= 0x20) {
            safe += c;
        }
    }
    std::string op = R"({"op":"group","objects":[)" + list + "]";
    if (!safe.empty()) {
        op += R"(,"name":")" + safe + "\"";
    }
    return wrap(op + "}");
}

std::string batchUngroup(const document::Project& project, const std::vector<std::uint64_t>& ids) {
    std::set<std::uint64_t> groups;
    for (const auto id : ids) {
        const auto* emb = project.findEmbroidery(ObjectId{id});
        if (emb == nullptr) {
            continue;
        }
        const auto top = project.topGroupOfShape(emb->source_vector);
        if (top.valid()) {
            groups.insert(top.value);
        }
    }
    std::string ops;
    for (const auto g : groups) {
        ops += (ops.empty() ? "" : ",") + std::string(R"({"op":"ungroup","group":)") +
               std::to_string(g) + "}";
    }
    return ops.empty() ? std::string{} : wrap(ops);
}

std::string batchPlace(const std::vector<std::uint64_t>& ids, const PlaceRequest& request) {
    if (ids.empty()) {
        return {};
    }
    const auto coordOk = [](const std::optional<double>& v) {
        return !v || (std::isfinite(*v) && std::abs(*v) <= 1000.0);
    };
    const auto sizeOk = [](const std::optional<double>& v) {
        return !v || (std::isfinite(*v) && *v >= 0.1 && *v <= 2000.0);
    };
    if (!coordOk(request.xMm) || !coordOk(request.yMm) || !sizeOk(request.widthMm) ||
        !sizeOk(request.heightMm) || !std::isfinite(request.rotateDeg) ||
        std::abs(request.rotateDeg) > 360.0) {
        return {};
    }
    const bool rotates = std::abs(request.rotateDeg) > 1e-4;
    if (!request.xMm && !request.yMm && !request.widthMm && !request.heightMm && !rotates) {
        return {};
    }
    std::string list;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        list += (i == 0 ? "" : ",") + std::to_string(ids[i]);
    }
    std::string op = R"({"op":"place","objects":[)" + list + "]";
    if (request.xMm) {
        op += R"(,"x":)" + num(*request.xMm);
    }
    if (request.yMm) {
        op += R"(,"y":)" + num(*request.yMm);
    }
    if (request.widthMm) {
        op += R"(,"width":)" + num(*request.widthMm);
    }
    if (request.heightMm) {
        op += R"(,"height":)" + num(*request.heightMm);
    }
    if (rotates) {
        op += R"(,"rotate":)" + num(request.rotateDeg);
    }
    return wrap(op + "}");
}

} // namespace openstitch::desktop_v2
