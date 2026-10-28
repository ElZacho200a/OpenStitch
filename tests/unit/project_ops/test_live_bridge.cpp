// SPDX-License-Identifier: Apache-2.0
// Pont « projet vivant » (ADR 0001) : révision, instantané, dry-run, commit en un pas d'annulation,
// refus périmé / non approuvé / verrouillé, remplacement du document.
#include <array>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "openstitch/commands/undo_stack.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/geometry/primitives.hpp"
#include "openstitch/project_ops/errors.hpp"
#include "openstitch/project_ops/live_bridge.hpp"

namespace {

using namespace openstitch;

struct Doc {
    document::Project project;
    commands::UndoStack stack;
    ObjectId first;
    ObjectId second;
};

void add_shape(document::Project& p, int x_mm, std::array<std::uint8_t, 3> rgb, ObjectId& out) {
    document::VectorObject v;
    v.id = p.object_ids.next();
    v.name = "Forme";
    v.paths = {
        {geometry::rectangle_path(Vec2um{Micrometers{x_mm * 1000}, Micrometers{0}},
                                  Vec2um{Micrometers{x_mm * 1000 + 10'000}, Micrometers{10'000}}),
         {}}};
    p.vector_objects.push_back(v);
    document::EmbroideryObject e;
    e.id = p.object_ids.next();
    e.name = "Broderie";
    e.source_vector = v.id;
    e.rgb = rgb;
    e.params = document::TatamiParams{};
    out = e.id;
    p.embroidery_objects.push_back(e);
}

void fill(Doc& d) {
    add_shape(d.project, 0, {10, 20, 30}, d.first);
    add_shape(d.project, 20, {40, 50, 60}, d.second);
}

std::string recolor(ObjectId id, const char* hex) {
    return std::string(
               R"({"schema":"openstitch.batch/1","operations":[{"op":"set_color","object":)") +
           std::to_string(id.value) + R"(,"rgb":")" + hex + R"("}]})";
}

const document::EmbroideryObject& find(const document::Project& p, ObjectId id) {
    for (const auto& e : p.embroidery_objects) {
        if (e.id == id) {
            return e;
        }
    }
    FAIL("objet introuvable");
    return p.embroidery_objects.front();
}

bool has_rgb(const document::Project& p, ObjectId id, std::array<std::uint8_t, 3> rgb) {
    return find(p, id).rgb == rgb;
}

} // namespace

TEST_CASE("live bridge commit is one undo step and redo is stable", "[live]") {
    Doc d;
    fill(d);
    project_ops::LiveBridge bridge(d.project, d.stack, "sess");
    const auto rev = bridge.revision();

    const auto r = bridge.commit("sess", rev, recolor(d.first, "#FF0000"), "IA : test", true);
    REQUIRE(r.ok);
    REQUIRE(r.applied);
    CHECK(r.revision > rev);
    CHECK(has_rgb(d.project, d.first, {255, 0, 0}));
    CHECK(d.stack.undoNames().size() == 1);
    CHECK(d.stack.undoName() == "IA : test");

    REQUIRE(d.stack.undo(d.project));
    CHECK(has_rgb(d.project, d.first, {10, 20, 30}));
    CHECK(bridge.revision() > r.revision); // Undo incrémente aussi la révision

    REQUIRE(d.stack.redo(d.project));
    CHECK(has_rgb(d.project, d.first, {255, 0, 0}));
}

TEST_CASE("live bridge preview never alters document, stack or revision", "[live]") {
    Doc d;
    fill(d);
    project_ops::LiveBridge bridge(d.project, d.stack, "sess");
    const auto rev = bridge.revision();

    const auto r = bridge.preview("sess", rev, recolor(d.first, "#00FF00"));
    REQUIRE(r.ok);
    CHECK_FALSE(r.applied);
    CHECK(r.report.dry_run);
    CHECK(has_rgb(d.project, d.first, {10, 20, 30}));
    CHECK_FALSE(d.stack.canUndo());
    CHECK(bridge.revision() == rev);
}

