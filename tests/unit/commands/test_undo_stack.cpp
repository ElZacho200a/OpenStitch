// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <variant>

#include "openstitch/commands/project_commands.hpp"
#include "openstitch/commands/undo_stack.hpp"

using namespace openstitch;
using namespace openstitch::commands;

TEST_CASE("execute / undo / redo sur la pile d'operations") {
    document::Project project;
    UndoStack stack;

    stack.execute(std::make_unique<AppendImageOpCommand>(image::GrayscaleOp{}), project);
    stack.execute(std::make_unique<AppendImageOpCommand>(image::FlipOp{true}), project);
    CHECK(project.ops.size() == 2);
    CHECK(stack.canUndo());
    CHECK_FALSE(stack.canRedo());
    CHECK(stack.undoName() == "Symétrie horizontale");

    CHECK(stack.undo(project));
    CHECK(project.ops.size() == 1);
    CHECK(stack.canRedo());
    CHECK(stack.redoName() == "Symétrie horizontale");

    CHECK(stack.redo(project));
    CHECK(project.ops.size() == 2);

    // undo total = etat initial
    CHECK(stack.undo(project));
    CHECK(stack.undo(project));
    CHECK(project.ops.empty());
    CHECK_FALSE(stack.undo(project));
}

namespace {

// Petite segmentation synthetique pour tester les commandes de regions.
document::Project project_with_segmentation() {
    document::Project project;
    project.original.width = 4;
    project.original.height = 1;
    project.original.rgba.assign(4 * 4, 255);

    segmentation::Segmentation seg;
    seg.width = 4;
    seg.height = 1;
    seg.labels = {1, 1, 2, 2};
    seg.region_slots.push_back(segmentation::Region{RegionId{1}, {255, 0, 0}, 2});
    seg.region_slots.push_back(segmentation::Region{RegionId{2}, {0, 0, 255}, 2});
    project.segmentation = std::move(seg);
    return project;
}

}  // namespace

TEST_CASE("AppendImageOpCommand invalide la segmentation et la restaure a l'annulation") {
    auto project = project_with_segmentation();
    UndoStack stack;

    stack.execute(std::make_unique<AppendImageOpCommand>(image::GrayscaleOp{}), project);
    CHECK_FALSE(project.segmentation.has_value());

    CHECK(stack.undo(project));
    REQUIRE(project.segmentation.has_value());
    CHECK(project.segmentation->region_count() == 2);
}

TEST_CASE("MergeRegionsCommand : undo restaure labels et comptes exactement") {
    auto project = project_with_segmentation();
    UndoStack stack;

    stack.execute(std::make_unique<MergeRegionsCommand>(RegionId{1}, RegionId{2}), project);
    CHECK(project.segmentation->region_count() == 1);
    CHECK(project.segmentation->find(RegionId{1})->pixel_count == 4);
    CHECK(project.segmentation->labels == std::vector<std::uint32_t>{1, 1, 1, 1});

    CHECK(stack.undo(project));
    CHECK(project.segmentation->region_count() == 2);
    CHECK(project.segmentation->find(RegionId{1})->pixel_count == 2);
    CHECK(project.segmentation->find(RegionId{2})->pixel_count == 2);
    CHECK(project.segmentation->labels == std::vector<std::uint32_t>{1, 1, 2, 2});

    CHECK(stack.redo(project));
    CHECK(project.segmentation->region_count() == 1);
}

TEST_CASE("RemoveRegionCommand : la region disparait, undo la restaure") {
    auto project = project_with_segmentation();  // labels {1,1,2,2}, deux regions
    UndoStack stack;

    stack.execute(std::make_unique<RemoveRegionCommand>(RegionId{1}), project);
    // Region 1 supprimee : ses pixels reviennent au fond (0), sans fusion.
    CHECK(project.segmentation->region_count() == 1);
    CHECK(project.segmentation->find(RegionId{1}) == nullptr);
    CHECK(project.segmentation->labels == std::vector<std::uint32_t>{0, 0, 2, 2});
    // La region 2 n'a PAS recupere les pixels supprimes.
    CHECK(project.segmentation->find(RegionId{2})->pixel_count == 2);

    CHECK(stack.undo(project));
    CHECK(project.segmentation->region_count() == 2);
    CHECK(project.segmentation->find(RegionId{1})->pixel_count == 2);
    CHECK(project.segmentation->labels == std::vector<std::uint32_t>{1, 1, 2, 2});

    CHECK(stack.redo(project));
    CHECK(project.segmentation->labels == std::vector<std::uint32_t>{0, 0, 2, 2});
}

TEST_CASE("RecolorRegionCommand : aller-retour exact") {
    auto project = project_with_segmentation();
    UndoStack stack;

    stack.execute(std::make_unique<RecolorRegionCommand>(RegionId{1},
                                                         std::array<std::uint8_t, 3>{9, 9, 9}),
                  project);
    CHECK(project.segmentation->find(RegionId{1})->rgb == std::array<std::uint8_t, 3>{9, 9, 9});
    CHECK(stack.undo(project));
    CHECK(project.segmentation->find(RegionId{1})->rgb == std::array<std::uint8_t, 3>{255, 0, 0});
}

TEST_CASE("SetSegmentationCommand : apply/revert echangent les etats") {
    document::Project project;
    UndoStack stack;
    CHECK_FALSE(project.segmentation.has_value());

    auto seg = project_with_segmentation().segmentation;
    stack.execute(std::make_unique<SetSegmentationCommand>(std::move(seg)), project);
    CHECK(project.segmentation.has_value());

    CHECK(stack.undo(project));
    CHECK_FALSE(project.segmentation.has_value());
    CHECK(stack.redo(project));
    CHECK(project.segmentation.has_value());
}

TEST_CASE("TranslateVectorObjectCommand : deplace tous les noeuds (tous morceaux, tous trous), undo exact") {
    document::Project project;
    UndoStack stack;

    document::VectorObject object;
    object.id = project.object_ids.next();
    object.name = "carre avec trou";
    geometry::Path outer;
    outer.closed = true;
    outer.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{0}, Micrometers{0}},
                                             geometry::NodeType::Corner, std::nullopt, std::nullopt});
    outer.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{10000}, Micrometers{0}},
                                             geometry::NodeType::Corner, std::nullopt, std::nullopt});
    outer.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{10000}, Micrometers{10000}},
                                             geometry::NodeType::Corner, std::nullopt, std::nullopt});
    geometry::Path hole;
    hole.closed = true;
    hole.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{2000}, Micrometers{2000}},
                                            geometry::NodeType::Corner, std::nullopt, std::nullopt});
    hole.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{3000}, Micrometers{2000}},
                                            geometry::NodeType::Corner, std::nullopt, std::nullopt});
    hole.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{3000}, Micrometers{3000}},
                                            geometry::NodeType::Corner, std::nullopt, std::nullopt});
    object.paths.push_back(geometry::PathSet{outer, {hole}});
    stack.execute(std::make_unique<AddVectorObjectCommand>(object), project);
    const ObjectId id = project.vector_objects[0].id;

    const Vec2um delta{Micrometers{500}, Micrometers{-1200}};
    stack.execute(std::make_unique<TranslateVectorObjectCommand>(id, delta), project);

    const auto* moved = project.findObject(id);
    REQUIRE(moved != nullptr);
    CHECK(moved->paths[0].outer.nodes[0].pos == Vec2um{Micrometers{500}, Micrometers{-1200}});
    CHECK(moved->paths[0].outer.nodes[1].pos == Vec2um{Micrometers{10500}, Micrometers{-1200}});
    CHECK(moved->paths[0].holes[0].nodes[0].pos == Vec2um{Micrometers{2500}, Micrometers{800}});

    CHECK(stack.undo(project));
    const auto* reverted = project.findObject(id);
    REQUIRE(reverted != nullptr);
    CHECK(reverted->paths[0].outer.nodes[0].pos == Vec2um{Micrometers{0}, Micrometers{0}});
    CHECK(reverted->paths[0].holes[0].nodes[0].pos == Vec2um{Micrometers{2000}, Micrometers{2000}});

    CHECK(stack.redo(project));
    const auto* redone = project.findObject(id);
    REQUIRE(redone != nullptr);
    CHECK(redone->paths[0].outer.nodes[0].pos == Vec2um{Micrometers{500}, Micrometers{-1200}});
}

