// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "document_session.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "preview_service.hpp"
#include "satin_column.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;

namespace {
DocumentSession newSession() {
    DocumentSession s;
    auto created = project_ops::create_empty_project({});
    REQUIRE(created.has_value());
    s.replaceProject(std::move(*created), true);
    return s;
}

std::vector<std::pair<PointMm, PointMm>> ladder() {
    return {{{-10, 0}, {-10, 4}}, {{0, 1}, {0, 5}}, {{10, 0}, {10, 4}}};
}
} // namespace

TEST_CASE("a satin column is created from rung pairs in one undo step", "[desktop_v2][satin]") {
    auto s = newSession();
    const auto depth = s.undoNames().size();
    const auto r = createSatinColumn(s, ladder());
    REQUIRE(r.ok);
    CHECK_FALSE(r.railsCross);
    CHECK(s.undoNames().size() == depth + 1);
    CHECK(s.project().embroidery_objects.size() == 1);
    CHECK(railNodeCount(s.project(), r.objectId, RailSide::A) == 3);
    CHECK(railNodeCount(s.project(), r.objectId, RailSide::B) == 3);
    const auto preview = render_preview(s.snapshot(), s.revision(), 4.0, TaskContext{});
    REQUIRE(preview.has_value());
    CHECK(preview->hits->boundsOf(r.objectId).has_value());
    REQUIRE(s.undo());
    CHECK(s.project().embroidery_objects.empty());
}

TEST_CASE("crossing rails are reported but still created", "[desktop_v2][satin]") {
    auto s = newSession();
    const auto r = createSatinColumn(s, {{{0, 0}, {10, 0}}, {{10, 5}, {0, 5}}});
    REQUIRE(r.ok);
    CHECK(r.railsCross);
}

TEST_CASE("bad satin requests are refused without stacking anything", "[desktop_v2][satin]") {
    auto s = newSession();
    const auto depth = s.undoNames().size();
    CHECK_FALSE(createSatinColumn(s, {}).ok);
    CHECK_FALSE(createSatinColumn(s, {{{0, 0}, {0, 4}}}).ok);
    CHECK_FALSE(createSatinColumn(s, {{{0, 0}, {0, 0}}, {{5, 0}, {5, 4}}}).ok);
    CHECK_FALSE(createSatinColumn(s, {{{0, 0}, {0, 4}}, {{5000, 0}, {5, 4}}}).ok);
    CHECK(s.undoNames().size() == depth);
    CHECK_FALSE(moveSatinRailNode(s, 12345, RailSide::A, 0, {0, 0}).ok);
    CHECK_FALSE(insertSatinRailNode(s, 12345, RailSide::A, 0, 0.5).ok);
}

TEST_CASE("rail nodes can be moved and inserted", "[desktop_v2][satin]") {
    auto s = newSession();
    const auto r = createSatinColumn(s, ladder());
    REQUIRE(r.ok);
    const auto depth = s.undoNames().size();
    REQUIRE(moveSatinRailNode(s, r.objectId, RailSide::A, 1, {0, -2}).ok);
    CHECK(s.undoNames().size() == depth + 1);
    CHECK_FALSE(moveSatinRailNode(s, r.objectId, RailSide::A, 9, {0, 0}).ok);
    CHECK_FALSE(moveSatinRailNode(s, r.objectId, RailSide::A, 1, {5000, 0}).ok);
    REQUIRE(insertSatinRailNode(s, r.objectId, RailSide::B, 0, 0.5).ok);
    CHECK(railNodeCount(s.project(), r.objectId, RailSide::B) == 4);
    CHECK_FALSE(insertSatinRailNode(s, r.objectId, RailSide::B, 0, 1.0).ok);
    CHECK_FALSE(insertSatinRailNode(s, r.objectId, RailSide::B, 9, 0.5).ok);
    REQUIRE(s.undo());
    CHECK(railNodeCount(s.project(), r.objectId, RailSide::B) == 3);
}

TEST_CASE("satin rungs can be added, moved and removed in one undo step each",
          "[desktop_v2][satin][rungs]") {
    auto s = newSession();
    const auto r = createSatinColumn(s, ladder());
    REQUIRE(r.ok);
    REQUIRE(satinRungCount(s.project(), r.objectId) == 3);
    auto depth = s.undoNames().size();

    REQUIRE(insertSatinRungBetween(s, r.objectId, 0).ok);
    CHECK(s.undoNames().size() == depth + 1);
    REQUIRE(satinRungCount(s.project(), r.objectId) == 4);
    const auto rungs = satinRungs(s.project(), r.objectId);
    CHECK(rungs[1].first.x == Catch::Approx(-5.0));
    CHECK(rungs[1].second.y == Catch::Approx(4.5));

    depth = s.undoNames().size();
    REQUIRE(moveSatinRung(s, r.objectId, 1, {-5, 0}, {-5, 3}).ok);
    CHECK(s.undoNames().size() == depth + 1);
    CHECK(satinRungs(s.project(), r.objectId)[1].second.y == Catch::Approx(3.0));
    CHECK_FALSE(moveSatinRung(s, r.objectId, 1, {-5, 0}, {-5, 3}).ok); // déjà là
    CHECK(s.undoNames().size() == depth + 1);

    REQUIRE(removeSatinRung(s, r.objectId, 1).ok);
    CHECK(satinRungCount(s.project(), r.objectId) == 3);
    REQUIRE(s.undo()); // retrait annulé
    CHECK(satinRungCount(s.project(), r.objectId) == 4);
    REQUIRE(s.undo()); // déplacement annulé
    CHECK(satinRungs(s.project(), r.objectId)[1].second.y == Catch::Approx(4.5));
}

