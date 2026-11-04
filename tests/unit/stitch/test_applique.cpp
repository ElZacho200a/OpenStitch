// SPDX-License-Identifier: Apache-2.0
// Objet appliqué (HP-SPEC-001) : placement -> arrêt -> maintien -> arrêt -> satin de couverture.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "openstitch/stitch_generation/applique.hpp"
#include "openstitch/stitch_generation/generate.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

namespace {

Vec2um um(int x, int y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

geometry::Path square(int x0, int y0, int size) {
    geometry::Path p;
    p.closed = true;
    for (const Vec2um v :
         {um(x0, y0), um(x0 + size, y0), um(x0 + size, y0 + size), um(x0, y0 + size)}) {
        p.nodes.push_back({v, geometry::NodeType::Corner, {}, {}});
    }
    return p;
}

document::Project make_project(const document::AppliqueParams& params, bool withHole = false) {
    document::Project project;
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    geometry::PathSet shape{square(0, 0, 30'000), {}};
    if (withHole) {
        shape.holes.push_back(square(10'000, 10'000, 10'000));
    }
    vec.paths.push_back(shape);
    project.vector_objects.push_back(vec);
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.source_vector = vec.id;
    emb.params = params;
    project.embroidery_objects.push_back(emb);
    return project;
}

std::vector<std::size_t> stop_indices(const stitch::StitchSequence& s) {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < s.commands.size(); ++i) {
        if (s.commands[i].type == stitch::CommandType::Stop) {
            out.push_back(i);
        }
    }
    return out;
}

// Distance au contour du carré 30 mm (axes alignés).
int dist_to_square_edge(Vec2um p) {
    const int dx = std::min(std::abs(p.x.value), std::abs(p.x.value - 30'000));
    const int dy = std::min(std::abs(p.y.value), std::abs(p.y.value - 30'000));
    return std::min(dx, dy);
}

} // namespace

TEST_CASE("applique : deux arrets machine separent placement, maintien et couverture") {
    const auto seq = generate_sequence(make_project(document::AppliqueParams{}));
    REQUIRE(seq.has_value());
    const auto stops = stop_indices(*seq);
    REQUIRE(stops.size() == 2);
    // Placement : points droits SUR le contour (carré de 30 mm), avant le premier arrêt.
    std::size_t placement = 0;
    for (std::size_t i = 0; i < stops[0]; ++i) {
        if (seq->commands[i].type == stitch::CommandType::Stitch) {
            ++placement;
            CHECK(dist_to_square_edge(seq->commands[i].pos) == 0);
        }
    }
    CHECK(placement >= 40); // 120 mm de contour / 2 mm
    // Maintien (zigzag centré) : points à au plus la demi-largeur (0,75 mm) du contour.
    std::size_t tack = 0;
    for (std::size_t i = stops[0]; i < stops[1]; ++i) {
        if (seq->commands[i].type == stitch::CommandType::Stitch) {
            ++tack;
            CHECK(dist_to_square_edge(seq->commands[i].pos) <= 1'500);
        }
    }
    CHECK(tack >= 40);
    // Couverture : après le second arrêt, des points de satin.
    std::size_t cover = 0;
    for (std::size_t i = stops[1]; i < seq->commands.size(); ++i) {
        cover += seq->commands[i].type == stitch::CommandType::Stitch ? 1 : 0;
    }
    CHECK(cover > 100);
    CHECK(seq->commands.back().type == stitch::CommandType::End);
}

TEST_CASE("applique : stops desactives n'emet aucun arret") {
    document::AppliqueParams p;
    p.stops = false;
    const auto seq = generate_sequence(make_project(p));
    REQUIRE(seq.has_value());
    CHECK(stop_indices(*seq).empty());
}

TEST_CASE("applique : le maintien droit et le feston restent proches du contour") {
    for (const auto tack : {document::AppliqueTack::Straight, document::AppliqueTack::Blanket}) {
        document::AppliqueParams p;
        p.tack = tack;
        const auto seq = generate_sequence(make_project(p));
        REQUIRE(seq.has_value());
        CHECK(stop_indices(*seq).size() == 2);
    }
}

TEST_CASE("applique : sortie deterministe et trous respectes") {
    const auto project = make_project(document::AppliqueParams{}, true);
    const auto a = generate_sequence(project);
    const auto b = generate_sequence(project);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(a->commands == b->commands);
    CHECK(stop_indices(*a).size() == 2);
}

TEST_CASE("applique : parametres aberrants ramenes dans leurs plages") {
    document::AppliqueParams p;
    p.placement_length = Micrometers{50};
    p.cover_width = Micrometers{900'000};
    p.cover_density = Micrometers{1};
    p.cut_offset = Micrometers{-900'000};
    const auto c = clamp_applique(p);
    CHECK(c.placement_length == kAppliqueMinPlacement);
    CHECK(c.cover_width == kAppliqueMaxCover);
    CHECK(c.cover_density == kAppliqueMinDensity);
    CHECK(c.cut_offset.value == -kAppliqueMaxCutOffset.value);
    const auto seq = generate_sequence(make_project(p));
    CHECK(seq.has_value());
}

TEST_CASE("applique : contour de decoupe decale et trous decoupes") {
    document::AppliqueParams p;
    std::vector<geometry::PathSet> regions{
        {square(0, 0, 30'000), {square(10'000, 10'000, 10'000)}}};
    const auto plain = applique_cut_paths(regions, p);
    REQUIRE(plain.size() == 2);
    CHECK(plain[0].closed);

    p.cut_offset = Micrometers{1'000};
    const auto grown = applique_cut_paths(regions, p);
    REQUIRE(!grown.empty());
    int minX = 0;
    int maxX = 0;
    for (const auto& n : grown[0].nodes) {
        minX = std::min(minX, n.pos.x.value);
        maxX = std::max(maxX, n.pos.x.value);
    }
    CHECK(minX <= -900);
    CHECK(maxX >= 30'900);
    CHECK(applique_cut_paths(regions, p) == grown); // déterminisme
}