TEST_CASE("AddVectorObject et MoveNode : undo/redo coherents") {
    document::Project project;
    UndoStack stack;

    document::VectorObject object;
    object.id = project.object_ids.next();
    object.name = "test";
    geometry::Path square;
    square.closed = true;
    square.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{0}, Micrometers{0}},
                                              geometry::NodeType::Corner, std::nullopt,
                                              std::nullopt});
    square.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{1000}, Micrometers{0}},
                                              geometry::NodeType::Corner, std::nullopt,
                                              std::nullopt});
    square.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{0}, Micrometers{1000}},
                                              geometry::NodeType::Corner, std::nullopt,
                                              std::nullopt});
    object.paths.push_back(geometry::PathSet{square, {}});

    stack.execute(std::make_unique<AddVectorObjectCommand>(object), project);
    REQUIRE(project.vector_objects.size() == 1);
    const ObjectId id = project.vector_objects[0].id;

    const Vec2um oldPos{Micrometers{0}, Micrometers{0}};
    const Vec2um newPos{Micrometers{500}, Micrometers{500}};
    stack.execute(std::make_unique<MoveNodeCommand>(id, document::NodeRef{0, 0, 0}, oldPos, newPos),
                  project);
    CHECK(project.findObject(id)->paths[0].outer.nodes[0].pos == newPos);

    CHECK(stack.undo(project));
    CHECK(project.findObject(id)->paths[0].outer.nodes[0].pos == oldPos);
    CHECK(stack.undo(project));
    CHECK(project.vector_objects.empty());
    CHECK(stack.redo(project));
    CHECK(stack.redo(project));
    CHECK(project.findObject(id)->paths[0].outer.nodes[0].pos == newPos);
}

TEST_CASE("RemoveEmbroideryObjectCommand : supprime seul, undo restaure a l'index d'origine") {
    document::Project project;
    UndoStack stack;

    document::EmbroideryObject first;
    first.id = project.object_ids.next();
    first.name = "premier";
    first.params = document::RunningStitchParams{};
    document::EmbroideryObject second;
    second.id = project.object_ids.next();
    second.name = "second";
    second.params = document::TatamiParams{};
    project.embroidery_objects = {first, second};

    stack.execute(std::make_unique<RemoveEmbroideryObjectCommand>(first.id), project);
    REQUIRE(project.embroidery_objects.size() == 1);
    CHECK(project.embroidery_objects[0].id == second.id);
    CHECK(stack.undoName() == "Supprimer l'objet de broderie");

    CHECK(stack.undo(project));
    REQUIRE(project.embroidery_objects.size() == 2);
    CHECK(project.embroidery_objects[0].id == first.id);  // reinsere au bon index
    CHECK(project.embroidery_objects[1].id == second.id);

    CHECK(stack.redo(project));
    REQUIRE(project.embroidery_objects.size() == 1);
    CHECK(project.embroidery_objects[0].id == second.id);
}

TEST_CASE("RemoveEmbroideryObjectCommand : id introuvable -- aucune mutation") {
    document::Project project;
    UndoStack stack;
    stack.execute(std::make_unique<RemoveEmbroideryObjectCommand>(ObjectId{999}), project);
    CHECK(project.embroidery_objects.empty());
    CHECK(stack.undo(project));  // no-op, jamais applique -> revert() sans effet
}

TEST_CASE("RemoveVectorObjectCommand : supprime l'objet ET les broderies qui en dependent") {
    document::Project project;
    UndoStack stack;

    document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.name = "forme";
    document::VectorObject otherVec;
    otherVec.id = project.object_ids.next();
    otherVec.name = "autre forme";
    project.vector_objects = {vec, otherVec};

    document::EmbroideryObject dependent;
    dependent.id = project.object_ids.next();
    dependent.source_vector = vec.id;
    dependent.params = document::RunningStitchParams{};
    document::EmbroideryObject unrelated;
    unrelated.id = project.object_ids.next();
    unrelated.source_vector = otherVec.id;
    unrelated.params = document::TatamiParams{};
    project.embroidery_objects = {dependent, unrelated};

    stack.execute(std::make_unique<RemoveVectorObjectCommand>(vec.id), project);
    REQUIRE(project.vector_objects.size() == 1);
    CHECK(project.vector_objects[0].id == otherVec.id);
    REQUIRE(project.embroidery_objects.size() == 1);
    CHECK(project.embroidery_objects[0].id == unrelated.id);  // seule la broderie liee a `vec` part
    CHECK(stack.undoName() == "Supprimer l'objet vectoriel");

    CHECK(stack.undo(project));
    REQUIRE(project.vector_objects.size() == 2);
    CHECK(project.vector_objects[0].id == vec.id);  // reinsere a son index d'origine
    REQUIRE(project.embroidery_objects.size() == 2);
    CHECK(project.embroidery_objects[0].id == dependent.id);
    CHECK(project.embroidery_objects[1].id == unrelated.id);

    CHECK(stack.redo(project));
    REQUIRE(project.vector_objects.size() == 1);
    REQUIRE(project.embroidery_objects.size() == 1);
    CHECK(project.embroidery_objects[0].id == unrelated.id);
}

TEST_CASE("RemoveVectorObjectCommand : id introuvable -- aucune mutation") {
    document::Project project;
    UndoStack stack;
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    project.vector_objects.push_back(vec);

    stack.execute(std::make_unique<RemoveVectorObjectCommand>(ObjectId{999}), project);
    CHECK(project.vector_objects.size() == 1);
    CHECK(stack.undo(project));
    CHECK(project.vector_objects.size() == 1);
}