TEST_CASE("live bridge refuses stale, unapproved, closed and foreign sessions", "[live]") {
    Doc d;
    fill(d);
    project_ops::LiveBridge bridge(d.project, d.stack, "sess");
    const auto rev = bridge.revision();

    SECTION("not approved") {
        const auto r = bridge.commit("sess", rev, recolor(d.first, "#FF0000"), "x", false);
        CHECK_FALSE(r.ok);
        CHECK(r.code == project_ops::code::kApprovalRequired);
        CHECK(has_rgb(d.project, d.first, {10, 20, 30}));
        CHECK_FALSE(d.stack.canUndo());
    }
    SECTION("manual change between read and commit") {
        // Modification manuelle concurrente : une première application avance la révision.
        REQUIRE(bridge.commit("sess", rev, recolor(d.second, "#0000FF"), "manuel", true).ok);
        const auto r = bridge.commit("sess", rev, recolor(d.first, "#FF0000"), "x", true);
        CHECK_FALSE(r.ok);
        CHECK(r.code == project_ops::code::kStaleRevision);
        CHECK(has_rgb(d.project, d.first, {10, 20, 30}));
        CHECK(d.stack.undoNames().size() == 1);
    }
    SECTION("undo makes the revision stale") {
        REQUIRE(bridge.commit("sess", rev, recolor(d.second, "#0000FF"), "a", true).ok);
        const auto after = bridge.revision();
        REQUIRE(d.stack.undo(d.project));
        CHECK(bridge.preview("sess", after, recolor(d.first, "#FF0000")).code ==
              project_ops::code::kStaleRevision);
    }
    SECTION("other session id") {
        CHECK(bridge.commit("autre", rev, recolor(d.first, "#FF0000"), "x", true).code ==
              project_ops::code::kSessionClosed);
    }
    SECTION("closed host") {
        bridge.close();
        CHECK(bridge.preview("sess", rev, recolor(d.first, "#FF0000")).code ==
              project_ops::code::kSessionClosed);
    }
    SECTION("document replaced") {
        bridge.reset("nouvelle");
        CHECK(bridge.commit("sess", rev, recolor(d.first, "#FF0000"), "x", true).code ==
              project_ops::code::kSessionClosed);
        CHECK(bridge.revision() > rev);
        CHECK(bridge.session_id() == "nouvelle");
    }
}

TEST_CASE("live bridge rolls back an invalid batch atomically", "[live]") {
    Doc d;
    fill(d);
    project_ops::LiveBridge bridge(d.project, d.stack, "sess");
    const auto rev = bridge.revision();
    // 2e opération sur un objet inexistant : le lot entier est refusé, rien n'est appliqué.
    const std::string batch =
        std::string(R"({"schema":"openstitch.batch/1","operations":[)") +
        R"({"op":"set_color","object":)" + std::to_string(d.first.value) +
        R"(,"rgb":"#FF0000"},{"op":"set_color","object":9999,"rgb":"#FF0000"}]})";
    const auto r = bridge.commit("sess", rev, batch, "x", true);
    CHECK_FALSE(r.ok);
    CHECK_FALSE(r.applied);
    CHECK(has_rgb(d.project, d.first, {10, 20, 30}));
    CHECK_FALSE(d.stack.canUndo());
    CHECK(bridge.revision() == rev);
}

TEST_CASE("live bridge is deterministic across bridges", "[live]") {
    // Même état + même lot => même résultat, quel que soit le pont (déterminisme).
    Doc a;
    Doc b;
    fill(a);
    fill(b);
    project_ops::LiveBridge ba(a.project, a.stack, "s");
    project_ops::LiveBridge bb(b.project, b.stack, "s");
    REQUIRE(ba.commit("s", ba.revision(), recolor(a.second, "#123456"), "x", true).ok);
    REQUIRE(bb.commit("s", bb.revision(), recolor(b.second, "#123456"), "x", true).ok);
    CHECK(find(a.project, a.second).rgb == find(b.project, b.second).rgb);
}

TEST_CASE("live bridge snapshot is frozen at its revision", "[live]") {
    Doc d;
    fill(d);
    project_ops::LiveBridge bridge(d.project, d.stack, "sess");
    const auto snap = bridge.snapshot();
    REQUIRE(bridge.commit("sess", snap.revision, recolor(d.first, "#FF0000"), "x", true).ok);
    CHECK(has_rgb(*snap.project, d.first, {10, 20, 30}));
    CHECK(has_rgb(d.project, d.first, {255, 0, 0}));
    CHECK(snap.session_id == "sess");
    CHECK(bridge.snapshot().revision > snap.revision);
}

TEST_CASE("live bridge generates distinct random session ids", "[live]") {
    Doc d;
    project_ops::LiveBridge a(d.project, d.stack);
    project_ops::LiveBridge b(d.project, d.stack);
    CHECK(a.session_id().size() == 32);
    CHECK(a.session_id() != b.session_id());
}
