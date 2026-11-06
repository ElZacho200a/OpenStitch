// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "document_session.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "shape_batch.hpp"
#include "spool_rail.hpp"
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

TEST_CASE("spool rail lists one spool per object and counts thread changes", "[spool_rail]") {
    auto s = threeObjects();
    auto rail = spoolRail(s.project());
    REQUIRE(rail.spools.size() == 3);
    CHECK_FALSE(rail.spools.front().threadChangeBefore);
    int changes = 0;
    for (std::size_t i = 1; i < rail.spools.size(); ++i) {
        const bool differs = rail.spools[i].rgb != rail.spools[i - 1].rgb;
        CHECK(rail.spools[i].threadChangeBefore == differs);
        changes += differs ? 1 : 0;
    }
    CHECK(rail.threadChanges == changes);
    CHECK(rail.summary().find("3 bobines") == 0);
    CHECK(spoolRail(document::Project{}).summary() == "Aucune bobine");
}

TEST_CASE("moving a spool reorders sewing order in one undo step", "[spool_rail]") {
    auto s = threeObjects();
    const auto before = spoolRail(s.project());
    const auto firstId = before.spools.front().id;
    REQUIRE(moveSpool(s, firstId, 2).ok);
    const auto after = spoolRail(s.project());
    CHECK(after.spools[2].id == firstId);
    CHECK(after.spools[0].id == before.spools[1].id);
    CHECK_FALSE(moveSpool(s, firstId, 2).ok); // même position : refusé
    CHECK_FALSE(moveSpool(s, 987654321, 0).ok);
    REQUIRE(s.undo());
    CHECK(spoolRail(s.project()).spools.front().id == firstId);
}