namespace {

// Construction explicite (pas de helper `um` court ici : celui du bas de
// fichier, § retouches manuelles, n'est pas visible avant sa propre
// définition -- même convention que le test AddVectorObject/MoveNode ci-dessus).
Vec2um bezierVec(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

document::Project project_with_bezier_object() {
    document::Project project;
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    geometry::Path curve;
    curve.closed = true;
    curve.nodes = {
        geometry::PathNode{bezierVec(0, 0), geometry::NodeType::Smooth, bezierVec(-1'000, 0),
                          bezierVec(1'000, 0)},
        geometry::PathNode{bezierVec(10'000, 0), geometry::NodeType::Corner, std::nullopt,
                          std::nullopt},
    };
    vec.paths.push_back(geometry::PathSet{curve, {}});
    project.vector_objects.push_back(vec);
    return project;
}

}  // namespace

TEST_CASE("SetNodeHandleCommand : deplace une poignee Bezier, undo restaure exact") {
    auto project = project_with_bezier_object();
    UndoStack stack;
    const ObjectId id = project.vector_objects[0].id;
    const document::NodeRef ref{0, 0, 0};

    const Vec2um oldTanOut = bezierVec(1'000, 0);
    const Vec2um newTanOut = bezierVec(1'500, 500);
    stack.execute(std::make_unique<SetNodeHandleCommand>(id, ref, /*isOut=*/true, oldTanOut,
                                                          newTanOut),
                  project);
    REQUIRE(project.findObject(id)->paths[0].outer.nodes[0].tan_out.has_value());
    CHECK(*project.findObject(id)->paths[0].outer.nodes[0].tan_out == newTanOut);
    // La poignée entrante n'est pas affectée par une commande sur la sortante.
    CHECK(*project.findObject(id)->paths[0].outer.nodes[0].tan_in == bezierVec(-1'000, 0));

    CHECK(stack.undo(project));
    CHECK(*project.findObject(id)->paths[0].outer.nodes[0].tan_out == oldTanOut);
    CHECK(stack.redo(project));
    CHECK(*project.findObject(id)->paths[0].outer.nodes[0].tan_out == newTanOut);
}

TEST_CASE("SetNodeHandleCommand : nullopt efface la poignee (segment redevient droit)") {
    auto project = project_with_bezier_object();
    UndoStack stack;
    const ObjectId id = project.vector_objects[0].id;
    const document::NodeRef ref{0, 0, 0};

    stack.execute(std::make_unique<SetNodeHandleCommand>(id, ref, /*isOut=*/true,
                                                          bezierVec(1'000, 0), std::nullopt),
                  project);
    CHECK_FALSE(project.findObject(id)->paths[0].outer.nodes[0].tan_out.has_value());
    CHECK(stack.undo(project));
    REQUIRE(project.findObject(id)->paths[0].outer.nodes[0].tan_out.has_value());
    CHECK(*project.findObject(id)->paths[0].outer.nodes[0].tan_out == bezierVec(1'000, 0));
}

TEST_CASE("SetNodeTypeCommand : bascule Coin/Lisse sur un objet vectoriel, undo exact") {
    auto project = project_with_bezier_object();
    UndoStack stack;
    const ObjectId id = project.vector_objects[0].id;
    const document::NodeRef ref{0, 0, 1};  // le 2e nœud, Coin au depart

    REQUIRE(project.findObject(id)->paths[0].outer.nodes[1].type == geometry::NodeType::Corner);
    stack.execute(
        std::make_unique<SetNodeTypeCommand>(id, ref, geometry::NodeType::Smooth), project);
    CHECK(project.findObject(id)->paths[0].outer.nodes[1].type == geometry::NodeType::Smooth);

    CHECK(stack.undo(project));
    CHECK(project.findObject(id)->paths[0].outer.nodes[1].type == geometry::NodeType::Corner);
    CHECK(stack.redo(project));
    CHECK(project.findObject(id)->paths[0].outer.nodes[1].type == geometry::NodeType::Smooth);
}

TEST_CASE("RemoveNodeCommand : supprime un noeud, undo restaure a l'index d'origine") {
    document::Project project;
    UndoStack stack;

    document::VectorObject object;
    object.id = project.object_ids.next();
    object.name = "carre";
    geometry::Path square;
    square.closed = true;
    square.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{0}, Micrometers{0}},
                                              geometry::NodeType::Corner, std::nullopt, std::nullopt});
    square.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{1000}, Micrometers{0}},
                                              geometry::NodeType::Corner, std::nullopt, std::nullopt});
    square.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{1000}, Micrometers{1000}},
                                              geometry::NodeType::Corner, std::nullopt, std::nullopt});
    square.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{0}, Micrometers{1000}},
                                              geometry::NodeType::Corner, std::nullopt, std::nullopt});
    object.paths.push_back(geometry::PathSet{square, {}});
    stack.execute(std::make_unique<AddVectorObjectCommand>(object), project);
    const ObjectId id = project.vector_objects[0].id;

    const document::NodeRef ref{0, 0, 1};  // 2e noeud, (1000, 0)
    stack.execute(std::make_unique<RemoveNodeCommand>(id, ref), project);
    REQUIRE(project.findObject(id)->paths[0].outer.nodes.size() == 3);
    CHECK(project.findObject(id)->paths[0].outer.nodes[1].pos ==
          Vec2um{Micrometers{1000}, Micrometers{1000}});

    CHECK(stack.undo(project));
    REQUIRE(project.findObject(id)->paths[0].outer.nodes.size() == 4);
    CHECK(project.findObject(id)->paths[0].outer.nodes[1].pos == Vec2um{Micrometers{1000}, Micrometers{0}});

    CHECK(stack.redo(project));
    REQUIRE(project.findObject(id)->paths[0].outer.nodes.size() == 3);
}

TEST_CASE("RemoveNodeCommand : refuse de descendre sous 3 noeuds") {
    document::Project project;
    UndoStack stack;

    document::VectorObject object;
    object.id = project.object_ids.next();
    object.name = "triangle";
    geometry::Path triangle;
    triangle.closed = true;
    triangle.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{0}, Micrometers{0}},
                                                geometry::NodeType::Corner, std::nullopt, std::nullopt});
    triangle.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{1000}, Micrometers{0}},
                                                geometry::NodeType::Corner, std::nullopt, std::nullopt});
    triangle.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{0}, Micrometers{1000}},
                                                geometry::NodeType::Corner, std::nullopt, std::nullopt});
    object.paths.push_back(geometry::PathSet{triangle, {}});
    stack.execute(std::make_unique<AddVectorObjectCommand>(object), project);
    const ObjectId id = project.vector_objects[0].id;

    stack.execute(std::make_unique<RemoveNodeCommand>(id, document::NodeRef{0, 0, 0}), project);
    CHECK(project.findObject(id)->paths[0].outer.nodes.size() == 3);  // inchange
}

TEST_CASE("SetFillAngleCommand : reoriente le tatami, undo/redo exacts") {
    document::Project project;
    UndoStack stack;

    document::EmbroideryObject fill;
    fill.id = project.object_ids.next();
    fill.name = "remplissage";
    document::TatamiParams tatami;
    tatami.angle = Angle{0.0};
    fill.params = tatami;
    stack.execute(std::make_unique<AddEmbroideryObjectCommand>(fill), project);
    const ObjectId id = project.embroidery_objects[0].id;

    stack.execute(std::make_unique<SetFillAngleCommand>(id, Angle{1.0}), project);
    REQUIRE(project.findEmbroidery(id)->is_tatami());
    CHECK(std::get<document::TatamiParams>(project.findEmbroidery(id)->params).angle.radians ==
          Catch::Approx(1.0));

    CHECK(stack.undo(project));
    CHECK(std::get<document::TatamiParams>(project.findEmbroidery(id)->params).angle.radians ==
          Catch::Approx(0.0));
    CHECK(stack.redo(project));
    CHECK(std::get<document::TatamiParams>(project.findEmbroidery(id)->params).angle.radians ==
          Catch::Approx(1.0));
}

TEST_CASE("SetFillAngleCommand : sans effet sur un objet non-tatami") {
    document::Project project;
    UndoStack stack;

    document::EmbroideryObject running;
    running.id = project.object_ids.next();
    running.params = document::RunningStitchParams{};
    stack.execute(std::make_unique<AddEmbroideryObjectCommand>(running), project);
    const ObjectId id = project.embroidery_objects[0].id;

    // La commande ne doit ni jeter ni altérer le type de point.
    stack.execute(std::make_unique<SetFillAngleCommand>(id, Angle{1.0}), project);
    CHECK(std::holds_alternative<document::RunningStitchParams>(project.findEmbroidery(id)->params));
    CHECK(stack.undo(project));
    CHECK(std::holds_alternative<document::RunningStitchParams>(project.findEmbroidery(id)->params));
}

