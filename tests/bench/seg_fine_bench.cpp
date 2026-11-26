// SPDX-License-Identifier: Apache-2.0
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>

#include "openstitch/core/pipeline_profile.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/segmentation/segmentation.hpp"

namespace {
using namespace openstitch;
using Clock = std::chrono::steady_clock;

double milliseconds(Clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}

bool save_mask(const std::filesystem::path& path, const segmentation::FineStructureMap& map) {
    image::Image mask{map.width, map.height, false, {}};
    mask.rgba.reserve(map.mask.size() * 4);
    for (const auto pixel : map.mask) {
        const auto gray = static_cast<std::uint8_t>(pixel != 0 ? 255 : 0);
        mask.rgba.insert(mask.rgba.end(), {gray, gray, gray, 255});
    }
    const auto png = image::encode_png(mask);
    if (!png) {
        return false;
    }
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(png->data()),
              static_cast<std::streamsize>(png->size()));
    return static_cast<bool>(out);
}
} // namespace

int main(int argc, char* argv[]) {
    if (argc != 5 && argc != 6) {
        std::cerr << "usage: openstitch-seg-fine-bench image mask.png repetitions "
                     "high_contrast [low_contrast]\n";
        return 2;
    }
    try {
        const auto source = openstitch::image::load_image(argv[1]);
        const int runs = std::stoi(argv[3]);
        if (!source || runs < 1) {
            std::cerr << "Invalid image or repetitions\n";
            return 2;
        }
        using namespace openstitch;
        constexpr auto morphology = static_cast<std::size_t>(pipeline_profile::Stage::Morphology);
        segmentation::SegmentationOptions options{
            .max_colors = 8, .min_region_px = 16, .smoothing_radius_px = 1};
        segmentation::FineStructureOptions fine;
        fine.high_contrast = std::stod(argv[4]);
        if (argc == 6) {
            fine.low_contrast = std::stod(argv[5]);
        }

        std::optional<segmentation::Segmentation> reference_off;
        std::optional<segmentation::Segmentation> reference_on;
        segmentation::FineStructureMap reference_mask;
        std::cout << "run,mode,segment_ms,detect_ms,morphology_ms,regions,mask_pixels\n";
        for (int run = -1; run < runs; ++run) {
            for (int pass = 0; pass < 2; ++pass) {
                const bool enabled = (run + 1 + pass) % 2 != 0;
                options.fine_structures = enabled ? std::optional{fine} : std::nullopt;
                pipeline_profile::Samples samples;
                segmentation::FineStructureMap map;
                const auto start = Clock::now();
                const auto result = [&] {
                    const pipeline_profile::Recording recording(samples);
                    return segmentation::segment(*source, options, &map);
                }();
                const auto elapsed = milliseconds(Clock::now() - start);
                const auto morphology_ms =
                    static_cast<double>(samples[morphology].elapsed_ns.load()) / 1'000'000.0;
                if (!result) {
                    std::cerr << result.error().message << '\n';
                    return 1;
                }
                auto& reference = enabled ? reference_on : reference_off;
                if (run == -1) {
                    reference = *result;
                    if (enabled) {
                        reference_mask = std::move(map);
                    }
                    continue;
                }
                if (reference->labels != result->labels ||
                    (enabled && reference_mask.mask != map.mask)) {
                    std::cerr << "Non-deterministic segmentation or mask\n";
                    return 1;
                }
                double detect_ms = 0.0;
                if (enabled) {
                    const auto detect_start = Clock::now();
                    const auto detected = segmentation::detect_fine_structures(*source, fine);
                    detect_ms = milliseconds(Clock::now() - detect_start);
                    if (!detected || detected->mask != map.mask) {
                        std::cerr << "Standalone detector and segment disagree\n";
                        return 1;
                    }
                }
                std::cout << run << ',' << (enabled ? "on" : "off") << ',' << elapsed << ','
                          << detect_ms << ',' << morphology_ms << ',' << result->region_count()
                          << ',' << map.pixel_count << '\n';
            }
        }
        return save_mask(argv[2], reference_mask) ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
