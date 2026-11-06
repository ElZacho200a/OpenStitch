// SPDX-License-Identifier: Apache-2.0
#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "document_session.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "pull_calibration.hpp"
#include "shape_batch.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;

TEST_CASE("the calibration table lists nominal and predicted distances for every metric",
          "[desktop_v2][calibration]") {
    PullCalibration cal;
    const auto rows = cal.rows();
    REQUIRE(rows.size() >= 6);
    bool anyPositive = false;
    for (const auto& r : rows) {
        CHECK_FALSE(r.id.empty());
        // Les métriques d'ordre de couture mesurent un jour entre deux points qui peuvent
        // coïncider : un nominal de 0 est valide, jamais un nombre négatif ou non fini.
        CHECK(std::isfinite(r.nominalMm));
        CHECK(std::isfinite(r.predictedMm));
        CHECK(r.nominalMm >= 0.0);
        CHECK(r.predictedMm >= 0.0);
        CHECK_FALSE(r.hasMeasure);
        anyPositive = anyPositive || (r.nominalMm > 0.0 && r.predictedMm > 0.0);
    }
    CHECK(anyPositive);
    CHECK(cal.measuredCount() == 0);
    CHECK(cal.rmsMm() == 0.0);
    CHECK_FALSE(cal.testSequence().empty());
    CHECK(cal.csv().find("measured_mm") != std::string::npos);
}

TEST_CASE("measurements are typed one by one and refused when invalid",
          "[desktop_v2][calibration]") {
    PullCalibration cal;
    const auto id = cal.rows().front().id;
    CHECK_FALSE(cal.setMeasure("no-such-metric", 10.0));
    CHECK_FALSE(cal.setMeasure(id, -1.0));
    CHECK_FALSE(cal.setMeasure(id, std::nan("")));
    CHECK(cal.measuredCount() == 0);
    const double nominal = cal.rows().front().nominalMm;
    REQUIRE(cal.setMeasure(id, nominal - 0.2));
    CHECK(cal.measuredCount() == 1);
    CHECK(cal.rmsMm() > 0.0);
    CHECK(cal.rows().front().errorMm ==
          Catch::Approx(cal.rows().front().predictedMm - (nominal - 0.2)));
    REQUIRE(cal.clearMeasure(id));
    CHECK(cal.measuredCount() == 0);
}

TEST_CASE("a CSV filled with measures is read back and below six measures the fit refuses",
          "[desktop_v2][calibration]") {
    PullCalibration cal;
    const auto fit0 = cal.fit();
    CHECK_FALSE(fit0.enoughData);
    CHECK(fit0.message.find("insuffisantes") != std::string::npos);

    // Mesures = distances prédites par un autre tissu : le CSV est celui du moteur.
    PullCalibration other("linen");
    for (const auto& r : other.rows()) {
        REQUIRE(other.setMeasure(r.id, r.predictedMm));
    }
    CHECK(cal.applyCsv(other.csv()) == other.rows().size());
    CHECK(cal.measuredCount() == other.rows().size());
    const auto fit = cal.fit();
    CHECK(fit.enoughData);
    CHECK(fit.rmsAfterMm <= fit.rmsBeforeMm);
    CHECK(fit.message.find("non calibré") != std::string::npos);
}

TEST_CASE("changing the fabric keeps the measures and unknown fabrics are refused",
          "[desktop_v2][calibration]") {
    PullCalibration cal;
    const auto id = cal.rows().front().id;
    REQUIRE(cal.setMeasure(id, 20.0));
    CHECK_FALSE(cal.setFabric("velvet"));
    CHECK(cal.fabric() == "default");
    REQUIRE(cal.setFabric("stable"));
    CHECK(cal.fabric() == "stable");
    CHECK(cal.measuredCount() == 1);
    CHECK(PullCalibration("velvet").fabric() == "default");
}

TEST_CASE("the calibration is deterministic", "[desktop_v2][calibration]") {
    PullCalibration a;
    PullCalibration b;
    CHECK(a.csv() == b.csv());
    CHECK(a.testSequence().commands == b.testSequence().commands);
}

TEST_CASE("the project comparison reports shifts and says when the model is not calibrated",
          "[desktop_v2][calibration]") {
    DocumentSession s;
    auto created = project_ops::create_empty_project({});
    REQUIRE(created.has_value());
    s.replaceProject(std::move(*created), true);
    const auto empty = compareProjectPull(s.project());
    CHECK_FALSE(empty.ok);

    REQUIRE(s.applyBatch(batchAddRect({0, 0}, 40, 20), "Rectangle").committed);
    const auto cmp = compareProjectPull(s.project(), "linen");
    REQUIRE(cmp.ok);
    CHECK(cmp.points > 0);
    CHECK(cmp.maxShiftMm >= cmp.meanShiftMm);
    CHECK(cmp.nominalWidthMm > 30.0);
    CHECK(cmp.summary().find("NON calibrée") != std::string::npos);
    CHECK(compareProjectPull(s.project(), "velvet").fabric == "default");
}