TEST_CASE("ConvertFillsToTatamiCommand : satin -> tatami, undo restaure le satin") {
    document::Project project;
    UndoStack stack;

    document::EmbroideryObject sat;
    sat.id = project.object_ids.next();
    document::SatinParams sp;
    sp.density = Micrometers{321};  // marqueur pour verifier la restauration exacte
    sat.params = sp;
    stack.execute(std::make_unique<AddEmbroideryObjectCommand>(sat), project);
    const ObjectId id = project.embroidery_objects[0].id;
    REQUIRE(project.findEmbroidery(id)->is_satin());

    stack.execute(std::make_unique<ConvertFillsToTatamiCommand>(std::vector<ObjectId>{id}), project);
    CHECK(project.findEmbroidery(id)->is_tatami());

    CHECK(stack.undo(project));
    REQUIRE(project.findEmbroidery(id)->is_satin());
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(id)->params).density ==
          Micrometers{321});

    CHECK(stack.redo(project));
    CHECK(project.findEmbroidery(id)->is_tatami());
}

TEST_CASE("SetStitchTypeCommand : change de type, undo restaure l'exact") {
    document::Project project;
    UndoStack stack;

    document::EmbroideryObject e;
    e.id = project.object_ids.next();
    document::RunningStitchParams rp;
    rp.repeats = 3;
    e.params = rp;
    stack.execute(std::make_unique<AddEmbroideryObjectCommand>(e), project);
    const ObjectId id = project.embroidery_objects[0].id;

    stack.execute(
        std::make_unique<SetStitchTypeCommand>(id, document::TatamiParams{}, "tatami"), project);
    CHECK(project.findEmbroidery(id)->is_tatami());

    CHECK(stack.undo(project));
    REQUIRE(
        std::holds_alternative<document::RunningStitchParams>(project.findEmbroidery(id)->params));
    CHECK(std::get<document::RunningStitchParams>(project.findEmbroidery(id)->params).repeats == 3);

    CHECK(stack.redo(project));
    CHECK(project.findEmbroidery(id)->is_tatami());
}

TEST_CASE("SetStitchParamsCommand : edite les parametres, undo restaure exact") {
    document::Project project;
    UndoStack stack;

    document::EmbroideryObject e;
    e.id = project.object_ids.next();
    document::TatamiParams tp;
    tp.row_spacing = Micrometers{400};
    e.params = tp;
    stack.execute(std::make_unique<AddEmbroideryObjectCommand>(e), project);
    const ObjectId id = project.embroidery_objects[0].id;

    document::TatamiParams edited;
    edited.row_spacing = Micrometers{800};
    stack.execute(std::make_unique<SetStitchParamsCommand>(id, edited), project);
    CHECK(std::get<document::TatamiParams>(project.findEmbroidery(id)->params).row_spacing ==
          Micrometers{800});

    CHECK(stack.undo(project));
    CHECK(std::get<document::TatamiParams>(project.findEmbroidery(id)->params).row_spacing ==
          Micrometers{400});
    CHECK(stack.redo(project));
    CHECK(std::get<document::TatamiParams>(project.findEmbroidery(id)->params).row_spacing ==
          Micrometers{800});
}

TEST_CASE("guides satin : ajout deplacement suppression sont annulables exactement") {
    document::Project project;
    UndoStack stack;
    document::EmbroideryObject object;
    object.id = project.object_ids.next();
    document::SatinParams params;
    params.rungs = {{{Micrometers{0}, Micrometers{0}},
                     {Micrometers{0}, Micrometers{4'000}}},
                    {{Micrometers{10'000}, Micrometers{0}},
                     {Micrometers{10'000}, Micrometers{4'000}}}};
    object.params = params;
    project.embroidery_objects.push_back(object);
    const ObjectId id = object.id;

    const document::SatinRung added{{Micrometers{5'000}, Micrometers{0}},
                                    {Micrometers{5'000}, Micrometers{4'000}}};
    stack.execute(std::make_unique<AddSatinGuideCommand>(id, added, 1), project);
    REQUIRE(std::get<document::SatinParams>(project.findEmbroidery(id)->params).rungs.size() == 3);
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(id)->params).rungs[1] == added);
    CHECK(stack.undoName() == "Ajouter un guide satin");
    CHECK(stack.undo(project));
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(id)->params).rungs == params.rungs);
    CHECK(stack.redo(project));

    const document::SatinRung moved{{Micrometers{6'000}, Micrometers{100}},
                                    {Micrometers{6'000}, Micrometers{3'900}}};
    stack.execute(std::make_unique<MoveSatinGuideCommand>(id, 1, moved), project);
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(id)->params).rungs[1] == moved);
    CHECK(stack.undo(project));
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(id)->params).rungs[1] == added);

    stack.execute(std::make_unique<RemoveSatinGuideCommand>(id, 1), project);
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(id)->params).rungs == params.rungs);
    CHECK(stack.undoName() == "Supprimer un guide satin");
    CHECK(stack.undo(project));
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(id)->params).rungs[1] == added);
}

TEST_CASE("guides satin coordonnes : plusieurs sections forment une seule commande undo redo") {
    document::Project project;
    UndoStack stack;
    document::SatinParams params;
    params.rungs = {{{Micrometers{0}, Micrometers{0}},
                     {Micrometers{0}, Micrometers{4'000}}},
                    {{Micrometers{10'000}, Micrometers{0}},
                     {Micrometers{10'000}, Micrometers{4'000}}}};
    document::EmbroideryObject first;
    first.id = project.object_ids.next();
    first.params = params;
    document::EmbroideryObject second;
    second.id = project.object_ids.next();
    second.params = params;
    project.embroidery_objects = {first, second};

    const document::SatinRung movedFirst{{Micrometers{500}, Micrometers{0}},
                                          {Micrometers{500}, Micrometers{4'000}}};
    const document::SatinRung movedSecond{{Micrometers{9'500}, Micrometers{0}},
                                           {Micrometers{9'500}, Micrometers{4'000}}};
    stack.execute(std::make_unique<MoveSatinGuidesCommand>(std::vector<SatinGuideEdit>{
                      {first.id, 0, movedFirst}, {second.id, 1, movedSecond}}),
                  project);
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(first.id)->params).rungs[0] ==
          movedFirst);
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(second.id)->params).rungs[1] ==
          movedSecond);
    CHECK(stack.undoName() == "Déplacer des guides satin coordonnés");

    REQUIRE(stack.undo(project));
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(first.id)->params).rungs ==
          params.rungs);
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(second.id)->params).rungs ==
          params.rungs);
    REQUIRE(stack.redo(project));
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(first.id)->params).rungs[0] ==
          movedFirst);
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(second.id)->params).rungs[1] ==
          movedSecond);
}

