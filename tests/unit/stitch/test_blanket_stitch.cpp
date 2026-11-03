// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#include "openstitch/stitch_generation/blanket_stitch.hpp"
#include "openstitch/stitch_generation/generate.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

namespace {

Vec2um um(int x, int y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

// Ligne horizontale de 10 mm, de (0,0) a (10 mm, 0), sens +X : la gauche est +Y.
geometry::Path horizontal_line() {
    geometry::Path p;
    p.closed = false;
    p.nodes.push_back({um(0, 0), geometry::NodeType::Corner, {}, {}});
    p.nodes.push_back({um(10'000, 0), geometry::NodeType::Corner, {}, {}});
    return p;
}

document::BlanketStitchParams params(document::BlanketStyle style, document::BlanketSide side) {
    document::BlanketStitchParams p;
    p.style = style;
    p.side = side;
    p.width = Micrometers{3'000};
    p.spacing = Micrometers{2'000};
    return p;
}

} // namespace

TEST_CASE("blanket : dents perpendiculaires du cote gauche, base sur le chemin") {
    const auto pts = blanket_stitch_points(
        horizontal_line(), params(document::BlanketStyle::Blanket, document::BlanketSide::Left));
    // 10 mm / 2 mm = 5 pas, 6 stations ; base, dent, base pour chacune.
    REQUIRE(pts.size() == 6 * 3);
    CHECK(pts.front() == um(0, 0));
    CHECK(pts.back() == um(10'000, 0));
    int tips = 0;
    for (const Vec2um& p : pts) {
        CHECK(p.y.value >= 0);
        if (p.y.value == 3'000) {
            ++tips;
        }
    }
    CHECK(tips == 6);
    CHECK(pts[1] == um(0, 3'000)); // dent perpendiculaire a la base
}

TEST_CASE("blanket : cote droit inverse la dent, centre l'alterne") {
    const auto right = blanket_stitch_points(
        horizontal_line(), params(document::BlanketStyle::Blanket, document::BlanketSide::Right));
    CHECK(right[1] == um(0, -3'000));
    const auto centre = blanket_stitch_points(
        horizontal_line(), params(document::BlanketStyle::Blanket, document::BlanketSide::Center));
    CHECK(centre[1] == um(0, 1'500));
    CHECK(std::any_of(centre.begin(), centre.end(),
                      [](const Vec2um& p) { return p.y.value == -1'500; }));
}

TEST_CASE("zigzag : un point par station alterne entre les deux bords") {
    const auto pts = blanket_stitch_points(
        horizontal_line(), params(document::BlanketStyle::Zigzag, document::BlanketSide::Center));
    REQUIRE(pts.size() == 6);
    for (std::size_t i = 0; i < pts.size(); ++i) {
        CHECK(pts[i].y.value == (i % 2 == 0 ? 1'500 : -1'500));
        CHECK(pts[i].x.value == static_cast<int>(i) * 2'000);
    }
    const auto left = blanket_stitch_points(
        horizontal_line(), params(document::BlanketStyle::Zigzag, document::BlanketSide::Left));
    CHECK(left[0].y.value == 0);
    CHECK(left[1].y.value == 3'000);
}

TEST_CASE("blanket : valeurs aberrantes bornees, chemin degenere vide") {
    auto p = params(document::BlanketStyle::Zigzag, document::BlanketSide::Left);
    p.spacing = Micrometers{0}; // borne a 0,3 mm : pas de boucle infinie ni de division par zero
    p.width = Micrometers{-5};  // borne a 0,2 mm
    const auto pts = blanket_stitch_points(horizontal_line(), p);
    CHECK(pts.size() >= 30);
    CHECK(pts[1].y.value == 200);

    geometry::Path single;
    single.closed = false;
    single.nodes.push_back({um(5, 5), geometry::NodeType::Corner, {}, {}});
    CHECK(blanket_stitch_points(single, p).empty());
}

TEST_CASE("blanket : contour ferme boucle sans dupliquer la station de depart") {
    geometry::Path square;
    square.closed = true;
    for (const Vec2um v : {um(0, 0), um(10'000, 0), um(10'000, 10'000), um(0, 10'000)}) {
        square.nodes.push_back({v, geometry::NodeType::Corner, {}, {}});
    }
    const auto pts = blanket_stitch_points(
        square, params(document::BlanketStyle::Blanket, document::BlanketSide::Left));
    REQUIRE(pts.size() > 10);
    CHECK(pts.front() == pts.back());
    // Sens antihoraire : la gauche est l'interieur, toutes les dents restent dans le carre.
    for (const Vec2um& p : pts) {
        CHECK(p.x.value >= 0);
        CHECK(p.x.value <= 10'000);
        CHECK(p.y.value >= 0);
        CHECK(p.y.value <= 10'000);
    }
}

TEST_CASE("blanket : generation deterministe via un objet de broderie") {
    document::Project project;
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.paths.push_back(geometry::PathSet{horizontal_line(), {}});
    project.vector_objects.push_back(vec);
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.source_vector = vec.id;
    emb.params = document::BlanketStitchParams{};
    project.embroidery_objects.push_back(emb);
    CHECK(project.embroidery_objects[0].is_blanket());

    const auto a = generate_sequence(project);
    const auto b = generate_sequence(project);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(a->commands.size() == b->commands.size());
    CHECK(a->commands.size() > 10);
    for (std::size_t i = 0; i < a->commands.size(); ++i) {
        CHECK(a->commands[i].pos == b->commands[i].pos);
        CHECK(a->commands[i].type == b->commands[i].type);
    }
}
