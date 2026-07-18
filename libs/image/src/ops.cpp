// SPDX-License-Identifier: Apache-2.0
#include "openstitch/image/ops.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <random>

#include "mat_convert.hpp"

namespace openstitch::image {

namespace {

using detail::image_from_mat_rgba;
using detail::mat_view_rgba;

Result<Image> apply_crop(const Image& in, const CropOp& op) {
    const cv::Rect wanted(op.x, op.y, op.width, op.height);
    const cv::Rect bounds(0, 0, in.width, in.height);
    const cv::Rect rect = wanted & bounds;
    if (rect.width <= 0 || rect.height <= 0) {
        return fail(ErrorCategory::UserInput, "Zone de recadrage vide ou hors de l'image");
    }
    const cv::Mat src = mat_view_rgba(in);
    return image_from_mat_rgba(src(rect).clone(), in.source_had_alpha);
}

Result<Image> apply_flip(const Image& in, const FlipOp& op) {
    const cv::Mat src = mat_view_rgba(in);
    cv::Mat dst;
    cv::flip(src, dst, op.horizontal ? 1 : 0);
    return image_from_mat_rgba(dst, in.source_had_alpha);
}

Result<Image> apply_rotate90(const Image& in, const Rotate90Op& op) {
    if (op.quarter_turns < 1 || op.quarter_turns > 3) {
        return fail(ErrorCategory::Internal, "Rotation invalide",
                    "quarter_turns=" + std::to_string(op.quarter_turns));
    }
    static constexpr cv::RotateFlags flags[] = {cv::ROTATE_90_CLOCKWISE, cv::ROTATE_180,
                                                cv::ROTATE_90_COUNTERCLOCKWISE};
    const cv::Mat src = mat_view_rgba(in);
    cv::Mat dst;
    cv::rotate(src, dst, flags[op.quarter_turns - 1]);
    return image_from_mat_rgba(dst, in.source_had_alpha);
}

Result<Image> apply_grayscale(const Image& in) {
    const cv::Mat src = mat_view_rgba(in);
    cv::Mat gray;
    cv::cvtColor(src, gray, cv::COLOR_RGBA2GRAY);
    std::vector<cv::Mat> planes;
    cv::split(src, planes);  // récupère le canal alpha d'origine
    cv::Mat dst;
    cv::merge(std::vector<cv::Mat>{gray, gray, gray, planes[3]}, dst);
    return image_from_mat_rgba(dst, in.source_had_alpha);
}

Result<Image> apply_brightness_contrast(const Image& in, const BrightnessContrastOp& op) {
    const double b = std::clamp(op.brightness, -100.0, 100.0) * 1.28;  // -128..128
    const double c = std::clamp(op.contrast, -100.0, 100.0);
    const double k = (c >= 0.0) ? 1.0 + c / 50.0 : 1.0 + c / 100.0;  // pente 0..3

    const cv::Mat src = mat_view_rgba(in);
    std::vector<cv::Mat> planes;
    cv::split(src, planes);
    for (int i = 0; i < 3; ++i) {  // l'alpha n'est pas touché
        planes[static_cast<std::size_t>(i)].convertTo(planes[static_cast<std::size_t>(i)], -1, k,
                                                      128.0 * (1.0 - k) + b);
    }
    cv::Mat dst;
    cv::merge(planes, dst);
    return image_from_mat_rgba(dst, in.source_had_alpha);
}

Result<Image> apply_median_denoise(const Image& in, const MedianDenoiseOp& op) {
    if (op.strength < 1 || op.strength > 2) {
        return fail(ErrorCategory::UserInput, "Force de débruitage invalide (1 ou 2)");
    }
    const int ksize = (op.strength == 1) ? 3 : 5;
    const cv::Mat src = mat_view_rgba(in);
    cv::Mat dst;
    cv::medianBlur(src, dst, ksize);
    return image_from_mat_rgba(dst, in.source_had_alpha);
}

// Quantification simple par k-means sur un échantillon de pixels (RGB, alpha
// conservé tel quel). Version de base : la segmentation perceptuelle (CIELAB,
// régions connexes) est l'objet de la Phase 4.
Result<Image> apply_quantize(const Image& in, const QuantizeOp& op) {
    if (op.colors < 2 || op.colors > 64) {
        return fail(ErrorCategory::UserInput, "Nombre de couleurs invalide (2 à 64)");
    }
    const auto pixelCount = static_cast<std::size_t>(in.width) * static_cast<std::size_t>(in.height);
    if (pixelCount == 0) {
        return fail(ErrorCategory::Internal, "Image vide");
    }

    // Échantillonnage déterministe (pas plus de 20 000 pixels pour le k-means).
    constexpr std::size_t kMaxSamples = 20'000;
    const std::size_t stride = std::max<std::size_t>(1, pixelCount / kMaxSamples);
    std::vector<cv::Vec3f> samplesVec;
    samplesVec.reserve(pixelCount / stride + 1);
    for (std::size_t i = 0; i < pixelCount; i += stride) {
        const std::uint8_t* px = in.rgba.data() + i * 4;
        samplesVec.emplace_back(px[0], px[1], px[2]);
    }
    const int k = std::min<int>(op.colors, static_cast<int>(samplesVec.size()));
    cv::Mat samples(static_cast<int>(samplesVec.size()), 3, CV_32F,
                    samplesVec.data());

    cv::Mat labels;
    cv::Mat centers;
    cv::TermCriteria criteria(cv::TermCriteria::EPS + cv::TermCriteria::MAX_ITER, 20, 1.0);
    cv::setRNGSeed(12345);  // déterminisme (tests golden)
    cv::kmeans(samples, k, labels, criteria, 3, cv::KMEANS_PP_CENTERS, centers);

    // Affecte chaque pixel au centre le plus proche.
    Image out = in;
    for (std::size_t i = 0; i < pixelCount; ++i) {
        std::uint8_t* px = out.rgba.data() + i * 4;
        int best = 0;
        float bestDist = std::numeric_limits<float>::max();
        for (int c = 0; c < k; ++c) {
            const float dr = centers.at<float>(c, 0) - static_cast<float>(px[0]);
            const float dg = centers.at<float>(c, 1) - static_cast<float>(px[1]);
            const float db = centers.at<float>(c, 2) - static_cast<float>(px[2]);
            const float d = dr * dr + dg * dg + db * db;
            if (d < bestDist) {
                bestDist = d;
                best = c;
            }
        }
        px[0] = static_cast<std::uint8_t>(std::lround(centers.at<float>(best, 0)));
        px[1] = static_cast<std::uint8_t>(std::lround(centers.at<float>(best, 1)));
        px[2] = static_cast<std::uint8_t>(std::lround(centers.at<float>(best, 2)));
    }
    return out;
}

}  // namespace

std::string op_name(const ImageOp& op) {
    return std::visit(
        [](const auto& o) -> std::string {
            using T = std::decay_t<decltype(o)>;
            if constexpr (std::is_same_v<T, CropOp>) return "Recadrage";
            if constexpr (std::is_same_v<T, FlipOp>)
                return o.horizontal ? "Symétrie horizontale" : "Symétrie verticale";
            if constexpr (std::is_same_v<T, Rotate90Op>) return "Rotation";
            if constexpr (std::is_same_v<T, GrayscaleOp>) return "Niveaux de gris";
            if constexpr (std::is_same_v<T, BrightnessContrastOp>) return "Luminosité/contraste";
            if constexpr (std::is_same_v<T, MedianDenoiseOp>) return "Débruitage";
            if constexpr (std::is_same_v<T, QuantizeOp>) return "Quantification";
        },
        op);
}

Result<Image> apply_op(const Image& input, const ImageOp& op) {
    if (input.empty()) {
        return fail(ErrorCategory::Internal, "Aucune image à transformer");
    }
    return std::visit(
        [&](const auto& o) -> Result<Image> {
            using T = std::decay_t<decltype(o)>;
            if constexpr (std::is_same_v<T, CropOp>) return apply_crop(input, o);
            if constexpr (std::is_same_v<T, FlipOp>) return apply_flip(input, o);
            if constexpr (std::is_same_v<T, Rotate90Op>) return apply_rotate90(input, o);
            if constexpr (std::is_same_v<T, GrayscaleOp>) return apply_grayscale(input);
            if constexpr (std::is_same_v<T, BrightnessContrastOp>)
                return apply_brightness_contrast(input, o);
            if constexpr (std::is_same_v<T, MedianDenoiseOp>) return apply_median_denoise(input, o);
            if constexpr (std::is_same_v<T, QuantizeOp>) return apply_quantize(input, o);
        },
        op);
}

Result<Image> apply_pipeline(const Image& original, std::span<const ImageOp> ops) {
    Image current = original;
    for (const ImageOp& op : ops) {
        auto next = apply_op(current, op);
        if (!next) {
            return std::unexpected(next.error());
        }
        current = std::move(*next);
    }
    return current;
}

}  // namespace openstitch::image