TEST_CASE("guides satin coordonnes : ajout multi-section atomique et annulable") {
    document::Project project;
    UndoStack stack;
    document::SatinParams params;
    params.rungs = {{{Micrometers{0}, Micrometers{0}},
                     {Micrometers{0}, Micrometers{4'000}}},
                    {{Micrometers{10'000}, Micrometers{0}},
                     {Micrometers{10'000}, Micrometers{4'000}}}};
    document::EmbroideryObject first;
    first.id = project.object_ids.next();
    first.params = params;
    document::EmbroideryObject second;
    second.id = project.object_ids.next();
    second.params = params;
    project.embroidery_objects = {first, second};
    const document::SatinRung guide{{Micrometers{5'000}, Micrometers{0}},
                                     {Micrometers{5'000}, Micrometers{4'000}}};

    stack.execute(std::make_unique<AddSatinGuidesCommand>(
                      std::vector<SatinGuideAddition>{{first.id, guide, 1},
                                                       {second.id, guide, 1}}),
                  project);
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(first.id)->params).rungs.size() ==
          3);
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(second.id)->params).rungs.size() ==
          3);
    CHECK(stack.undoName() == "Ajouter des guides satin coordonnés");
    REQUIRE(stack.undo(project));
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(first.id)->params).rungs ==
          params.rungs);
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(second.id)->params).rungs ==
          params.rungs);
    REQUIRE(stack.redo(project));
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(first.id)->params).rungs[1] ==
          guide);
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(second.id)->params).rungs[1] ==
          guide);

    AddSatinGuidesCommand stale(
        {{first.id, guide, 1}, {ObjectId{999}, guide, 0}});
    stale.apply(project);
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(first.id)->params).rungs.size() ==
          3);
}

TEST_CASE("guides satin coordonnes : une cible obsolete interdit toute mutation partielle") {
    document::Project project;
    document::SatinParams params;
    params.rungs = {{{Micrometers{0}, Micrometers{0}},
                     {Micrometers{0}, Micrometers{4'000}}},
                    {{Micrometers{10'000}, Micrometers{0}},
                     {Micrometers{10'000}, Micrometers{4'000}}}};
    document::EmbroideryObject object;
    object.id = project.object_ids.next();
    object.params = params;
    project.embroidery_objects.push_back(object);
    const document::SatinRung moved{{Micrometers{500}, Micrometers{0}},
                                     {Micrometers{500}, Micrometers{4'000}}};

    MoveSatinGuidesCommand stale({{object.id, 0, moved}, {ObjectId{999}, 0, moved}});
    stale.apply(project);
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(object.id)->params).rungs ==
          params.rungs);

    MoveSatinGuidesCommand duplicate({{object.id, 0, moved}, {object.id, 0, moved}});
    duplicate.apply(project);
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(object.id)->params).rungs ==
          params.rungs);
}

TEST_CASE("guides satin coordonnes : suppression de groupe atomique et annulable") {
    document::Project project;
    UndoStack stack;
    document::SatinParams params;
    params.rungs = {
        {{Micrometers{0}, Micrometers{0}}, {Micrometers{0}, Micrometers{4'000}}},
        {{Micrometers{5'000}, Micrometers{0}},
         {Micrometers{5'000}, Micrometers{4'000}},
         std::uint32_t{2}},
        {{Micrometers{10'000}, Micrometers{0}}, {Micrometers{10'000}, Micrometers{4'000}}}};
    document::EmbroideryObject first;
    first.id = project.object_ids.next();
    first.params = params;
    document::EmbroideryObject second;
    second.id = project.object_ids.next();
    second.params = params;
    project.embroidery_objects = {first, second};

    stack.execute(std::make_unique<RemoveSatinGuidesCommand>(
                      std::vector<SatinGuideRemoval>{{first.id, 1}, {second.id, 1}}),
                  project);
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(first.id)->params).rungs.size() ==
          2);
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(second.id)->params).rungs.size() ==
          2);
    CHECK(stack.undoName() == "Supprimer des guides satin coordonnés");

    REQUIRE(stack.undo(project));
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(first.id)->params).rungs ==
          params.rungs);
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(second.id)->params).rungs ==
          params.rungs);
    REQUIRE(stack.redo(project));
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(first.id)->params).rungs.size() ==
          2);
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(second.id)->params).rungs.size() ==
          2);
}

TEST_CASE("guides satin coordonnes : une suppression obsolete interdit toute mutation partielle") {
    document::Project project;
    document::SatinParams params;
    params.rungs = {
        {{Micrometers{0}, Micrometers{0}}, {Micrometers{0}, Micrometers{4'000}}},
        {{Micrometers{10'000}, Micrometers{0}}, {Micrometers{10'000}, Micrometers{4'000}}}};
    document::EmbroideryObject object;
    object.id = project.object_ids.next();
    object.params = params;
    project.embroidery_objects.push_back(object);

    // Index hors bornes sur une cible : rejet total, l'objet valide n'est pas
    // touché non plus.
    RemoveSatinGuidesCommand outOfRange({{object.id, 0}, {ObjectId{999}, 0}});
    outOfRange.apply(project);
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(object.id)->params).rungs ==
          params.rungs);

    // Doublon (meme objet, meme index) : rejet total plutot qu'une suppression
    // simple silencieuse.
    RemoveSatinGuidesCommand duplicate({{object.id, 0}, {object.id, 0}});
    duplicate.apply(project);
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(object.id)->params).rungs ==
          params.rungs);

    // Liste vide : rejet (aucune suppression n'a de sens).
    RemoveSatinGuidesCommand empty(std::vector<SatinGuideRemoval>{});
    empty.apply(project);
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(object.id)->params).rungs ==
          params.rungs);

    // Une commande directe ne peut pas réduire une colonne sous les deux
    // guides terminaux minimaux, même si l'UI a déjà son propre garde-fou.
    RemoveSatinGuidesCommand tooFew({{object.id, 0}});
    tooFew.apply(project);
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(object.id)->params).rungs ==
          params.rungs);
}

