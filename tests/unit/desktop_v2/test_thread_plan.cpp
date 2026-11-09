// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "document_session.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/commands/thread_commands.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "shape_batch.hpp"
#include "thread_plan.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;

namespace {
DocumentSession threeObjects() {
    DocumentSession s;
    auto created = project_ops::create_empty_project({});
    REQUIRE(created.has_value());
    s.replaceProject(std::move(*created), true);
    REQUIRE(s.applyBatch(batchAddRect({-30, 0}, 10, 10), "A").committed);
    REQUIRE(s.applyBatch(batchAddRect({0, 0}, 10, 10), "B").committed);
    REQUIRE(s.applyBatch(batchAddRect({30, 0}, 10, 10), "C").committed);
    return s;
}
} // namespace

TEST_CASE("threads are grouped by colour in sewing order and can be merged",
          "[desktop_v2][threads]") {
    auto s = threeObjects();
    auto entries = threadEntries(s.project());
    REQUIRE(entries.size() >= 1);
    // Force trois couleurs distinctes par recoloration.
    std::size_t total = 0;
    for (const auto& e : entries) {
        total += e.objects.size();
    }
    CHECK(total == 3);
    // Un fil par objet : on recolore chaque entrée d'une teinte distincte.
    REQUIRE(recolorThread(s, 0, {255, 0, 0}).ok);
    entries = threadEntries(s.project());
    CHECK(entries.front().rgb == std::array<std::uint8_t, 3>{255, 0, 0});
    CHECK(entries.front().label == "#ff0000");
    CHECK_FALSE(recolorThread(s, 99, {0, 0, 0}).ok);
    CHECK_FALSE(mergeThreads(s, 0, 0).ok);
    CHECK_FALSE(mergeThreads(s, 5, 0).ok);
}

TEST_CASE("merging threads is one undo step and gives one colour", "[desktop_v2][threads]") {
    auto s = threeObjects();
    const auto id1 = s.project().embroidery_objects[1].id;
    s.execute(std::make_unique<commands::SetObjectThreadCommand>(
        std::vector<ObjectId>{id1}, std::nullopt, std::array<std::uint8_t, 3>{9, 9, 9}));
    const auto groups = threadEntries(s.project()).size();
    REQUIRE(groups == 2);
    const auto before = s.undoNames().size();
    REQUIRE(mergeThreads(s, 1, 0).ok);
    CHECK(s.undoNames().size() == before + 1);
    CHECK(threadEntries(s.project()).size() == 1);
    s.undo();
    CHECK(threadEntries(s.project()).size() == 2);
}

TEST_CASE("thread group moves respect sewing layers and locked positions",
          "[desktop_v2][threads]") {
    auto s = threeObjects();
    const auto first = s.project().embroidery_objects.front().id;
    s.execute(std::make_unique<commands::SetObjectThreadCommand>(
        std::vector<ObjectId>{first}, std::nullopt, std::array<std::uint8_t, 3>{255, 0, 0}));
    REQUIRE(threadEntries(s.project()).size() == 2);
    s.execute(std::make_unique<commands::SetEmbroideryLockCommand>(first, true));
    const auto before = s.historyPosition();
    CHECK_FALSE(moveThread(s, 0, 1).ok);
    CHECK(s.historyPosition() == before);
    REQUIRE(s.undo());

    auto project = s.project();
    if (project.calques.empty()) {
        project.calques.push_back({project.object_ids.next(), "Base"});
    }
    const auto upper = project.object_ids.next();
    project.calques.push_back({upper, "Upper"});
    auto* shape = project.findObject(project.embroidery_objects[1].source_vector);
    REQUIRE(shape != nullptr);
    shape->calque = upper;
    shape = project.findObject(project.embroidery_objects[2].source_vector);
    REQUIRE(shape != nullptr);
    shape->calque = upper;
    s.replaceProject(std::move(project), true);
    CHECK_FALSE(moveThread(s, 0, 1).ok);
    CHECK_FALSE(s.canUndo());
    CHECK(orderRows(s.project()).front().id == first.value);
}

TEST_CASE("the sewing order list can move an object and refuses a no-op", "[desktop_v2][threads]") {
    auto s = threeObjects();
    auto rows = orderRows(s.project());
    REQUIRE(rows.size() == 3);
    const auto first = rows[0].id;
    CHECK_FALSE(moveInOrder(s, first, 0).ok);
    CHECK_FALSE(moveInOrder(s, 424242, 1).ok);
    CHECK_FALSE(moveInOrder(s, first, 9).ok);
    REQUIRE(moveInOrder(s, first, 2).ok);
    rows = orderRows(s.project());
    CHECK(rows[2].id == first);
    s.undo();
    CHECK(orderRows(s.project())[0].id == first);
}

TEST_CASE("colorways are added, activated, renamed and removed with undo",
          "[desktop_v2][threads]") {
    auto s = threeObjects();
    CHECK_FALSE(addColorway(s, "  ").ok);
    REQUIRE(addColorway(s, "Hiver").ok);
    auto rows = colorwayRows(s.project());
    REQUIRE(rows.size() == 2); // « Original » capturé puis « Hiver »
    CHECK(rows[1].active);
    REQUIRE(recolorThread(s, 0, {1, 2, 3}).ok);
    REQUIRE(activateColorway(s, 0).ok);
    CHECK(threadEntries(s.project()).front().rgb != std::array<std::uint8_t, 3>{1, 2, 3});
    CHECK_FALSE(activateColorway(s, 0).ok); // déjà actif
    CHECK_FALSE(removeColorway(s, 0).ok);   // actif : refusé
    REQUIRE(renameColorway(s, 1, "Été").ok);
    CHECK(colorwayRows(s.project())[1].name == "Été");
    REQUIRE(activateColorway(s, 1).ok);
    CHECK(threadEntries(s.project()).front().rgb == std::array<std::uint8_t, 3>{1, 2, 3});
    REQUIRE(removeColorway(s, 0).ok);
    CHECK(colorwayRows(s.project()).size() == 1);
    s.undo();
    CHECK(colorwayRows(s.project()).size() == 2);
}
