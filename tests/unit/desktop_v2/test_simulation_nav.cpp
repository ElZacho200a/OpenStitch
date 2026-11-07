// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "digitize_pipeline.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "simulation_nav.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
namespace fs = std::filesystem;

TEST_CASE("mark navigation moves strictly forward and backward and clamps at the ends",
          "[desktop_v2][simulation]") {
    const std::vector<std::size_t> marks{10, 25, 40};
    CHECK(nextMark(marks, 0, 100) == 10);
    CHECK(nextMark(marks, 10, 100) == 25);  // strictement après
    CHECK(nextMark(marks, 40, 100) == 100); // plus de marque : la fin
    CHECK(previousMark(marks, 100) == 40);
    CHECK(previousMark(marks, 25) == 10); // strictement avant
    CHECK(previousMark(marks, 10) == 0);  // plus de marque : le début
    CHECK(nextMark({}, 5, 50) == 50);
    CHECK(previousMark({}, 5) == 0);
}

TEST_CASE("simulation marks match the effective sequence", "[desktop_v2][simulation]") {
    DigitizeRequest request;
    request.image = fs::path(OPENSTITCH_FIXTURES_DIR) / "tentabrode.png";
    request.target_height_mm = 50.0;
    request.settings.max_colors = 4;
    auto out = run_digitize(request, TaskContext{});
    REQUIRE(out.has_value());
    const auto sequence = stitch_generation::effective_sequence(out->project);
    REQUIRE(sequence.has_value());
    const auto marks = simulationMarks(out->project);
    REQUIRE(marks.has_value());
    CHECK(marks->total == sequence->commands.size());
    std::size_t colourChanges = 0;
    for (const auto& c : sequence->commands) {
        colourChanges += c.type == stitch::CommandType::ColorChange ? 1 : 0;
    }
    CHECK(marks->colours.size() == colourChanges);
    CHECK(marks->objects.size() >= 2);
    for (std::size_t i = 1; i < marks->objects.size(); ++i) {
        CHECK(marks->objects[i - 1] < marks->objects[i]); // croissantes, sans doublon
    }
    for (const auto m : marks->objects) {
        CHECK(m > 0);
        CHECK(m < marks->total);
    }
    // Aller de marque en marque traverse toute la séquence sans boucler.
    std::size_t position = 0;
    std::size_t steps = 0;
    while (position < marks->total && steps < marks->total + 5) {
        const auto next = nextMark(marks->objects, position, marks->total);
        REQUIRE(next > position);
        position = next;
        ++steps;
    }
    CHECK(position == marks->total);
    CHECK(steps == marks->objects.size() + 1);
}