TEST_CASE("guides satin coordonnes : suppression de plusieurs guides dans le meme objet") {
    // Cas défensif non produit par l'éditeur actuel (un groupe lié compte un
    // guide par section/objet) mais que RemoveSatinGuidesCommand doit traiter
    // sans corrompre les index : suppression descendante puis ré-insertion
    // ascendante doit restaurer exactement l'ordre d'origine.
    document::Project project;
    UndoStack stack;
    document::SatinParams params;
    params.rungs = {
        {{Micrometers{0}, Micrometers{0}}, {Micrometers{0}, Micrometers{4'000}}},
        {{Micrometers{2'500}, Micrometers{0}}, {Micrometers{2'500}, Micrometers{4'000}}},
        {{Micrometers{5'000}, Micrometers{0}}, {Micrometers{5'000}, Micrometers{4'000}}},
        {{Micrometers{10'000}, Micrometers{0}}, {Micrometers{10'000}, Micrometers{4'000}}}};
    document::EmbroideryObject object;
    object.id = project.object_ids.next();
    object.params = params;
    project.embroidery_objects.push_back(object);

    stack.execute(std::make_unique<RemoveSatinGuidesCommand>(
                      std::vector<SatinGuideRemoval>{{object.id, 1}, {object.id, 2}}),
                  project);
    const auto& afterRemove =
        std::get<document::SatinParams>(project.findEmbroidery(object.id)->params).rungs;
    REQUIRE(afterRemove.size() == 2);
    CHECK(afterRemove[0] == params.rungs[0]);
    CHECK(afterRemove[1] == params.rungs[3]);

    REQUIRE(stack.undo(project));
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(object.id)->params).rungs ==
          params.rungs);
}

TEST_CASE("guides satin coordonnes : le document change entre apply et revert interdit la "
          "restauration partielle") {
    document::Project project;
    document::SatinParams params;
    params.rungs = {
        {{Micrometers{0}, Micrometers{0}}, {Micrometers{0}, Micrometers{4'000}}},
        {{Micrometers{5'000}, Micrometers{0}}, {Micrometers{5'000}, Micrometers{4'000}}},
        {{Micrometers{10'000}, Micrometers{0}}, {Micrometers{10'000}, Micrometers{4'000}}}};
    document::EmbroideryObject first;
    first.id = project.object_ids.next();
    first.params = params;
    document::EmbroideryObject second;
    second.id = project.object_ids.next();
    second.params = params;
    project.embroidery_objects = {first, second};

    RemoveSatinGuidesCommand command({{first.id, 1}, {second.id, 1}});
    command.apply(project);
    REQUIRE(
        std::get<document::SatinParams>(project.findEmbroidery(first.id)->params).rungs.size() ==
        2);
    REQUIRE(
        std::get<document::SatinParams>(project.findEmbroidery(second.id)->params).rungs.size() ==
        2);

    // Entre apply et revert, `second` perd toute sa géométrie satin (édité
    // ailleurs) : son index d'insertion d'origine n'est plus valide.
    std::get<document::SatinParams>(project.findEmbroidery(second.id)->params).rungs.clear();

    command.revert(project);
    // Tout ou rien : `first` n'est PAS restauré non plus, même si sa propre
    // cible était encore valide à elle seule.
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(first.id)->params).rungs.size() ==
          2);
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(second.id)->params).rungs.empty());
}

TEST_CASE("SetCanvasCommand : change la taille du cadre, undo restaure") {
    document::Project project;
    UndoStack stack;
    REQUIRE(project.canvas.width == Micrometers{100'000});

    document::Canvas c;
    c.width = Micrometers{160'000};
    c.height = Micrometers{260'000};
    stack.execute(std::make_unique<SetCanvasCommand>(c), project);
    CHECK(project.canvas.width == Micrometers{160'000});
    CHECK(project.canvas.height == Micrometers{260'000});

    CHECK(stack.undo(project));
    CHECK(project.canvas.width == Micrometers{100'000});
    CHECK(project.canvas.height == Micrometers{100'000});
    CHECK(stack.redo(project));
    CHECK(project.canvas.width == Micrometers{160'000});
}

// ---------------------------------------------------------------------------
// Retouches manuelles de points (Lot 8.1)
// ---------------------------------------------------------------------------

namespace {

Vec2um um(std::int32_t x, std::int32_t y) { return Vec2um{Micrometers{x}, Micrometers{y}}; }

document::Project project_with_embroidery() {
    document::Project project;
    document::EmbroideryObject e;
    e.id = project.object_ids.next();
    e.params = document::RunningStitchParams{};
    project.embroidery_objects.push_back(e);
    return project;
}

}  // namespace

TEST_CASE("MoveStitchPointCommand : transitionne Clean -> ManuallyEdited, undo restaure Clean exact") {
    auto project = project_with_embroidery();
    const ObjectId id = project.embroidery_objects[0].id;
    UndoStack stack;
    constexpr std::uint64_t fp = 0xABCDEFu;
    constexpr std::uint32_t count = 4;

    stack.execute(
        std::make_unique<MoveStitchPointCommand>(id, std::size_t{1}, um(500, 500), fp, count),
        project);

    auto* obj = project.findEmbroidery(id);
    REQUIRE(obj != nullptr);
    REQUIRE(obj->overrides.size() == 1);
    CHECK(obj->overrides[0].base_index == 1);
    REQUIRE(obj->overrides[0].moved_to.has_value());
    CHECK(*obj->overrides[0].moved_to == um(500, 500));
    CHECK(obj->edited_fingerprint == fp);
    CHECK(obj->edited_point_count == count);

    CHECK(stack.undo(project));
    obj = project.findEmbroidery(id);
    CHECK(obj->overrides.empty());
    CHECK(obj->edited_fingerprint == 0);
    CHECK(obj->edited_point_count == 0);

    CHECK(stack.redo(project));
    obj = project.findEmbroidery(id);
    REQUIRE(obj->overrides.size() == 1);
    CHECK(*obj->overrides[0].moved_to == um(500, 500));
}

TEST_CASE("SetStitchPointTypeCommand : force Stitch<->Jump, undo exact") {
    auto project = project_with_embroidery();
    const ObjectId id = project.embroidery_objects[0].id;
    UndoStack stack;

    stack.execute(std::make_unique<SetStitchPointTypeCommand>(
                      id, std::size_t{3}, document::StitchPointType::Jump, 1, 4),
                  project);

    auto* obj = project.findEmbroidery(id);
    REQUIRE(obj->overrides.size() == 1);
    REQUIRE(obj->overrides[0].forced_type.has_value());
    CHECK(*obj->overrides[0].forced_type == document::StitchPointType::Jump);

    CHECK(stack.undo(project));
    obj = project.findEmbroidery(id);
    CHECK(obj->overrides.empty());

    CHECK(stack.redo(project));
    obj = project.findEmbroidery(id);
    REQUIRE(obj->overrides.size() == 1);
    CHECK(*obj->overrides[0].forced_type == document::StitchPointType::Jump);
}

TEST_CASE("SetStitchTrimCommand : bascule trim_after, undo exact") {
    auto project = project_with_embroidery();
    const ObjectId id = project.embroidery_objects[0].id;
    UndoStack stack;

    stack.execute(std::make_unique<SetStitchTrimCommand>(id, std::size_t{0}, true, 1, 4), project);

    auto* obj = project.findEmbroidery(id);
    REQUIRE(obj->overrides.size() == 1);
    CHECK(obj->overrides[0].trim_after);

    CHECK(stack.undo(project));
    obj = project.findEmbroidery(id);
    CHECK(obj->overrides.empty());

    CHECK(stack.redo(project));
    obj = project.findEmbroidery(id);
    REQUIRE(obj->overrides.size() == 1);
    CHECK(obj->overrides[0].trim_after);
}

// ---------------------------------------------------------------------------
// Revue corrective 8.1, point 5 : SetStitchTrimCommand(false) sans override
// existant ne doit ni creer d'entree vide, ni transitionner Clean ->
// ManuallyEdited.
// ---------------------------------------------------------------------------

TEST_CASE("SetStitchTrimCommand(false) sur un index sans override existant : no-op exact, reste Clean") {
    auto project = project_with_embroidery();
    const ObjectId id = project.embroidery_objects[0].id;
    UndoStack stack;

    stack.execute(std::make_unique<SetStitchTrimCommand>(id, std::size_t{2}, false, 1, 4), project);

    auto* obj = project.findEmbroidery(id);
    REQUIRE(obj != nullptr);
    CHECK(obj->overrides.empty());
    CHECK(obj->edited_fingerprint == 0);
    CHECK(obj->edited_point_count == 0);

    // undo() sur une commande qui n'a rien modifie doit rester un no-op --
    // pas de transition fantome, pas de plantage.
    CHECK(stack.undo(project));
    obj = project.findEmbroidery(id);
    CHECK(obj->overrides.empty());
    CHECK(obj->edited_fingerprint == 0);

    CHECK(stack.redo(project));
    obj = project.findEmbroidery(id);
    CHECK(obj->overrides.empty());
}

TEST_CASE("SetStitchTrimCommand(false) sur un index sans override, avec d'autres index deja "
          "retouches : n'ajoute rien") {
    auto project = project_with_embroidery();
    const ObjectId id = project.embroidery_objects[0].id;
    auto* obj = project.findEmbroidery(id);
    document::StitchOverride existing;
    existing.base_index = 0;
    existing.trim_after = true;
    obj->overrides = {existing};
    obj->edited_fingerprint = 1;
    obj->edited_point_count = 4;

    UndoStack stack;
    // index 2 n'a pas d'override : trim_after(false) doit rester un no-op,
    // sans toucher a l'entree existante de l'index 0.
    stack.execute(std::make_unique<SetStitchTrimCommand>(id, std::size_t{2}, false, 1, 4), project);

    obj = project.findEmbroidery(id);
    REQUIRE(obj->overrides.size() == 1);
    CHECK(obj->overrides[0].base_index == 0);
    CHECK(obj->overrides[0].trim_after);

    CHECK(stack.undo(project));
    obj = project.findEmbroidery(id);
    REQUIRE(obj->overrides.size() == 1);
    CHECK(obj->overrides[0].base_index == 0);
}

TEST_CASE("SetStitchTrimCommand(false) efface le dernier champ effectif d'une entree existante : "
          "l'entree est retiree, undo la restaure exacte") {
    auto project = project_with_embroidery();
    const ObjectId id = project.embroidery_objects[0].id;
    auto* obj = project.findEmbroidery(id);
    document::StitchOverride existing;
    existing.base_index = 2;
    existing.trim_after = true;  // seul champ effectif de cette entree
    obj->overrides = {existing};
    obj->edited_fingerprint = 42;
    obj->edited_point_count = 4;

    UndoStack stack;
    stack.execute(std::make_unique<SetStitchTrimCommand>(id, std::size_t{2}, false, 42, 4), project);

    obj = project.findEmbroidery(id);
    // L'entree n'avait plus aucun champ effectif une fois trim_after efface
    // -- retiree plutot que laissee vide (invalide a la relecture .osp).
    CHECK(obj->overrides.empty());
    // L'objet reste par ailleurs "retouche" au sens metadonnees (d'autres
    // commandes ont pu deja poser fingerprint/count) : ce test ne verifie que
    // l'absence d'entree fantome, pas une remise a Clean totale.

    CHECK(stack.undo(project));
    obj = project.findEmbroidery(id);
    REQUIRE(obj->overrides.size() == 1);
    CHECK(obj->overrides[0].base_index == 2);
    CHECK(obj->overrides[0].trim_after);

    CHECK(stack.redo(project));
    obj = project.findEmbroidery(id);
    CHECK(obj->overrides.empty());
}

TEST_CASE("Deux commandes sur le meme base_index partagent une seule entree, undo la seconde seule") {
    auto project = project_with_embroidery();
    const ObjectId id = project.embroidery_objects[0].id;
    UndoStack stack;
    constexpr std::uint64_t fp = 111;
    constexpr std::uint32_t count = 4;

    stack.execute(
        std::make_unique<MoveStitchPointCommand>(id, std::size_t{2}, um(10, 10), fp, count),
        project);
    // La vue brute n'a pas change entre les deux commandes (aucun point genere
    // n'a ete ajoute/retire) : meme fp/count, l'objet reste ManuallyEdited (pas
    // Dirty) pour la deuxieme commande.
    stack.execute(std::make_unique<SetStitchPointTypeCommand>(
                      id, std::size_t{2}, document::StitchPointType::Jump, fp, count),
                  project);

    auto* obj = project.findEmbroidery(id);
    REQUIRE(obj->overrides.size() == 1);  // une seule entree, pas deux
    REQUIRE(obj->overrides[0].moved_to.has_value());
    CHECK(*obj->overrides[0].moved_to == um(10, 10));
    REQUIRE(obj->overrides[0].forced_type.has_value());
    CHECK(*obj->overrides[0].forced_type == document::StitchPointType::Jump);

    CHECK(stack.undo(project));  // annule seulement SetStitchPointTypeCommand
    obj = project.findEmbroidery(id);
    REQUIRE(obj->overrides.size() == 1);
    CHECK(*obj->overrides[0].moved_to == um(10, 10));       // toujours present
    CHECK_FALSE(obj->overrides[0].forced_type.has_value());  // type restaure
    CHECK(obj->edited_fingerprint == fp);  // deja pose par la 1ere commande

    CHECK(stack.undo(project));  // annule MoveStitchPointCommand -> Clean total
    obj = project.findEmbroidery(id);
    CHECK(obj->overrides.empty());
    CHECK(obj->edited_fingerprint == 0);
    CHECK(obj->edited_point_count == 0);
}

TEST_CASE("commande d'edition de point refusee sur un objet Dirty : aucune mutation, undo neutre") {
    auto project = project_with_embroidery();
    const ObjectId id = project.embroidery_objects[0].id;
    auto* obj = project.findEmbroidery(id);
    document::StitchOverride existing;
    existing.base_index = 0;
    existing.trim_after = true;
    obj->overrides = {existing};
    obj->edited_fingerprint = 999;
    obj->edited_point_count = 4;

    UndoStack stack;
    // raw_fingerprint/raw_point_count (vue brute ACTUELLE) different de ceux
    // memorises sur l'objet -> Dirty, aucune commande ne doit rouvrir l'edition.
    stack.execute(
        std::make_unique<MoveStitchPointCommand>(id, std::size_t{1}, um(1, 1), 1'000, 4), project);

    obj = project.findEmbroidery(id);
    REQUIRE(obj->overrides.size() == 1);
    CHECK(obj->overrides[0].base_index == 0);
    CHECK_FALSE(obj->overrides[0].moved_to.has_value());
    CHECK(obj->edited_fingerprint == 999);

    CHECK(stack.undo(project));
    obj = project.findEmbroidery(id);
    REQUIRE(obj->overrides.size() == 1);
    CHECK(obj->overrides[0].base_index == 0);
    CHECK(obj->edited_fingerprint == 999);
}

TEST_CASE("MoveStitchPointCommand : base_index hors bornes de la vue brute -- aucune mutation") {
    auto project = project_with_embroidery();
    const ObjectId id = project.embroidery_objects[0].id;
    UndoStack stack;

    stack.execute(
        std::make_unique<MoveStitchPointCommand>(id, std::size_t{10}, um(1, 1), 1, 4), project);

    auto* obj = project.findEmbroidery(id);
    CHECK(obj->overrides.empty());
    CHECK(stack.undo(project));
    obj = project.findEmbroidery(id);
    CHECK(obj->overrides.empty());
}

TEST_CASE("MoveStitchPointCommand : objet introuvable -- aucune mutation, aucun plantage") {
    document::Project project;
    UndoStack stack;
    stack.execute(std::make_unique<MoveStitchPointCommand>(ObjectId{999}, std::size_t{0}, um(1, 1),
                                                            1, 4),
                  project);
    CHECK(project.embroidery_objects.empty());
    CHECK(stack.undo(project));
}

TEST_CASE("DiscardOverridesCommand : vide les retouches (y compris Dirty), undo restaure exactement") {
    auto project = project_with_embroidery();
    const ObjectId id = project.embroidery_objects[0].id;
    auto* obj = project.findEmbroidery(id);
    document::StitchOverride ov;
    ov.base_index = 1;
    ov.moved_to = um(7, 7);
    obj->overrides = {ov};
    obj->edited_fingerprint = 555;  // volontairement incoherent (simule Dirty) :
    obj->edited_point_count = 4;    // Discard reste valable dans tous les cas.

    UndoStack stack;
    stack.execute(std::make_unique<DiscardOverridesCommand>(id), project);
    obj = project.findEmbroidery(id);
    CHECK(obj->overrides.empty());
    CHECK(obj->edited_fingerprint == 0);
    CHECK(obj->edited_point_count == 0);

    CHECK(stack.undo(project));
    obj = project.findEmbroidery(id);
    REQUIRE(obj->overrides.size() == 1);
    CHECK(*obj->overrides[0].moved_to == um(7, 7));
    CHECK(obj->edited_fingerprint == 555);
    CHECK(obj->edited_point_count == 4);

    CHECK(stack.redo(project));
    obj = project.findEmbroidery(id);
    CHECK(obj->overrides.empty());
}

TEST_CASE("DiscardOverridesCommand : objet introuvable -- aucune mutation, aucun plantage") {
    document::Project project;
    UndoStack stack;
    stack.execute(std::make_unique<DiscardOverridesCommand>(ObjectId{999}), project);
    CHECK(stack.undo(project));
}

// ---------------------------------------------------------------------------
// Colonne satin manuelle : commandes d'edition des rails + creation par lot
// ---------------------------------------------------------------------------

namespace {

document::Project project_with_manual_satin() {
    document::Project project;
    document::EmbroideryObject object;
    object.id = project.object_ids.next();
    document::SatinParams params;
    params.rail_a.closed = false;
    params.rail_a.nodes = {geometry::PathNode{um(0, 0), geometry::NodeType::Corner, {}, {}},
                           geometry::PathNode{um(10'000, 0), geometry::NodeType::Corner, {}, {}}};
    params.rail_b.closed = false;
    params.rail_b.nodes = {geometry::PathNode{um(0, 4'000), geometry::NodeType::Corner, {}, {}},
                           geometry::PathNode{um(10'000, 4'000), geometry::NodeType::Corner, {}, {}}};
    params.rungs = {{um(0, 0), um(0, 4'000), std::nullopt},
                    {um(10'000, 0), um(10'000, 4'000), std::nullopt}};
    object.params = params;
    project.embroidery_objects.push_back(object);
    return project;
}

}  // namespace

TEST_CASE("MoveSatinRailNodeCommand : deplace un noeud d'un rail, undo restaure exact") {
    auto project = project_with_manual_satin();
    const ObjectId id = project.embroidery_objects[0].id;
    UndoStack stack;

    stack.execute(std::make_unique<MoveSatinRailNodeCommand>(id, SatinRailSide::RailA, 0,
                                                              um(0, 0), um(100, 200)),
                  project);
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(id)->params).rail_a.nodes[0].pos ==
          um(100, 200));
    // Le rail B n'est pas touche par une commande visant le rail A.
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(id)->params).rail_b.nodes[0].pos ==
          um(0, 4'000));

    CHECK(stack.undo(project));
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(id)->params).rail_a.nodes[0].pos ==
          um(0, 0));
    CHECK(stack.redo(project));
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(id)->params).rail_a.nodes[0].pos ==
          um(100, 200));
}

TEST_CASE("MoveSatinRailNodeCommand : index hors bornes -- aucune mutation") {
    auto project = project_with_manual_satin();
    const ObjectId id = project.embroidery_objects[0].id;
    UndoStack stack;

    stack.execute(std::make_unique<MoveSatinRailNodeCommand>(id, SatinRailSide::RailA, 99,
                                                              um(0, 0), um(1, 1)),
                  project);
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(id)->params).rail_a.nodes[0].pos ==
          um(0, 0));
}

TEST_CASE("SetSatinRailNodeTypeCommand : bascule Coin/Lisse, undo restaure exact") {
    auto project = project_with_manual_satin();
    const ObjectId id = project.embroidery_objects[0].id;
    UndoStack stack;

    stack.execute(std::make_unique<SetSatinRailNodeTypeCommand>(id, SatinRailSide::RailB, 1,
                                                                 geometry::NodeType::Smooth),
                  project);
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(id)->params).rail_b.nodes[1].type ==
          geometry::NodeType::Smooth);
    CHECK(stack.undo(project));
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(id)->params).rail_b.nodes[1].type ==
          geometry::NodeType::Corner);
}

TEST_CASE("InsertSatinRailNodeCommand : ajoute un noeud par subdivision exacte, undo le retire") {
    auto project = project_with_manual_satin();
    const ObjectId id = project.embroidery_objects[0].id;
    UndoStack stack;

    stack.execute(
        std::make_unique<InsertSatinRailNodeCommand>(id, SatinRailSide::RailA, 0, 0.5), project);
    const auto& railA =
        std::get<document::SatinParams>(project.findEmbroidery(id)->params).rail_a;
    REQUIRE(railA.nodes.size() == 3);
    CHECK(railA.nodes[1].pos == um(5'000, 0));
    CHECK(stack.undoName() == "Ajouter un nœud de rail satin");

    CHECK(stack.undo(project));
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(id)->params).rail_a.nodes.size() ==
          2);
}

TEST_CASE("RemoveSatinRailNodeCommand : retire un noeud, undo le restaure a la meme position") {
    auto project = project_with_manual_satin();
    const ObjectId id = project.embroidery_objects[0].id;
    UndoStack stack;
    // Un 3e noeud, sinon le rail (2 noeuds) refuserait la suppression.
    stack.execute(
        std::make_unique<InsertSatinRailNodeCommand>(id, SatinRailSide::RailA, 0, 0.5), project);

    stack.execute(std::make_unique<RemoveSatinRailNodeCommand>(id, SatinRailSide::RailA, 1),
                  project);
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(id)->params).rail_a.nodes.size() ==
          2);
    CHECK(stack.undo(project));
    REQUIRE(
        std::get<document::SatinParams>(project.findEmbroidery(id)->params).rail_a.nodes.size() ==
        3);
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(id)->params).rail_a.nodes[1].pos ==
          um(5'000, 0));
}

TEST_CASE("RemoveSatinRailNodeCommand : refuse de reduire un rail sous deux noeuds") {
    auto project = project_with_manual_satin();
    const ObjectId id = project.embroidery_objects[0].id;
    UndoStack stack;

    stack.execute(std::make_unique<RemoveSatinRailNodeCommand>(id, SatinRailSide::RailA, 0),
                  project);
    CHECK(std::get<document::SatinParams>(project.findEmbroidery(id)->params).rail_a.nodes.size() ==
          2);
}

TEST_CASE("AddObjectBatchCommand : cree rails + objet de broderie en une seule commande annulable") {
    document::Project project;
    UndoStack stack;

    document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.paths.push_back(geometry::PathSet{});
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.source_vector = vec.id;
    document::SatinParams params;
    params.rail_a.nodes = {geometry::PathNode{um(0, 0), geometry::NodeType::Corner, {}, {}}};
    emb.params = params;

    stack.execute(std::make_unique<AddObjectBatchCommand>(
                      std::vector<document::VectorObject>{vec},
                      std::vector<document::EmbroideryObject>{emb},
                      "Colonne satin (création manuelle)"),
                  project);
    REQUIRE(project.vector_objects.size() == 1);
    REQUIRE(project.embroidery_objects.size() == 1);
    CHECK(stack.undoName() == "Colonne satin (création manuelle)");

    CHECK(stack.undo(project));
    CHECK(project.vector_objects.empty());
    CHECK(project.embroidery_objects.empty());
    CHECK(stack.redo(project));
    CHECK(project.vector_objects.size() == 1);
    CHECK(project.embroidery_objects.size() == 1);
}

TEST_CASE("une nouvelle commande invalide la branche redo") {
    document::Project project;
    UndoStack stack;

    stack.execute(std::make_unique<AppendImageOpCommand>(image::GrayscaleOp{}), project);
    stack.execute(std::make_unique<AppendImageOpCommand>(image::FlipOp{true}), project);
    stack.undo(project);
    CHECK(stack.canRedo());

    stack.execute(std::make_unique<AppendImageOpCommand>(image::QuantizeOp{4}), project);
    CHECK_FALSE(stack.canRedo());
    CHECK(project.ops.size() == 2);
    CHECK(std::holds_alternative<image::QuantizeOp>(project.ops.back()));
}
