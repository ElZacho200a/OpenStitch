// SPDX-License-Identifier: Apache-2.0
#include "openstitch/auto_satin/distance_field.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>

namespace openstitch::auto_satin {

float DistanceField::max_value() const {
    float m = 0.0f;
    for (const float v : distance_um) {
        m = std::max(m, v);
    }
    return m;
}

DistanceField distance_transform(const RasterMask& mask) {
    DistanceField df;
    df.width = mask.width;
    df.height = mask.height;
    df.transform = mask.transform;
    df.distance_um.assign(mask.pixels.size(), 0.0f);
    if (mask.width <= 0 || mask.height <= 0) {
        return df;
    }

    cv::Mat bin(mask.height, mask.width, CV_8UC1);
    for (int y = 0; y < mask.height; ++y) {
        auto* row = bin.ptr<std::uint8_t>(y);
        for (int x = 0; x < mask.width; ++x) {
            row[x] = mask.at(x, y) ? 255 : 0;
        }
    }
    cv::Mat dist;
    cv::distanceTransform(bin, dist, cv::DIST_L2, 3);  // en pixels
    const float px = static_cast<float>(mask.transform.pixel_size_um);
    for (int y = 0; y < mask.height; ++y) {
        const auto* row = dist.ptr<float>(y);
        for (int x = 0; x < mask.width; ++x) {
            df.distance_um[static_cast<std::size_t>(y) * static_cast<std::size_t>(mask.width) +
                           static_cast<std::size_t>(x)] = row[x] * px;
        }
    }
    return df;
}

}  // namespace openstitch::auto_satin