TEST_CASE("rung edits refuse bad requests without stacking anything",
          "[desktop_v2][satin][rungs]") {
    auto s = newSession();
    const auto r = createSatinColumn(s, {{{0, 0}, {0, 4}}, {{5, 0}, {5, 4}}});
    REQUIRE(r.ok);
    const auto depth = s.undoNames().size();
    CHECK_FALSE(removeSatinRung(s, r.objectId, 0).ok); // il en faut deux au minimum
    CHECK_FALSE(removeSatinRung(s, r.objectId, 7).ok);
    CHECK_FALSE(addSatinRung(s, r.objectId, 3, {1, 0}, {1, 4}).ok); // position inconnue
    CHECK_FALSE(addSatinRung(s, r.objectId, 1, {1, 1}, {1, 1}).ok); // extrémités confondues
    CHECK_FALSE(addSatinRung(s, r.objectId, 1, {5000, 0}, {1, 4}).ok);
    CHECK_FALSE(insertSatinRungBetween(s, r.objectId, 1).ok);
    CHECK_FALSE(moveSatinRung(s, r.objectId, 5, {0, 0}, {0, 4}).ok);
    CHECK_FALSE(addSatinRung(s, 12345, 0, {0, 0}, {0, 4}).ok);
    CHECK(s.undoNames().size() == depth);
    CHECK(satinRungCount(s.project(), r.objectId) == 2);
    CHECK(satinRungCount(s.project(), 12345) == -1);
}

TEST_CASE("the widest gap and the most redundant rung are found deterministically",
          "[desktop_v2][satin][rungs]") {
    auto s = newSession();
    const auto r = createSatinColumn(
        s, {{{0, 0}, {0, 4}}, {{1, 0}, {1, 4}}, {{2, 0}, {2, 4}}, {{12, 0}, {12, 4}}});
    REQUIRE(r.ok);
    CHECK(widestRungGap(s.project(), r.objectId) == std::optional<std::size_t>{2});
    CHECK(redundantRung(s.project(), r.objectId) == std::optional<std::size_t>{1});
    CHECK_FALSE(widestRungGap(s.project(), 999).has_value());
}

TEST_CASE("the satin diagnosis finds bow-ties, crossing rails and over-wide rungs",
          "[desktop_v2][satin][rungs]") {
    auto s = newSession();
    const auto clean = createSatinColumn(s, ladder());
    REQUIRE(clean.ok);
    const auto ok = diagnoseSatin(s.project(), clean.objectId);
    CHECK(ok.clean());
    CHECK(satinDiagnosisSummary(ok).find("saine") != std::string::npos);

    // Barreaux consécutifs qui se croisent : nœud papillon.
    const auto bow = createSatinColumn(s, {{{0, 0}, {10, 0}}, {{5, -3}, {5, 3}}});
    REQUIRE(bow.ok);
    const auto db = diagnoseSatin(s.project(), bow.objectId);
    CHECK_FALSE(db.railsCross);
    CHECK(db.crossing == std::vector<std::size_t>{0});
    CHECK_FALSE(db.clean());
    CHECK(satinDiagnosisSummary(db).find("papillon") != std::string::npos);

    const auto crossed = createSatinColumn(s, {{{0, 0}, {0, 4}}, {{5, 4}, {5, 0}}});
    REQUIRE(crossed.ok);
    CHECK(diagnoseSatin(s.project(), crossed.objectId).railsCross);

    // Un barreau plus long que la largeur maximale du satin.
    const auto wide = createSatinColumn(s, {{{0, 0}, {0, 40}}, {{5, 0}, {5, 40}}});
    REQUIRE(wide.ok);
    const auto dw = diagnoseSatin(s.project(), wide.objectId);
    CHECK(dw.tooWide.size() == 2);
    CHECK(dw.widestMm == Catch::Approx(40.0));
    CHECK(satinDiagnosisSummary(dw).find("trop large") != std::string::npos);

    CHECK_FALSE(diagnoseSatin(s.project(), 777).isSatin);
}
