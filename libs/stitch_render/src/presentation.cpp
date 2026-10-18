// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_render/presentation.hpp"

#include <algorithm>
#include <optional>

namespace openstitch::stitch_render {

namespace {

constexpr std::array<std::array<std::uint8_t, 3>, 12> kFallbackPalette{{
    {200, 30, 40},   // rouge
    {30, 90, 190},   // bleu
    {240, 190, 20},  // jaune
    {40, 150, 70},   // vert
    {20, 20, 20},    // noir
    {235, 235, 235}, // blanc
    {230, 110, 30},  // orange
    {130, 50, 150},  // violet
    {120, 75, 40},   // marron
    {60, 180, 200},  // turquoise
    {225, 120, 160}, // rose
    {130, 130, 130}, // gris
}};

} // namespace

std::array<std::uint8_t, 3> block_palette_color(std::size_t block) {
    return kFallbackPalette[block % kFallbackPalette.size()];
}

stitch::StitchSequence assign_color_blocks(const stitch::StitchSequence& sequence) {
    stitch::StitchSequence out = sequence;
    std::uint64_t block = 0;
    for (stitch::StitchCommand& command : out.commands) {
        command.source = ObjectId{block + 1};
        if (command.type == stitch::CommandType::ColorChange) {
            ++block;
        }
    }
    return out;
}

RasterImage render_sequence(const stitch::StitchSequence& sequence, const ThreadColorFn& color_of,
                            const PresentationOptions& options) {
    const std::vector<ThreadSegment> segments =
        build_thread_segments(sequence, sequence.commands.size(), color_of);
    RectMm region = segments_bounds(segments);
    if (region.empty()) {
        return {};
    }
    const double margin = std::max(0.0, options.margin_mm);
    region = {region.x0 - margin, region.y0 - margin, region.x1 + margin, region.y1 + margin};
    const RasterView view = plan_view(region, std::max(0.5, options.px_per_mm), options.max_pixels);
    if (view.width <= 0 || view.height <= 0) {
        return {};
    }
    return render_threads(segments, options.params, view);
}

RasterImage render_machine_sequence(const stitch::StitchSequence& sequence,
                                    const PresentationOptions& options) {
    const stitch::StitchSequence blocks = assign_color_blocks(sequence);
    return render_sequence(
        blocks,
        [](ObjectId id) -> std::optional<std::array<std::uint8_t, 3>> {
            return block_palette_color(id.value == 0 ? 0 : static_cast<std::size_t>(id.value - 1));
        },
        options);
}

std::vector<std::uint8_t> to_straight_rgba(const RasterImage& image) {
    std::vector<std::uint8_t> out = image.rgba;
    for (std::size_t i = 0; i + 3 < out.size(); i += 4) {
        const unsigned a = out[i + 3];
        if (a == 0) {
            out[i] = out[i + 1] = out[i + 2] = 0;
        } else if (a < 255) {
            for (std::size_t c = 0; c < 3; ++c) {
                out[i + c] =
                    static_cast<std::uint8_t>(std::min(255U, (out[i + c] * 255U + a / 2) / a));
            }
        }
    }
    return out;
}

} // namespace openstitch::stitch_render
