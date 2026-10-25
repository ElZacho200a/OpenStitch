// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "openstitch/stitch_render/simulation.hpp"

using namespace openstitch;
using namespace openstitch::stitch_render;

namespace {

stitch::StitchCommand cmd(stitch::CommandType type, std::uint64_t src) {
    stitch::StitchCommand c;
    c.type = type;
    c.source = ObjectId{src};
    return c;
}

// 0..2 objet 1, 3 coupe, 4..5 objet 2, 6 changement de couleur, 7..8 objet 3, 9 fin.
stitch::StitchSequence sample() {
    using T = stitch::CommandType;
    stitch::StitchSequence s;
    s.commands = {cmd(T::Stitch, 1), cmd(T::Stitch, 1), cmd(T::Stitch, 1),      cmd(T::Trim, 1),
                  cmd(T::Stitch, 2), cmd(T::Stitch, 2), cmd(T::ColorChange, 2), cmd(T::Stitch, 3),
                  cmd(T::Stitch, 3), cmd(T::End, 3)};
    return s;
}

} // namespace

TEST_CASE("simulation index lists object and color starts", "[simulation]") {
    const auto idx = build_simulation_index(sample());
    CHECK(idx.color_starts == std::vector<int>{0, 6});
    CHECK(idx.object_starts == std::vector<int>{0, 4, 7});
}

TEST_CASE("simulation step target moves by stitch object and color", "[simulation]") {
    const auto seq = sample();
    const auto idx = build_simulation_index(seq);
    const auto n = seq.commands.size();
    CHECK(simulation_step_target(idx, n, 0, SimulationUnit::Stitch, -1) == 0);
    CHECK(simulation_step_target(idx, n, 9, SimulationUnit::Stitch, +1) == 9);
    CHECK(simulation_step_target(idx, n, 3, SimulationUnit::Stitch, +1) == 4);
    CHECK(simulation_step_target(idx, n, 1, SimulationUnit::Object, +1) == 4);
    CHECK(simulation_step_target(idx, n, 4, SimulationUnit::Object, +1) == 7);
    CHECK(simulation_step_target(idx, n, 8, SimulationUnit::Object, +1) == 9);
    CHECK(simulation_step_target(idx, n, 5, SimulationUnit::Object, -1) == 4);
    CHECK(simulation_step_target(idx, n, 4, SimulationUnit::Object, -1) == 0);
    CHECK(simulation_step_target(idx, n, 2, SimulationUnit::Color, +1) == 6);
    CHECK(simulation_step_target(idx, n, 8, SimulationUnit::Color, -1) == 6);
    CHECK(simulation_step_target(idx, n, 6, SimulationUnit::Color, -1) == 0);
}

TEST_CASE("simulation elapsed minutes follow the production hypotheses", "[simulation]") {
    const auto seq = sample();
    SimulationTiming t;
    t.stitches_per_minute = 600.0;
    t.color_change_seconds = 30.0;
    t.trim_seconds = 6.0;
    CHECK(simulation_elapsed_minutes(seq, -1, t) == Catch::Approx(0.0));
    CHECK(simulation_elapsed_minutes(seq, 2, t) == Catch::Approx(3.0 / 600.0));
    CHECK(simulation_elapsed_minutes(seq, 3, t) == Catch::Approx(3.0 / 600.0 + 0.1));
    CHECK(simulation_elapsed_minutes(seq, 99, t) == Catch::Approx(7.0 / 600.0 + 0.1 + 0.5));
    CHECK(simulation_machine_steps_per_second(t) == Catch::Approx(10.0));
    CHECK(simulation_color_rank(build_simulation_index(seq), 8) == 2);
}
