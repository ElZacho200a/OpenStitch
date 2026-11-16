// SPDX-License-Identifier: Apache-2.0
#include "openstitch/formats/dst.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "shape_batch.hpp"
#include "warp_brush.hpp"
#include <catch2/catch_test_macros.hpp>
#include <limits>
using namespace openstitch;
using namespace openstitch::desktop_v2;
TEST_CASE("warp validates exact bounds and produces deterministic undoable contours",
          "[desktop_v2][warp]") {
    CHECK(validateWarpOptions({0.5, 0.05}));
    CHECK(validateWarpOptions({60, 1}));
    CHECK_FALSE(validateWarpOptions({0.49, 1}));
    CHECK_FALSE(validateWarpOptions({60.1, 1}));
    CHECK_FALSE(validateWarpOptions({5, std::numeric_limits<double>::quiet_NaN()}));
    DocumentSession session;
    auto empty = project_ops::create_empty_project({});
    REQUIRE(empty);
    session.replaceProject(std::move(*empty), true);
    REQUIRE(session.applyBatch(batchAddRect({0, 0}, 20, 10), "Fixture").committed);
    const auto before = session.project();
    const auto id = before.embroidery_objects.front().id;
    const auto source = before.embroidery_objects.front().source_vector;
    const auto revision = session.revision();
    const std::vector<WarpTouch> touches{{Vec2um{Micrometers{0}, Micrometers{5000}}, false},
                                         {Vec2um{Micrometers{0}, Micrometers{7000}}, false}};
    auto a = previewWarp(before, id, {}, touches, true);
    REQUIRE(a);
    auto b = previewWarp(before, id, {}, touches, true);
    REQUIRE(b);
    CHECK(a->paths == b->paths);
    CHECK(a->paths != before.findObject(source)->paths);
    CHECK(session.revision() == revision);
    auto dst = [](const document::Project& p) {
        auto seq = stitch_generation::effective_sequence(p);
        REQUIRE(seq);
        auto bytes = formats::encode_dst(*seq);
        REQUIRE(bytes);
        return *bytes;
    };
    const auto initial = dst(before);
    const auto depth = session.undoNames().size();
    session.execute(std::move(a->command));
    CHECK(session.undoNames().size() == depth + 1);
    auto other = before;
    b->command->apply(other);
    CHECK(dst(session.project()) == dst(other));
    REQUIRE(session.undo());
    CHECK(dst(session.project()) == initial);
    CHECK(session.project().findObject(source)->paths == before.findObject(source)->paths);
    CHECK_FALSE(previewWarp(before, ObjectId{999999}, {}, touches, true));
    CHECK_FALSE(previewWarp(before, id, {}, {}, true));
    const std::vector<WarpTouch> outside{{Vec2um{Micrometers{1000001}, Micrometers{0}}, false}};
    CHECK_FALSE(previewWarp(before, id, {}, outside, false));
}
