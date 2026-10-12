// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "openstitch/document/canvas.hpp"
#include "openstitch/document/hoop_presets.hpp"

using namespace openstitch;
using namespace openstitch::document;

TEST_CASE("canvas: the usable area is the frame minus the margin on every side") {
    Canvas c;
    c.width = Micrometers{130'000};
    c.height = Micrometers{180'000};
    CHECK(c.usable_width().value == 130'000); // marge nulle par défaut : tout le cadre
    CHECK(c.usable_height().value == 180'000);

    c.margin = Micrometers{5'000};
    CHECK(c.usable_width().value == 120'000);
    CHECK(c.usable_height().value == 170'000);

    // Marge démesurée : jamais négatif.
    c.margin = Micrometers{200'000};
    CHECK(c.usable_width().value == 0);
    CHECK(c.usable_height().value == 0);
}

TEST_CASE("canvas: hoop presets are valid frames and build a canvas") {
    for (const auto& preset : kHoopPresets) {
        CHECK(!preset.name.empty());
        CHECK(preset.width_mm >= 10);
        CHECK(preset.height_mm >= 10);
    }
    const Canvas c = canvas_from_preset(kHoopPresets[2], Micrometers{3'000});
    CHECK(c.width.value == kHoopPresets[2].width_mm * 1000);
    CHECK(c.height.value == kHoopPresets[2].height_mm * 1000);
    CHECK(c.margin.value == 3'000);
    CHECK(c == canvas_from_preset(kHoopPresets[2], Micrometers{3'000}));
}
