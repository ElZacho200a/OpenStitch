// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "document_session.hpp"
#include "edit_batch.hpp"
#include "inspector_model.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "param_spec.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;

namespace {
struct Doc {
    DocumentSession session;
    std::uint64_t id{0};
};
Doc rectWithType(std::string_view type) {
    Doc d;
    auto created = project_ops::create_empty_project({});
    REQUIRE(created.has_value());
    d.session.replaceProject(std::move(*created), true);
    const auto r = d.session.applyBatch(batchAddRect({0, 0}, 20, 10), "Rectangle");
    REQUIRE(r.committed);
    d.id = *createdObjectId(r.report);
    if (type != "tatami") {
        REQUIRE(d.session.applyBatch(batchSetStitchType({d.id}, type), "Type").committed);
    }
    return d;
}
} // namespace

TEST_CASE("every spec key is readable on its own type and refused on the others",
          "[desktop_v2][params]") {
    for (const std::string_view type : {"running", "tatami", "directional"}) {
        auto d = rectWithType(type);
        const auto& object = d.session.project().embroidery_objects.front();
        const auto specs = paramSpecs(type);
        REQUIRE_FALSE(specs.empty());
        for (const auto& s : specs) {
            INFO(std::string(type) + "." + std::string(s.key));
            const auto v = readParam(object, s.key);
            REQUIRE(v.has_value());
            CHECK(*v >= s.min - 1e-6);
            CHECK(*v <=
                  s.max + 1e-6); // la valeur par défaut du moteur est dans la plage de la table
        }
    }
    auto tatami = rectWithType("tatami");
    CHECK_FALSE(
        readParam(tatami.session.project().embroidery_objects.front(), "repeats").has_value());
    CHECK(paramSpecs("no_such_type").empty());
}

TEST_CASE("setting a parameter is one undo step and reads back in display units",
          "[desktop_v2][params]") {
    auto d = rectWithType("tatami");
    const auto depth = d.session.undoNames().size();
    REQUIRE(d.session.applyBatch(batchSetParam({d.id}, "tatami", "row_spacing_mm", 0.55), "Écart")
                .committed);
    CHECK(d.session.undoNames().size() == depth + 1);
    const auto& o = d.session.project().embroidery_objects.front();
    CHECK(*readParam(o, "row_spacing_mm") == Catch::Approx(0.55).margin(0.001));
    REQUIRE(d.session.applyBatch(batchSetParam({d.id}, "tatami", "angle_deg", 45.0), "Angle")
                .committed);
    CHECK(*readParam(d.session.project().embroidery_objects.front(), "angle_deg") ==
          Catch::Approx(45.0).margin(0.01));
    REQUIRE(d.session.applyBatch(batchSetParam({d.id}, "tatami", "underlay_edge", 1), "Sous-couche")
                .committed);
    CHECK(*readParam(d.session.project().embroidery_objects.front(), "underlay_edge") == 1.0);
    REQUIRE(d.session.undo());
    CHECK(*readParam(d.session.project().embroidery_objects.front(), "underlay_edge") == 0.0);
}

TEST_CASE("integers are rounded and out-of-range or unknown settings are refused",
          "[desktop_v2][params]") {
    auto d = rectWithType("running");
    REQUIRE(d.session.applyBatch(batchSetParam({d.id}, "running", "repeats", 2.4), "Passages")
                .committed);
    CHECK(*readParam(d.session.project().embroidery_objects.front(), "repeats") == 2.0);
    CHECK(batchSetParam({d.id}, "running", "repeats", 7).empty());
    CHECK(batchSetParam({d.id}, "running", "stitch_length_mm", 99).empty());
    CHECK(batchSetParam({d.id}, "running", "row_spacing_mm", 0.4)
              .empty()); // pas un réglage de ce type
    CHECK(batchSetParam({d.id}, "running", "repeats", std::nan("")).empty());
    CHECK(batchSetParam({}, "running", "repeats", 2).empty());
}

TEST_CASE("one change applies to every selected object in a single undo step",
          "[desktop_v2][params]") {
    Doc d;
    auto created = project_ops::create_empty_project({});
    REQUIRE(created.has_value());
    d.session.replaceProject(std::move(*created), true);
    const auto a =
        *createdObjectId(d.session.applyBatch(batchAddRect({-20, 0}, 10, 10), "R").report);
    const auto b =
        *createdObjectId(d.session.applyBatch(batchAddRect({20, 0}, 10, 10), "R").report);
    const auto depth = d.session.undoNames().size();
    REQUIRE(
        d.session.applyBatch(batchSetParam({a, b}, "tatami", "stitch_length_mm", 2.5), "Longueur")
            .committed);
    CHECK(d.session.undoNames().size() == depth + 1);
    for (const auto& o : d.session.project().embroidery_objects) {
        CHECK(*readParam(o, "stitch_length_mm") == Catch::Approx(2.5).margin(0.001));
    }
}
