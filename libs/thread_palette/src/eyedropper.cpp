// SPDX-License-Identifier: Apache-2.0
#include "openstitch/thread_palette/eyedropper.hpp"

#include <algorithm>

namespace openstitch::thread_palette {

namespace {

bool view_is_consistent(const RgbImageView& image) {
    if (image.width <= 0 || image.height <= 0) {
        return false;
    }
    return image.data.size() ==
           static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 3U;
}

Result<std::array<std::uint8_t, 3>> average_rect(const RgbImageView& image, long x0, long y0,
                                                 long x1, long y1) {
    x0 = std::max<long>(x0, 0);
    y0 = std::max<long>(y0, 0);
    x1 = std::min<long>(x1, image.width);
    y1 = std::min<long>(y1, image.height);
    if (x1 <= x0 || y1 <= y0) {
        return fail(ErrorCategory::UserInput, "Zone d'échantillonnage vide ou hors de l'image");
    }
    std::uint64_t sum[3] = {0, 0, 0};
    for (long y = y0; y < y1; ++y) {
        for (long x = x0; x < x1; ++x) {
            const std::size_t i =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
                 static_cast<std::size_t>(x)) *
                3U;
            sum[0] += image.data[i];
            sum[1] += image.data[i + 1];
            sum[2] += image.data[i + 2];
        }
    }
    const std::uint64_t count =
        static_cast<std::uint64_t>(x1 - x0) * static_cast<std::uint64_t>(y1 - y0);
    std::array<std::uint8_t, 3> out{};
    for (int c = 0; c < 3; ++c) {
        out[static_cast<std::size_t>(c)] =
            static_cast<std::uint8_t>((sum[c] * 2 + count) / (count * 2)); // arrondi au plus proche
    }
    return out;
}

} // namespace

Result<std::array<std::uint8_t, 3>> sample_color(const RgbImageView& image, int x, int y,
                                                 int radius) {
    if (!view_is_consistent(image)) {
        return fail(ErrorCategory::UserInput, "Image incohérente avec ses dimensions");
    }
    if (radius < 0 || x < 0 || y < 0 || x >= image.width || y >= image.height) {
        return fail(ErrorCategory::UserInput, "Point d'échantillonnage hors de l'image");
    }
    return average_rect(image, static_cast<long>(x) - radius, static_cast<long>(y) - radius,
                        static_cast<long>(x) + radius + 1, static_cast<long>(y) + radius + 1);
}

Result<std::array<std::uint8_t, 3>> sample_region(const RgbImageView& image, int x, int y, int w,
                                                  int h) {
    if (!view_is_consistent(image)) {
        return fail(ErrorCategory::UserInput, "Image incohérente avec ses dimensions");
    }
    if (w <= 0 || h <= 0) {
        return fail(ErrorCategory::UserInput, "Zone d'échantillonnage vide ou hors de l'image");
    }
    return average_rect(image, x, y, static_cast<long>(x) + w, static_cast<long>(y) + h);
}

Result<EyedropperResult> pick_thread(const RgbImageView& image, int x, int y, int radius,
                                     const ThreadLibrary& library, std::string_view chart_id,
                                     const ThreadStock* stock, std::size_t top_n) {
    auto color = sample_color(image, x, y, radius);
    if (!color) {
        return std::unexpected(color.error());
    }
    EyedropperResult result;
    result.sampled = *color;
    result.matches = stock != nullptr ? stock->nearest(library, *color, top_n)
                                      : library.nearest(*color, chart_id, top_n);
    return result;
}

} // namespace openstitch::thread_palette
