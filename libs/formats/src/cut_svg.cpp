// SPDX-License-Identifier: Apache-2.0
#include "openstitch/formats/cut_svg.hpp"

#include <fmt/format.h>

#include <algorithm>

#include "openstitch/core/atomic_file.hpp"
#include "openstitch/geometry/polyline.hpp"

namespace openstitch::formats {

std::string encode_cut_svg(const std::vector<geometry::Path>& paths) {
    std::vector<geometry::Polyline> flat;
    flat.reserve(paths.size());
    bool any = false;
    int minX = 0;
    int maxX = 0;
    int minY = 0;
    int maxY = 0;
    for (const geometry::Path& path : paths) {
        geometry::Polyline poly = geometry::flatten(path, Micrometers{50});
        if (poly.points.size() < 2) {
            continue;
        }
        for (const Vec2um& p : poly.points) {
            if (!any) {
                minX = maxX = p.x.value;
                minY = maxY = p.y.value;
                any = true;
            }
            minX = std::min(minX, p.x.value);
            maxX = std::max(maxX, p.x.value);
            minY = std::min(minY, p.y.value);
            maxY = std::max(maxY, p.y.value);
        }
        poly.closed = path.closed;
        flat.push_back(std::move(poly));
    }
    const double w = static_cast<double>(maxX - minX) / 1000.0;
    const double h = static_cast<double>(maxY - minY) / 1000.0;
    std::string svg =
        fmt::format("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 {:.3f} {:.3f}\" "
                    "width=\"{:.3f}mm\" height=\"{:.3f}mm\">\n",
                    w, h, w, h);
    svg += "<!-- contour de decoupe d'applique (mm) -->\n";
    for (const geometry::Polyline& poly : flat) {
        std::string d;
        for (std::size_t i = 0; i < poly.points.size(); ++i) {
            const double x = static_cast<double>(poly.points[i].x.value - minX) / 1000.0;
            const double y = static_cast<double>(maxY - poly.points[i].y.value) / 1000.0;
            d += fmt::format("{}{:.3f} {:.3f}", i == 0 ? "M" : "L", x, y);
        }
        if (poly.closed) {
            d += "Z";
        }
        svg += fmt::format(
            "<path d=\"{}\" fill=\"none\" stroke=\"#ff0000\" stroke-width=\"0.1\"/>\n", d);
    }
    svg += "</svg>\n";
    return svg;
}

Result<void> write_cut_svg_file(const std::filesystem::path& path,
                                const std::vector<geometry::Path>& paths) {
    return write_file_atomic(path, std::string_view(encode_cut_svg(paths)));
}

} // namespace openstitch::formats
