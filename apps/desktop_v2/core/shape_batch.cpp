// SPDX-License-Identifier: Apache-2.0
#include "shape_batch.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>

namespace openstitch::desktop_v2 {

namespace {

std::string num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.3f", v);
    return buf;
}

bool coordOk(double v) {
    return std::isfinite(v) && std::abs(v) <= 1000.0;
}
bool sizeOk(double v) {
    return std::isfinite(v) && v >= 0.1 && v <= 2000.0;
}

// Le nom est du texte libre : on échappe guillemets, barres obliques inverses et contrôles.
std::string quoted(std::string_view s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            out += ' ';
        } else {
            out += c;
        }
    }
    return out + "\"";
}

std::string wrap(const std::string& op) {
    return R"({"schema":"openstitch.batch/1","operations":[)" + op + "]}";
}

std::string boxShape(std::string_view kind, PointMm c, double w, double h, std::string_view name) {
    if (!coordOk(c.x) || !coordOk(c.y) || !sizeOk(w) || !sizeOk(h)) {
        return {};
    }
    std::string op = R"({"op":"add_shape","shape":")" + std::string(kind) + R"(","center_mm":[)" +
                     num(c.x) + "," + num(c.y) + R"(],"width_mm":)" + num(w) + R"(,"height_mm":)" +
                     num(h);
    if (!name.empty()) {
        op += R"(,"name":)" + quoted(name);
    }
    return wrap(op + "}");
}

} // namespace

std::string batchAddRect(PointMm centre, double widthMm, double heightMm, std::string_view name) {
    return boxShape("rect", centre, widthMm, heightMm, name);
}

std::string batchAddEllipse(PointMm centre, double widthMm, double heightMm,
                            std::string_view name) {
    return boxShape("ellipse", centre, widthMm, heightMm, name);
}

std::string batchAddPolygon(const std::vector<PointMm>& points, std::string_view name) {
    if (points.size() < 3 || points.size() > 2000) {
        return {};
    }
    std::string pts = "[";
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (!coordOk(points[i].x) || !coordOk(points[i].y)) {
            return {};
        }
        pts += (i == 0 ? "[" : ",[") + num(points[i].x) + "," + num(points[i].y) + "]";
    }
    pts += "]";
    std::string op = R"({"op":"add_shape","shape":"polygon","points_mm":)" + pts;
    if (!name.empty()) {
        op += R"(,"name":)" + quoted(name);
    }
    return wrap(op + "}");
}

namespace {
// Premier entier qui suit `key` (clé JSON complète, guillemets compris) ; `[` accepté entre deux.
std::optional<std::uint64_t> integerAfter(const std::string& json, const std::string& key) {
    const auto at = json.find(key);
    if (at == std::string::npos) {
        return std::nullopt;
    }
    std::size_t i = at + key.size();
    while (i < json.size() && (json[i] == ':' || json[i] == '[' ||
                               std::isspace(static_cast<unsigned char>(json[i])))) {
        ++i;
    }
    std::uint64_t value = 0;
    bool any = false;
    while (i < json.size() && std::isdigit(static_cast<unsigned char>(json[i]))) {
        value = value * 10 + static_cast<std::uint64_t>(json[i] - '0');
        any = true;
        ++i;
    }
    return any ? std::optional<std::uint64_t>{value} : std::nullopt;
}
} // namespace

std::optional<std::uint64_t> createdObjectId(const project_ops::BatchReport& report) {
    if (report.outcomes.empty()) {
        return std::nullopt;
    }
    const std::string& json = report.outcomes.front().result_json;
    // `add_shape` répond {"id":N,...} ; les opérations de duplication/découpe {"created":[N,...]}.
    if (const auto id = integerAfter(json, "\"id\"")) {
        return id;
    }
    return integerAfter(json, "\"created\"");
}

} // namespace openstitch::desktop_v2
