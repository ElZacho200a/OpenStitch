// SPDX-License-Identifier: Apache-2.0
#include "mat_convert.hpp"

namespace openstitch::image::detail {

cv::Mat mat_view_rgba(const Image& img) {
    return cv::Mat(img.height, img.width, CV_8UC4,
                   const_cast<std::uint8_t*>(img.rgba.data()));
}

Image image_from_mat_rgba(const cv::Mat& mat, bool source_had_alpha) {
    CV_Assert(mat.type() == CV_8UC4);
    Image img;
    img.width = mat.cols;
    img.height = mat.rows;
    img.source_had_alpha = source_had_alpha;
    img.rgba.resize(static_cast<std::size_t>(mat.cols) * static_cast<std::size_t>(mat.rows) * 4);
    for (int row = 0; row < mat.rows; ++row) {
        const auto* src = mat.ptr<std::uint8_t>(row);
        std::copy_n(src, static_cast<std::size_t>(mat.cols) * 4,
                    img.rgba.data() +
                        static_cast<std::size_t>(row) * static_cast<std::size_t>(mat.cols) * 4);
    }
    return img;
}

}  // namespace openstitch::image::detail
