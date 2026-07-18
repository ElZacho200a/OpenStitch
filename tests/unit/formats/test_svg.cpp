// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "openstitch/formats/svg.hpp"

using namespace openstitch;
using namespace openstitch::formats;
using stitch::CommandType;

TEST_CASE("svg de diagnostic : structure et determinisme") {
    stitch::StitchSequence seq;
    seq.commands = {
        {Vec2um{Micrometers{0}, Micrometers{0}}, CommandType::Stitch, ObjectId{}},
        {Vec2um{Micrometers{5'000}, Micrometers{0}}, CommandType::Stitch, ObjectId{}},
        {Vec2um{Micrometers{5'000}, Micrometers{0}}, CommandType::ColorChange, ObjectId{}},
        {Vec2um{Micrometers{10'000}, Micrometers{0}}, CommandType::Jump, ObjectId{}},
        {Vec2um{Micrometers{10'000}, Micrometers{5'000}}, CommandType::Stitch, ObjectId{}},
        {Vec2um{Micrometers{10'000}, Micrometers{5'000}}, CommandType::End, ObjectId{}},
    };
    const std::string svg = to_diagnostic_svg(seq);
    CHECK(svg.find("<svg") == 0);
    CHECK(svg.find("</svg>") != std::string::npos);
    CHECK(svg.find("stroke=\"black\"") != std::string::npos);   // couture
    CHECK(svg.find("stroke=\"orange\"") != std::string::npos);  // sauts
    CHECK(svg.find("stroke=\"red\"") != std::string::npos);     // changement de couleur
    CHECK(svg.find("points: 3 sauts: 1") != std::string::npos);
    CHECK(svg == to_diagnostic_svg(seq));  // deterministe
}
