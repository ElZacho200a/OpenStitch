// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "openstitch/stitch_analysis/color_blocks.hpp"

using namespace openstitch;
using namespace openstitch::stitch_analysis;

namespace {
Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}
} // namespace

TEST_CASE("color_blocks : un seul objet, pas de changement de couleur") {
    document::Project project;
    document::EmbroideryObject obj;
    obj.id = ObjectId{1};
    obj.rgb = {200, 10, 10};
    project.embroidery_objects.push_back(obj);

    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), stitch::CommandType::Jump, ObjectId{1}},
        {um(0, 0), stitch::CommandType::Stitch, ObjectId{1}},
        {um(1'000, 0), stitch::CommandType::Stitch, ObjectId{1}},
        {um(1'000, 0), stitch::CommandType::End, ObjectId{}},
    };

    const auto blocks = color_blocks(project, seq);
    REQUIRE(blocks.size() == 1);
    CHECK(blocks[0].start == 0);
    CHECK(blocks[0].end == 3);
    CHECK(blocks[0].rgb == std::array<std::uint8_t, 3>{200, 10, 10});
    CHECK_FALSE(blocks[0].thread_key.has_value());
}

TEST_CASE("color_blocks : deux objets separes par un changement de couleur") {
    document::Project project;
    document::EmbroideryObject a;
    a.id = ObjectId{1};
    a.rgb = {10, 20, 30};
    document::EmbroideryObject b;
    b.id = ObjectId{2};
    b.rgb = {40, 50, 60};
    project.embroidery_objects = {a, b};

    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), stitch::CommandType::Stitch, ObjectId{1}},
        {um(1'000, 0), stitch::CommandType::Stitch, ObjectId{1}},
        {um(1'000, 0), stitch::CommandType::ColorChange, ObjectId{2}},
        {um(2'000, 0), stitch::CommandType::Stitch, ObjectId{2}},
        {um(2'000, 0), stitch::CommandType::End, ObjectId{}},
    };

    const auto blocks = color_blocks(project, seq);
    REQUIRE(blocks.size() == 2);
    CHECK(blocks[0].rgb == std::array<std::uint8_t, 3>{10, 20, 30});
    CHECK(blocks[0].start == 0);
    CHECK(blocks[0].end == 2);
    CHECK(blocks[1].rgb == std::array<std::uint8_t, 3>{40, 50, 60});
    CHECK(blocks[1].start == 3);
    CHECK(blocks[1].end == 4);
}

TEST_CASE("color_blocks : source inconnue (design importe) -> couleur noire par defaut") {
    document::Project project; // aucun objet de broderie

    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), stitch::CommandType::Stitch, ObjectId{}},
        {um(1'000, 0), stitch::CommandType::Stitch, ObjectId{}},
        {um(1'000, 0), stitch::CommandType::ColorChange, ObjectId{}},
        {um(2'000, 0), stitch::CommandType::Stitch, ObjectId{}},
        {um(2'000, 0), stitch::CommandType::End, ObjectId{}},
    };

    const auto blocks = color_blocks(project, seq);
    REQUIRE(blocks.size() == 2);
    for (const auto& block : blocks) {
        CHECK(block.rgb == std::array<std::uint8_t, 3>{0, 0, 0});
        CHECK_FALSE(block.thread_key.has_value());
    }
}

TEST_CASE("color_blocks : blocs vides omis (deux arrets consecutifs)") {
    document::Project project;
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), stitch::CommandType::Stitch, ObjectId{}},
        {um(1'000, 0), stitch::CommandType::Stitch, ObjectId{}},
        {um(1'000, 0), stitch::CommandType::ColorChange, ObjectId{}},
        {um(1'000, 0), stitch::CommandType::Stop, ObjectId{}}, // pas de point entre les deux arrets
        {um(2'000, 0), stitch::CommandType::Stitch, ObjectId{}},
        {um(2'000, 0), stitch::CommandType::End, ObjectId{}},
    };

    const auto blocks = color_blocks(project, seq);
    REQUIRE(blocks.size() == 2); // le bloc vide entre ColorChange et Stop est omis
    CHECK(blocks[0].start == 0);
    CHECK(blocks[0].end == 2);
    CHECK(blocks[1].start == 4);
    CHECK(blocks[1].end == 5);
}

TEST_CASE("color_blocks : sequence vide") {
    document::Project project;
    const auto blocks = color_blocks(project, stitch::StitchSequence{});
    CHECK(blocks.empty());
}
