// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

#include "openstitch/commands/transform_ops.hpp"
#include "openstitch/commands/undo_stack.hpp"
#include "openstitch/document/project.hpp"

using namespace openstitch;
using namespace openstitch::commands;

namespace {

constexpr double kPi = std::numbers::pi;

Vec2um pt(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

geometry::PathSet rect(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1) {
    geometry::PathSet set;
    set.outer.closed = true;
    for (const Vec2um p : {pt(x0, y0), pt(x1, y0), pt(x1, y1), pt(x0, y1)}) {
        set.outer.nodes.push_back(geometry::PathNode{p, geometry::NodeType::Corner, {}, {}});
    }
    return set;
}

ObjectId add_shape(document::Project& project, geometry::PathSet shape,
                   const document::StitchParams& params) {
    document::VectorObject object;
    object.id = project.object_ids.next();
    object.paths = {std::move(shape)};
    project.vector_objects.push_back(object);
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.source_vector = object.id;
    emb.params = params;
    project.embroidery_objects.push_back(emb);
    return object.id;
}

double deg(Angle a) {
    return a.radians * 180.0 / kPi;
}

} // namespace

TEST_CASE("transform: rotating a tatami turns its rows with it") {
    document::Project project;
    document::TatamiParams tatami;
    tatami.angle = Angle{30.0 * kPi / 180.0};
    const ObjectId id = add_shape(project, rect(0, 0, 20'000, 10'000), tatami);
    const auto box = objects_bounds(project, {id});
    REQUIRE(box.has_value());
    CHECK(box->center() == pt(10'000, 5'000));

    UndoStack stack;
    stack.execute(
        make_transform_command(project, {id},
                               AffineTransform::rotation(box->center(), Angle{45.0 * kPi / 180.0}),
                               "Pivoter"),
        project);
    const auto& turned = std::get<document::TatamiParams>(project.embroidery_objects[0].params);
    CHECK(deg(turned.angle) == Catch::Approx(75.0).margin(1e-9));

    stack.undo(project);
    const auto& back = std::get<document::TatamiParams>(project.embroidery_objects[0].params);
    CHECK(back.angle.radians == tatami.angle.radians); // instantané : valeur exacte
    CHECK(project.vector_objects[0].paths[0] == rect(0, 0, 20'000, 10'000));
}

TEST_CASE("transform: a full turn and four quarter turns leave the geometry identical") {
    document::Project project;
    const ObjectId id =
        add_shape(project, rect(1'001, -333, 20'007, 10'999), document::TatamiParams{});
    const auto original = project.vector_objects[0].paths;
    const Vec2um pivot = pt(3'333, 7'777);

    UndoStack stack;
    stack.execute(make_transform_command(project, {id},
                                         AffineTransform::rotation(pivot, Angle{2.0 * kPi}), "360"),
                  project);
    CHECK(project.vector_objects[0].paths == original);
    for (int i = 0; i < 4; ++i) {
        stack.execute(make_transform_command(
                          project, {id}, AffineTransform::rotation(pivot, Angle{kPi / 2.0}), "90"),
                      project);
    }
    CHECK(project.vector_objects[0].paths == original);
    // Un quart de tour : (x, y) -> (-y, x) autour du pivot, exactement.
    stack.execute(make_transform_command(
                      project, {id}, AffineTransform::rotation(pt(0, 0), Angle{kPi / 2.0}), "90"),
                  project);
    CHECK(project.vector_objects[0].paths[0].outer.nodes[0].pos == pt(333, 1'001));
}

TEST_CASE("transform: mirror flips shape, row angle, guides and satin rails") {
    document::Project project;
    document::TatamiParams tatami;
    tatami.angle = Angle{30.0 * kPi / 180.0};
    const ObjectId fill = add_shape(project, rect(0, 0, 20'000, 10'000), tatami);

    document::DirectionalFillParams dir;
    geometry::Path guide;
    guide.nodes = {{pt(2'000, 5'000), geometry::NodeType::Smooth, pt(-500, 0), pt(500, 100)},
                   {pt(18'000, 5'000), geometry::NodeType::Corner, {}, {}}};
    dir.guides.push_back(guide);
    dir.density_gradient = document::DensityGradient{pt(0, 5'000), pt(20'000, 5'000),
                                                     Micrometers{400}, Micrometers{2'000}};
    const ObjectId directional = add_shape(project, rect(0, 0, 20'000, 10'000), dir);

    document::SatinParams satin;
    satin.rail_a.nodes = {{pt(0, 0), geometry::NodeType::Corner, {}, {}},
                          {pt(20'000, 0), geometry::NodeType::Corner, {}, {}}};
    satin.rail_b.nodes = {{pt(0, 3'000), geometry::NodeType::Corner, {}, {}},
                          {pt(20'000, 3'000), geometry::NodeType::Corner, {}, {}}};
    satin.rungs.push_back({pt(5'000, 0), pt(5'000, 3'000), {}});
    satin.entry_point = pt(1'000, 1'500);
    const ObjectId column = add_shape(project, rect(0, 0, 20'000, 3'000), satin);

    document::AutoSatinParams autoSatin;
    autoSatin.guides.push_back({pt(4'000, 2'000), Angle{0.3}, false});
    autoSatin.guides.push_back({pt(4'000, 2'000), Angle{30.0 * kPi / 180.0}, true});
    autoSatin.strategy = document::AutoSatinStrategy::ParallelSweep;
    autoSatin.sweep_angle = Angle{30.0 * kPi / 180.0}; // direction fixe du balayage parallèle
    const ObjectId autoColumn = add_shape(project, rect(0, 0, 20'000, 4'000), autoSatin);

    const std::vector<ObjectId> all = all_object_ids(project);
    REQUIRE(all == std::vector<ObjectId>{fill, directional, column, autoColumn});
    const auto before = project;

    UndoStack stack;
    // Retournement gauche/droite autour de x = 10 mm.
    stack.execute(make_transform_command(project, all, AffineTransform::mirror(pt(10'000, 0), true),
                                         "Miroir"),
                  project);

    CHECK(project.vector_objects[0].paths[0].outer.nodes[0].pos == pt(20'000, 0));
    CHECK(deg(std::get<document::TatamiParams>(project.embroidery_objects[0].params).angle) ==
          Catch::Approx(150.0).margin(1e-9));

    const auto& d = std::get<document::DirectionalFillParams>(project.embroidery_objects[1].params);
    CHECK(d.guides[0].nodes[0].pos == pt(18'000, 5'000));
    CHECK(d.guides[0].nodes[0].tan_in == pt(500, 0));
    CHECK(d.guides[0].nodes[0].tan_out == pt(-500, 100));
    CHECK(d.density_gradient->from == pt(20'000, 5'000));
    CHECK(d.density_gradient->to == pt(0, 5'000));

    const auto& s = std::get<document::SatinParams>(project.embroidery_objects[2].params);
    // Les rails sont échangés par le retournement : l'ancien rail B (y = 3 mm) devient A.
    CHECK(s.rail_a.nodes[0].pos == pt(20'000, 3'000));
    CHECK(s.rail_a.nodes[1].pos == pt(0, 3'000));
    CHECK(s.rail_b.nodes[0].pos == pt(20'000, 0));
    CHECK(s.rail_b.nodes[1].pos == pt(0, 0));
    CHECK(s.rungs[0].a == pt(15'000, 3'000));
    CHECK(s.rungs[0].b == pt(15'000, 0));
    CHECK(s.entry_point == pt(19'000, 1'500));

    const auto& a = std::get<document::AutoSatinParams>(project.embroidery_objects[3].params);
    CHECK(a.guides[0].anchor == pt(16'000, 2'000));
    CHECK(a.guides[0].angle.radians == Catch::Approx(kPi - 0.3));       // relatif : écart inversé
    CHECK(deg(a.guides[1].angle) == Catch::Approx(150.0).margin(1e-9)); // absolu : direction
    // L'angle du balayage est une direction du repère : il suit le retournement. La méthode
    // n'est pas touchée.
    REQUIRE(a.sweep_angle.has_value());
    CHECK(deg(*a.sweep_angle) == Catch::Approx(150.0).margin(1e-9));
    CHECK(a.strategy == document::AutoSatinStrategy::ParallelSweep);

    // Deux retournements = identité ; une annulation restaure exactement.
    stack.undo(project);
    for (std::size_t i = 0; i < before.vector_objects.size(); ++i) {
        CHECK(project.vector_objects[i].paths == before.vector_objects[i].paths);
        CHECK(project.embroidery_objects[i].params == before.embroidery_objects[i].params);
    }
}

TEST_CASE("transform: mirroring a satin twice restores it and rotation keeps rails in place") {
    document::Project project;
    document::SatinParams satin;
    satin.rail_a.nodes = {{pt(0, 0), geometry::NodeType::Corner, {}, {}},
                          {pt(20'000, 0), geometry::NodeType::Corner, {}, {}}};
    satin.rail_b.nodes = {{pt(0, 3'000), geometry::NodeType::Corner, {}, {}},
                          {pt(20'000, 3'000), geometry::NodeType::Corner, {}, {}}};
    satin.rungs.push_back({pt(5'000, 0), pt(5'000, 3'000), {}});
    satin.pull_left = Micrometers{100};
    satin.pull_right = Micrometers{300};
    const ObjectId column = add_shape(project, rect(0, 0, 20'000, 3'000), satin);
    const auto before = project.embroidery_objects[0].params;

    UndoStack stack;
    stack.execute(make_transform_command(project, {column},
                                         AffineTransform::mirror(pt(10'000, 1'500), false), "V"),
                  project);
    const auto& once = std::get<document::SatinParams>(project.embroidery_objects[0].params);
    CHECK(once.pull_left.value == 300);
    CHECK(once.pull_right.value == 100);
    // Retournement vertical : l'ancien rail B (y = 3 mm) retombe en y = 0 mais reste le rail A.
    CHECK(once.rail_a.nodes[0].pos == pt(0, 0));
    CHECK(once.rail_b.nodes[0].pos == pt(0, 3'000));
    CHECK(once.rungs[0].a == pt(5'000, 0));

    stack.execute(make_transform_command(project, {column},
                                         AffineTransform::mirror(pt(10'000, 1'500), false), "V"),
                  project);
    CHECK(project.embroidery_objects[0].params == before);

    // Une rotation ne change pas la chiralité : les rails ne sont pas échangés.
    stack.execute(make_transform_command(project, {column},
                                         AffineTransform::rotation(pt(0, 0), Angle{kPi}), "R"),
                  project);
    const auto& turned = std::get<document::SatinParams>(project.embroidery_objects[0].params);
    CHECK(turned.pull_left.value == 100);
    CHECK(turned.rail_a.nodes[0].pos == pt(0, 0));
}

TEST_CASE("transform: scale and translate move everything; unknown ids give no command") {
    document::Project project;
    const ObjectId id =
        add_shape(project, rect(-10'000, -5'000, 10'000, 5'000), document::TatamiParams{});
    UndoStack stack;
    stack.execute(
        make_transform_command(project, {id}, AffineTransform::scale(pt(0, 0), 0.5), "Echelle"),
        project);
    CHECK(project.vector_objects[0].paths[0] == rect(-5'000, -2'500, 5'000, 2'500));
    stack.execute(make_transform_command(project, {id},
                                         AffineTransform::translate(pt(1'000, -2'000)), "Deplacer"),
                  project);
    CHECK(project.vector_objects[0].paths[0] == rect(-4'000, -4'500, 6'000, 500));
    // Facteur invalide : identité.
    CHECK(AffineTransform::scale(pt(0, 0), -3.0).apply(pt(7, 9)) == pt(7, 9));

    CHECK(make_transform_command(project, {ObjectId{424'242}}, AffineTransform{}, "x") == nullptr);
    CHECK(make_transform_command(project, {}, AffineTransform{}, "x") == nullptr);
    CHECK_FALSE(objects_bounds(project, {}).has_value());
    CHECK_FALSE(objects_bounds(project, {ObjectId{424'242}}).has_value());
}
