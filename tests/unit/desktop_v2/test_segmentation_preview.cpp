// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "openstitch/image/image.hpp"
#include "segmentation_preview.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;

namespace {

// Image 60 x 40 : trois bandes verticales rouge / vert / bleu, plus un petit point jaune (4 px)
// dans la bande rouge : un « îlot » que la taille minimale des régions doit absorber.
image::Image bands() {
    image::Image img;
    img.width = 60;
    img.height = 40;
    img.rgba.assign(static_cast<std::size_t>(img.width) * img.height * 4, 255);
    for (int y = 0; y < img.height; ++y) {
        for (int x = 0; x < img.width; ++x) {
            auto* p = &img.rgba[(static_cast<std::size_t>(y) * img.width + x) * 4];
            const int band = x / 20;
            p[0] = band == 0 ? 220 : 20;
            p[1] = band == 1 ? 200 : 20;
            p[2] = band == 2 ? 220 : 20;
            p[3] = 255;
        }
    }
    for (int y = 10; y < 12; ++y) {
        for (int x = 8; x < 10; ++x) {
            auto* p = &img.rgba[(static_cast<std::size_t>(y) * img.width + x) * 4];
            p[0] = 240;
            p[1] = 240;
            p[2] = 10;
        }
    }
    return img;
}

} // namespace

TEST_CASE("the preview finds the colour bands and reports its palette", "[desktop_v2][segment]") {
    TaskContext task;
    segmentation::SegmentationOptions options;
    options.max_colors = 4;
    options.min_region_px = 1;
    const auto preview = previewSegmentation(bands(), options, task);
    REQUIRE(preview.has_value());
    CHECK(preview->width == 60);
    CHECK(preview->height == 40);
    CHECK(preview->rgba.size() == static_cast<std::size_t>(60) * 40 * 4);
    CHECK(preview->regions >= 3);
    CHECK(preview->palette.size() == preview->regions);
    CHECK(preview->seconds >= 0.0);
}

TEST_CASE("a larger minimum region size absorbs the speck", "[desktop_v2][segment]") {
    TaskContext task;
    segmentation::SegmentationOptions keep;
    keep.max_colors = 4;
    keep.min_region_px = 1;
    segmentation::SegmentationOptions clean = keep;
    clean.min_region_px = 30;
    const auto withSpeck = previewSegmentation(bands(), keep, task);
    const auto withoutSpeck = previewSegmentation(bands(), clean, task);
    REQUIRE(withSpeck.has_value());
    REQUIRE(withoutSpeck.has_value());
    CHECK(withoutSpeck->regions < withSpeck->regions);
    CHECK(withoutSpeck->smallestRegionPx >= withSpeck->smallestRegionPx);
}

TEST_CASE("fewer colours never produces more regions than the colour budget allows",
          "[desktop_v2][segment]") {
    TaskContext task;
    segmentation::SegmentationOptions two;
    two.max_colors = 2;
    two.min_region_px = 1;
    const auto preview = previewSegmentation(bands(), two, task);
    REQUIRE(preview.has_value());
    // Deux couleurs maximum : les bandes de même teinte quantifiée restent des régions distinctes
    // (composantes connexes), mais la palette ne dépasse pas ce que le budget autorise.
    CHECK(preview->regions >= 1);
    CHECK(preview->regions <= 4);
}

TEST_CASE("the preview is deterministic and read-only", "[desktop_v2][segment]") {
    TaskContext task;
    segmentation::SegmentationOptions options;
    options.max_colors = 4;
    const auto image = bands();
    const auto a = previewSegmentation(image, options, task);
    const auto b = previewSegmentation(image, options, task);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(a->rgba == b->rgba);
    CHECK(a->palette == b->palette);
}

TEST_CASE("an unreadable file is reported, not silently ignored", "[desktop_v2][segment]") {
    TaskContext task;
    const auto preview = previewSegmentation(std::filesystem::path("n'existe/pas.png"),
                                             segmentation::SegmentationOptions{}, task);
    CHECK_FALSE(preview.has_value());
}
