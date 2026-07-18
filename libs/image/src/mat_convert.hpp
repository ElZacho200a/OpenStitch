// SPDX-License-Identifier: Apache-2.0
// En-tête INTERNE à libs/image : les types OpenCV ne sortent pas de la lib.
#pragma once

#include <opencv2/core.hpp>

#include "openstitch/image/image.hpp"

namespace openstitch::image::detail {

// Vue cv::Mat (CV_8UC4, RGBA) sur les pixels d'une Image. Pas de copie :
// la Mat est invalide dès que l'Image source est détruite ou modifiée.
[[nodiscard]] cv::Mat mat_view_rgba(const Image& img);

// Copie une Mat CV_8UC4 (RGBA) vers une Image.
[[nodiscard]] Image image_from_mat_rgba(const cv::Mat& mat, bool source_had_alpha);

}  // namespace openstitch::image::detail
