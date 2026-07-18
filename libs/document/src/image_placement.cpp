// SPDX-License-Identifier: Apache-2.0
#include "openstitch/document/image_placement.hpp"

namespace openstitch::document {

namespace {

Result<void> check_pixels(int width_px, int height_px) {
    if (width_px <= 0 || height_px <= 0) {
        return fail(ErrorCategory::Internal, "Dimensions d'image invalides",
                    "width_px=" + std::to_string(width_px) +
                        " height_px=" + std::to_string(height_px));
    }
    return {};
}

Result<void> check_positive(Millimeters value, const char* name) {
    if (value.value <= 0.0) {
        return fail(ErrorCategory::UserInput,
                    std::string("La dimension doit être strictement positive : ") + name);
    }
    return {};
}

}  // namespace

Result<ImagePlacement> placement_from_width(int width_px, int height_px,
                                            Millimeters target_width) {
    if (auto ok = check_pixels(width_px, height_px); !ok) {
        return std::unexpected(ok.error());
    }
    if (auto ok = check_positive(target_width, "largeur"); !ok) {
        return std::unexpected(ok.error());
    }
    const double ratio = static_cast<double>(height_px) / static_cast<double>(width_px);
    ImagePlacement p;
    p.width = to_micrometers(target_width);
    p.height = to_micrometers(Millimeters{target_width.value * ratio});
    return p;
}

Result<ImagePlacement> placement_from_size(int width_px, int height_px, Millimeters target_width,
                                           Millimeters target_height) {
    if (auto ok = check_pixels(width_px, height_px); !ok) {
        return std::unexpected(ok.error());
    }
    if (auto ok = check_positive(target_width, "largeur"); !ok) {
        return std::unexpected(ok.error());
    }
    if (auto ok = check_positive(target_height, "hauteur"); !ok) {
        return std::unexpected(ok.error());
    }
    ImagePlacement p;
    p.width = to_micrometers(target_width);
    p.height = to_micrometers(target_height);
    return p;
}

Result<ImagePlacement> placement_from_dpi(int width_px, int height_px, double dpi) {
    if (dpi <= 0.0) {
        return fail(ErrorCategory::UserInput, "La résolution (dpi) doit être positive");
    }
    const double mm_per_px = 25.4 / dpi;
    return placement_from_size(width_px, height_px, Millimeters{width_px * mm_per_px},
                               Millimeters{height_px * mm_per_px});
}

Millimeters mm_per_pixel(const ImagePlacement& placement, int width_px) {
    if (width_px <= 0) {
        return Millimeters{0.0};
    }
    return Millimeters{to_millimeters(placement.width).value / static_cast<double>(width_px)};
}

}  // namespace openstitch::document
