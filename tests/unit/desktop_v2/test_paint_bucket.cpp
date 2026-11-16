// SPDX-License-Identifier: Apache-2.0
#include "openstitch/project_ops/factory.hpp"
#include "paint_bucket.hpp"
#include "shape_batch.hpp"
#include <catch2/catch_test_macros.hpp>
using namespace openstitch;
using namespace openstitch::desktop_v2;
TEST_CASE("bucket fills only enclosed empty faces and commits one vector command",
          "[desktop_v2][bucket]") {
    DocumentSession session;
    auto empty = project_ops::create_empty_project({});
    REQUIRE(empty);
    session.replaceProject(std::move(*empty), true);
    for (const auto& batch : {batchAddRect({-12, 0}, 4, 28), batchAddRect({12, 0}, 4, 28),
                              batchAddRect({0, -12}, 28, 4), batchAddRect({0, 12}, 28, 4)})
        REQUIRE(session.applyBatch(batch, "Wall").committed);
    const auto revision = session.revision();
    const auto count = session.project().vector_objects.size();
    const auto depth = session.undoNames().size();
    auto a = previewBucket(session.project(), {});
    REQUIRE(a);
    auto b = previewBucket(session.project(), {});
    REQUIRE(b);
    CHECK(*a == *b);
    CHECK(session.revision() == revision);
    CHECK_FALSE(previewBucket(session.project(), {Micrometers{12000}, Micrometers{0}}));
    CHECK_FALSE(previewBucket(session.project(), {Micrometers{30000}, Micrometers{0}}));
    CHECK_FALSE(previewBucket(session.project(), {}, Micrometers{-1}));
    REQUIRE(commitBucket(session, revision, *a));
    CHECK(session.project().vector_objects.size() == count + 1);
    CHECK(session.project().embroidery_objects.size() == 4); // V1 creates a vector only
    CHECK(session.undoNames().size() == depth + 1);
    CHECK_FALSE(commitBucket(session, revision, *b));
    REQUIRE(session.undo());
    CHECK(session.project().vector_objects.size() == count);
}
