// SPDX-License-Identifier: Apache-2.0
#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "document_session.hpp"
#include "export_settings.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "shape_batch.hpp"

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
} // namespace

TEST_CASE("export options are read in millimetres and changed in one undo step",
          "[desktop_v2][export]") {
    auto s = newSession();
    auto o = exportOptionsOf(s.project());
    CHECK(o.trimThresholdMm == Catch::Approx(3.0));
    CHECK(o.lockKind == LockKind::BackAndForth);
    CHECK_FALSE(o.designStart.has_value());

    const auto depth = s.undoNames().size();
    o.trimThresholdMm = 5.5;
    o.lockKind = LockKind::Triangle;
    o.lockPasses = 3;
    o.splitLongStitches = true;
    o.maxStitchMm = 6.0;
    o.designStart = PointMm{-20.0, -10.0};
    o.designEnd = PointMm{20.0, 10.0};
    REQUIRE(setExportOptions(s, o).ok);
    CHECK(s.undoNames().size() == depth + 1);
    const auto back = exportOptionsOf(s.project());
    CHECK(back.trimThresholdMm == Catch::Approx(5.5));
    CHECK(back.lockKind == LockKind::Triangle);
    CHECK(back.lockPasses == 3);
    CHECK(back.splitLongStitches);
    REQUIRE(back.designStart.has_value());
    CHECK(back.designStart->x == Catch::Approx(-20.0));
    CHECK(back.designEnd->y == Catch::Approx(10.0));

    CHECK_FALSE(setExportOptions(s, back).ok); // inchangé : refusé sans trace
    CHECK(s.undoNames().size() == depth + 1);
    REQUIRE(s.undo());
    CHECK(exportOptionsOf(s.project()).trimThresholdMm == Catch::Approx(3.0));
    CHECK_FALSE(exportOptionsOf(s.project()).designStart.has_value());
}

TEST_CASE("invalid export options are refused without stacking anything", "[desktop_v2][export]") {
    auto s = newSession();
    const auto depth = s.undoNames().size();
    auto o = exportOptionsOf(s.project());
    auto bad = o;
    bad.trimThresholdMm = 0.1;
    CHECK_FALSE(setExportOptions(s, bad).ok);
    bad = o;
    bad.lockPasses = 9;
    CHECK_FALSE(setExportOptions(s, bad).ok);
    bad = o;
    bad.maxStitchMm = 1.0;
    CHECK_FALSE(setExportOptions(s, bad).ok);
    bad = o;
    bad.minStitchMm = 3.0;
    bad.maxStitchMm = 2.5; // max <= min
    CHECK_FALSE(setExportOptions(s, bad).ok);
    bad = o;
    bad.designStart = PointMm{5000.0, 0.0};
    CHECK_FALSE(setExportOptions(s, bad).ok);
    bad = o;
    bad.trimThresholdMm = std::nan("");
    CHECK_FALSE(setExportOptions(s, bad).ok);
    CHECK(s.undoNames().size() == depth);
}

TEST_CASE("the hoop library is offered and applied with shape, size and margin",
          "[desktop_v2][export]") {
    const auto hoops = hoopChoices();
    REQUIRE(hoops.size() >= 10);
    auto s = newSession();
    const auto depth = s.undoNames().size();
    REQUIRE(applyHoop(s, "round-150", 5.0).ok);
    CHECK(s.undoNames().size() == depth + 1);
    CHECK(currentHoopId(s.project()) == "round-150");
    CHECK(currentMarginMm(s.project()) == Catch::Approx(5.0));
    CHECK(s.project().canvas.shape == document::HoopShape::Ellipse);
    CHECK_FALSE(applyHoop(s, "round-150", 5.0).ok); // déjà celui-là
    CHECK_FALSE(applyHoop(s, "no-such-hoop", 0.0).ok);
    CHECK_FALSE(applyHoop(s, "round-60", -1.0).ok);
    CHECK_FALSE(applyHoop(s, "round-60", 40.0).ok); // marge >= moitié du côté
    CHECK(s.undoNames().size() == depth + 1);
    REQUIRE(applyCustomHoop(s, 120.0, 90.0, 3.0).ok);
    CHECK(currentHoopId(s.project()).empty()); // personnalisé
    CHECK_FALSE(applyCustomHoop(s, 5.0, 90.0, 0.0).ok);
    REQUIRE(s.undo());
    CHECK(currentHoopId(s.project()) == "round-150");
}

TEST_CASE("machines are chosen by profile and refused when unknown", "[desktop_v2][export]") {
    const auto machines = machineChoices();
    REQUIRE(machines.size() >= 4);
    auto s = newSession();
    const auto depth = s.undoNames().size();
    REQUIRE(applyMachine(s, "generic-single-130x180").ok);
    CHECK(s.project().machine_id == "generic-single-130x180");
    CHECK_FALSE(applyMachine(s, "generic-single-130x180").ok);
    CHECK_FALSE(applyMachine(s, "no-such-machine").ok);
    CHECK(s.undoNames().size() == depth + 1);
    REQUIRE(applyMachine(s, "").ok); // aucune machine
    CHECK(s.project().machine_id.empty());
}

TEST_CASE("the pre-export check reports points outside the hoop and machine mismatches",
          "[desktop_v2][export]") {
    auto s = newSession();
    CHECK_FALSE(checkExport(s.project()).ok); // rien à contrôler
    // Le cadre par défaut est déjà 100 x 100 (refusé comme inchangé) : on en prend un autre.
    REQUIRE(applyHoop(s, "rect-130x180", 0.0).ok);
    REQUIRE(s.applyBatch(batchAddRect({0, 0}, 40, 20), "Rectangle").committed);
    const auto fits = checkExport(s.project(), "dst");
    REQUIRE(fits.ok);
    CHECK(fits.fitsHoop);
    CHECK(fits.stitches > 0);

    REQUIRE(applyHoop(s, "round-60", 0.0).ok); // 40 x 20 dans un rond de 60 : tient aux coins ?
    REQUIRE(s.applyBatch(batchAddRect({0, 0}, 120, 50), "Grand").committed);
    const auto tooBig = checkExport(s.project());
    REQUIRE(tooBig.ok);
    CHECK_FALSE(tooBig.fitsHoop);
    CHECK(tooBig.outsideHoop > 0);
    CHECK(tooBig.summary().find("hors de la zone") != std::string::npos);

    REQUIRE(applyMachine(s, "generic-single-100").ok); // champ 100 x 100
    const auto field = checkExport(s.project(), "vp3");
    REQUIRE(field.ok);
    CHECK_FALSE(field.fitsMachineField);
    CHECK_FALSE(field.formatAccepted);
}
