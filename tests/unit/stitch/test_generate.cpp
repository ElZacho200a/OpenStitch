// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "openstitch/stitch_generation/generate.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

namespace {

// Projet avec un carre vectoriel de 10 mm et un objet de broderie de contour.
document::Project make_project(int embroideryCount = 1) {
    document::Project project;

    geometry::Path square;
    square.closed = true;
    const std::int32_t s = 10'000;
    square.nodes = {
        {Vec2um{Micrometers{0}, Micrometers{0}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{s}, Micrometers{0}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{s}, Micrometers{s}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{0}, Micrometers{s}}, geometry::NodeType::Corner, {}, {}},
    };

    document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.paths.push_back(geometry::PathSet{square, {}});
    project.vector_objects.push_back(vec);

    for (int i = 0; i < embroideryCount; ++i) {
        document::EmbroideryObject emb;
        emb.id = project.object_ids.next();
        emb.name = "contour " + std::to_string(i);
        emb.source_vector = vec.id;
        emb.rgb = {static_cast<std::uint8_t>(200 - i * 100), 30, 30};  // couleurs differentes
        project.embroidery_objects.push_back(emb);
    }
    return project;
}

}  // namespace

TEST_CASE("sequence d'un contour : Jump, points, End") {
    const auto seq = generate_sequence(make_project());
    REQUIRE(seq.has_value());
    REQUIRE_FALSE(seq->commands.empty());
    CHECK(seq->commands.front().type == stitch::CommandType::Jump);
    CHECK(seq->commands.back().type == stitch::CommandType::End);

    const auto stats = stitch::compute_stats(*seq);
    CHECK(stats.jumps == 1);
    CHECK(stats.color_changes == 0);
    // Perimetre 40 mm, pas 3 mm -> 4 aretes x 4 pas = 16 segments, 17 points.
    CHECK(stats.stitches == 17);
    // Fil cousu = perimetre exact.
    CHECK(stats.thread_length_um == 40'000.0);
    CHECK(stats.bounds.max == Vec2um{Micrometers{10'000}, Micrometers{10'000}});
}

TEST_CASE("deux objets de couleurs differentes -> un ColorChange") {
    const auto seq = generate_sequence(make_project(2));
    REQUIRE(seq.has_value());
    const auto stats = stitch::compute_stats(*seq);
    CHECK(stats.color_changes == 1);
    CHECK(stats.jumps == 2);
}

TEST_CASE("objet invisible ignore ; projet vide -> erreur utilisateur") {
    auto project = make_project();
    project.embroidery_objects[0].visible = false;
    const auto seq = generate_sequence(project);
    REQUIRE_FALSE(seq.has_value());
    CHECK(seq.error().category == ErrorCategory::UserInput);
}

TEST_CASE("source vectorielle manquante -> erreur interne") {
    auto project = make_project();
    project.vector_objects.clear();
    CHECK_FALSE(generate_sequence(project).has_value());
}

TEST_CASE("la sequence est deterministe") {
    const auto a = generate_sequence(make_project(2));
    const auto b = generate_sequence(make_project(2));
    REQUIRE((a.has_value() && b.has_value()));
    CHECK(a->commands == b->commands);
}
