// SPDX-License-Identifier: Apache-2.0
#include "openstitch/auto_satin/raster.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace openstitch::auto_satin {

namespace {

struct BBox {
    double minx, miny, maxx, maxy;
};

BBox bbox(const geometry::Path& p) {
    BBox b{1e18, 1e18, -1e18, -1e18};
    for (const auto& n : p.nodes) {
        b.minx = std::min(b.minx, static_cast<double>(n.pos.x.value));
        b.miny = std::min(b.miny, static_cast<double>(n.pos.y.value));
        b.maxx = std::max(b.maxx, static_cast<double>(n.pos.x.value));
        b.maxy = std::max(b.maxy, static_cast<double>(n.pos.y.value));
    }
    return b;
}

std::vector<cv::Point> to_cv(const geometry::Path& path, const RasterTransform& t) {
    std::vector<cv::Point> out;
    out.reserve(path.nodes.size());
    for (const auto& n : path.nodes) {
        out.emplace_back(static_cast<int>(std::lround(t.col_of(n.pos))),
                         static_cast<int>(std::lround(t.row_of(n.pos))));
    }
    return out;
}

}  // namespace

Result<RasterMask> rasterize(const geometry::PathSet& region,
                             const SkeletonRasterParameters& params) {
    if (region.outer.nodes.size() < 3) {
        return fail(ErrorCategory::UserInput, "Région trop petite pour être rasterisée");
    }
    const BBox b = bbox(region.outer);
    const double wUm = b.maxx - b.minx;
    const double hUm = b.maxy - b.miny;
    if (wUm <= 0.0 || hUm <= 0.0) {
        return fail(ErrorCategory::UserInput, "Région dégénérée");
    }

    double pixel = std::max(1.0, static_cast<double>(params.pixel_size.value));
    const int margin = std::max(1, params.margin_px);
    // Augmente la résolution (pixel plus gros) si les dimensions débordent.
    const auto dims = [&](double px) {
        return std::pair<int, int>{static_cast<int>(std::ceil(wUm / px)) + 2 * margin + 1,
                                   static_cast<int>(std::ceil(hUm / px)) + 2 * margin + 1};
    };
    auto [w, h] = dims(pixel);
    while ((w > params.max_dimension || h > params.max_dimension) && pixel < 1e7) {
        pixel *= 1.5;
        std::tie(w, h) = dims(pixel);
    }

    RasterMask mask;
    mask.width = w;
    mask.height = h;
    mask.transform.pixel_size_um = pixel;
    mask.transform.min_x_um = b.minx - margin * pixel;
    mask.transform.max_y_um = b.maxy + margin * pixel;

    cv::Mat img(h, w, CV_8UC1, cv::Scalar(0));
    const std::vector<std::vector<cv::Point>> outer{to_cv(region.outer, mask.transform)};
    cv::fillPoly(img, outer, cv::Scalar(1), cv::LINE_8);
    for (const auto& hole : region.holes) {
        if (hole.nodes.size() >= 3) {
            const std::vector<std::vector<cv::Point>> hp{to_cv(hole, mask.transform)};
            cv::fillPoly(img, hp, cv::Scalar(0), cv::LINE_8);
        }
    }

    mask.pixels.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
    for (int y = 0; y < h; ++y) {
        const auto* row = img.ptr<std::uint8_t>(y);
        std::copy_n(row, static_cast<std::size_t>(w),
                    mask.pixels.begin() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w));
    }
    return mask;
}

}  // namespace openstitch::auto_satin
