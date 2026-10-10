// SPDX-License-Identifier: Apache-2.0
//
// Commandes du lot « moteur de points » : entrée/sortie automatiques par objet
// (HP-ENG-010), réglages de tirage/sous-couche via SetStitchParamsCommand, et satin de
// bordure qui suit son contour (HP-STI-004).
#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "openstitch/commands/project_commands.hpp"
#include "openstitch/commands/undo_stack.hpp"

using namespace openstitch;
using namespace openstitch::commands;

namespace {

geometry::PathNode node(std::int32_t x, std::int32_t y) {
    return geometry::PathNode{Vec2um{Micrometers{x}, Micrometers{y}}, geometry::NodeType::Corner,
                              std::nullopt, std::nullopt};
}

document::Project project_with_tatami() {
    document::Project project;
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    geometry::Path square;
    square.closed = true;
    square.nodes = {node(0, 0), node(10'000, 0), node(10'000, 10'000), node(0, 10'000)};
    vec.paths.push_back(geometry::PathSet{square, {}});
    project.vector_objects.push_back(vec);
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.source_vector = vec.id;
    emb.params = document::TatamiParams{};
    project.embroidery_objects.push_back(emb);
    return project;
}

} // namespace

TEST_CASE("SetEmbroideryJoinModeCommand : apply, undo, redo exacts") {
    auto project = project_with_tatami();
    const ObjectId id = project.embroidery_objects[0].id;
    UndoStack stack;
    stack.execute(std::make_unique<SetEmbroideryJoinModeCommand>(id, document::JoinMode::Auto),
                  project);
    CHECK(project.embroidery_objects[0].join == document::JoinMode::Auto);
    stack.execute(std::make_unique<SetEmbroideryJoinModeCommand>(id, document::JoinMode::Off),
                  project);
    CHECK(project.embroidery_objects[0].join == document::JoinMode::Off);
    stack.undo(project);
    CHECK(project.embroidery_objects[0].join == document::JoinMode::Auto);
    stack.undo(project);
    CHECK(project.embroidery_objects[0].join == document::JoinMode::Inherit);
    stack.redo(project);
    CHECK(project.embroidery_objects[0].join == document::JoinMode::Auto);
}

TEST_CASE("SetEmbroideryJoinModeCommand : objet introuvable = sans effet") {
    auto project = project_with_tatami();
    UndoStack stack;
    stack.execute(
        std::make_unique<SetEmbroideryJoinModeCommand>(ObjectId{999}, document::JoinMode::Auto),
        project);
    CHECK(project.embroidery_objects[0].join == document::JoinMode::Inherit);
    stack.undo(project);
    CHECK(project.embroidery_objects[0].join == document::JoinMode::Inherit);
}

TEST_CASE("SetStitchParamsCommand : tirage et sous-couche auto, undo exact") {
    auto project = project_with_tatami();
    const ObjectId id = project.embroidery_objects[0].id;
    const auto before = project.embroidery_objects[0].params;
    auto changed = std::get<document::TatamiParams>(before);
    changed.pull_compensation = Micrometers{300};
    changed.underlay_mode = document::UnderlayMode::Auto;
    UndoStack stack;
    stack.execute(std::make_unique<SetStitchParamsCommand>(id, changed, "Compensation du tirage"),
                  project);
    CHECK(std::get<document::TatamiParams>(project.embroidery_objects[0].params) == changed);
    stack.undo(project);
    CHECK(project.embroidery_objects[0].params == before);
}

TEST_CASE("satin de bordure : suit son contour quand la forme est deplacee, undo exact") {
    auto project = project_with_tatami();
    const ObjectId vec = project.vector_objects[0].id;
    document::SatinParams satin;
    satin.rail_a.closed = false;
    satin.rail_b.closed = false;
    satin.rail_a.nodes = {node(0, 0), node(5'000, 0)};
    satin.rail_b.nodes = {node(0, 2'000), node(5'000, 2'000)};
    satin.rungs = {{Vec2um{Micrometers{0}, Micrometers{0}},
                    Vec2um{Micrometers{0}, Micrometers{2'000}}, std::nullopt},
                   {Vec2um{Micrometers{5'000}, Micrometers{0}},
                    Vec2um{Micrometers{5'000}, Micrometers{2'000}}, std::nullopt}};
    satin.border = document::BorderSatinSpec{};
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.source_vector = vec;
    emb.params = satin;
    project.embroidery_objects.push_back(emb);
    // Un satin SANS bordure sur le même vecteur ne bouge pas (comportement historique).
    document::SatinParams plain = satin;
    plain.border.reset();
    document::EmbroideryObject other;
    other.id = project.object_ids.next();
    other.source_vector = vec;
    other.params = plain;
    project.embroidery_objects.push_back(other);

    const auto original = project;
    UndoStack stack;
    stack.execute(std::make_unique<TranslateVectorObjectCommand>(
                      vec, Vec2um{Micrometers{1'500}, Micrometers{-700}}),
                  project);
    const auto& moved = std::get<document::SatinParams>(project.embroidery_objects[1].params);
    CHECK(moved.rail_a.nodes[0].pos == Vec2um{Micrometers{1'500}, Micrometers{-700}});
    CHECK(moved.rungs[1].b == Vec2um{Micrometers{6'500}, Micrometers{1'300}});
    CHECK(std::get<document::SatinParams>(project.embroidery_objects[2].params) == plain);
    stack.undo(project);
    CHECK(project.embroidery_objects[1].params == original.embroidery_objects[1].params);
    CHECK(project.vector_objects[0].paths == original.vector_objects[0].paths);
}
