// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

#include "document_session.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "openstitch/stitch/sequence.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "path_optimizer.hpp"
#include "shape_batch.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;

namespace {

// Quatre carrés alignés, rouge / bleu / rouge / bleu : trois changements de fil à l'origine.
// Rouge en 0 et 30, bleu en 60 et 90, mais cousus dans l'ordre 0, 60, 30, 90.
constexpr double kX[4] = {0.0, 60.0, 30.0, 90.0};

struct Fixture {
    DocumentSession session;
    Fixture() {
        auto created = project_ops::create_empty_project({});
        REQUIRE(created.has_value());
        session.replaceProject(std::move(*created), true);
        for (int i = 0; i < 4; ++i) {
            REQUIRE(session.applyBatch(batchAddRect({kX[i], 0}, 10, 10), "Carré").committed);
        }
        const char* colours[] = {"#FF0000", "#0000FF", "#FF0000", "#0000FF"};
        for (int i = 0; i < 4; ++i) {
            const auto id =
                session.project().embroidery_objects[static_cast<std::size_t>(i)].id.value;
            const std::string batch = "{\"schema\":\"openstitch.batch/"
                                      "1\",\"operations\":[{\"op\":\"set_color\",\"object\":" +
                                      std::to_string(id) + ",\"rgb\":\"" + colours[i] + "\"}]}";
            REQUIRE(session.applyBatch(batch, "Couleur").committed);
        }
    }

    [[nodiscard]] std::size_t realColorChanges() const {
        auto sequence = stitch_generation::effective_sequence(session.project());
        REQUIRE(sequence.has_value());
        return stitch::compute_stats(*sequence).color_changes;
    }
};

} // namespace

TEST_CASE("grouping colours is proposed with measured before and after values",
          "[desktop_v2][path]") {
    Fixture f;
    TaskContext task;
    const auto revision = f.session.revision();
    const auto proposal =
        proposeOrder(f.session.snapshot(), revision, PathStrategy::ByColor, false, task);
    REQUIRE(proposal.has_value());
    CHECK(proposal->changes);
    CHECK(proposal->before.colorChanges == f.realColorChanges());
    CHECK(proposal->after.colorChanges < proposal->before.colorChanges);
    CHECK(proposal->improves());
    CHECK(proposal->summary().size() == 5);
    CHECK(f.session.revision() == revision); // proposer ne modifie jamais le document
}

TEST_CASE("accepting a proposal is one undo step and matches what was measured",
          "[desktop_v2][path]") {
    Fixture f;
    TaskContext task;
    const auto proposal = proposeOrder(f.session.snapshot(), f.session.revision(),
                                       PathStrategy::ByColor, false, task);
    REQUIRE(proposal.has_value());
    std::vector<std::uint64_t> before;
    for (const auto& o : f.session.project().embroidery_objects) {
        before.push_back(o.id.value);
    }

    const auto depth = f.session.undoNames().size();
    REQUIRE(applyProposal(f.session, *proposal).ok);
    CHECK(f.session.undoNames().size() == depth + 1);
    // La promesse affichée est tenue : la séquence réelle a bien ce nombre de changements de fil.
    CHECK(f.realColorChanges() == proposal->after.colorChanges);

    REQUIRE(f.session.undo());
    std::vector<std::uint64_t> restored;
    for (const auto& o : f.session.project().embroidery_objects) {
        restored.push_back(o.id.value);
    }
    CHECK(restored == before);
    CHECK(f.realColorChanges() == proposal->before.colorChanges);
}

TEST_CASE("a proposal computed for an older revision is refused", "[desktop_v2][path]") {
    Fixture f;
    TaskContext task;
    const auto proposal = proposeOrder(f.session.snapshot(), f.session.revision(),
                                       PathStrategy::ByColor, false, task);
    REQUIRE(proposal.has_value());
    REQUIRE(f.session.applyBatch(batchAddRect({200, 0}, 5, 5), "Autre").committed);
    const auto depth = f.session.undoNames().size();
    const auto r = applyProposal(f.session, *proposal);
    CHECK_FALSE(r.ok);
    CHECK(f.session.undoNames().size() == depth);
}

TEST_CASE("an already optimal order proposes nothing", "[desktop_v2][path]") {
    Fixture f;
    TaskContext task;
    const auto first = proposeOrder(f.session.snapshot(), f.session.revision(),
                                    PathStrategy::ByColor, false, task);
    REQUIRE(first.has_value());
    REQUIRE(applyProposal(f.session, *first).ok);
    const auto again = proposeOrder(f.session.snapshot(), f.session.revision(),
                                    PathStrategy::ByColor, false, task);
    REQUIRE(again.has_value());
    CHECK_FALSE(again->changes);
    CHECK_FALSE(again->improves());
    CHECK_FALSE(applyProposal(f.session, *again).ok);
}

TEST_CASE("fewer than two objects cannot be reordered", "[desktop_v2][path]") {
    DocumentSession session;
    auto created = project_ops::create_empty_project({});
    REQUIRE(created.has_value());
    session.replaceProject(std::move(*created), true);
    REQUIRE(session.applyBatch(batchAddRect({0, 0}, 10, 10), "Carré").committed);
    TaskContext task;
    CHECK_FALSE(
        proposeOrder(session.snapshot(), session.revision(), PathStrategy::ByColor, false, task)
            .has_value());
}

TEST_CASE("every strategy yields a deterministic order", "[desktop_v2][path]") {
    Fixture f;
    TaskContext task;
    for (const auto strategy : {PathStrategy::ByColor, PathStrategy::ByProximity,
                                PathStrategy::ColorThenProximity, PathStrategy::Layered}) {
        const auto a =
            proposeOrder(f.session.snapshot(), f.session.revision(), strategy, true, task);
        const auto b =
            proposeOrder(f.session.snapshot(), f.session.revision(), strategy, true, task);
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        CHECK(a->order == b->order);
        CHECK(a->order.size() == 4);
        CHECK_FALSE(pathStrategyLabel(strategy).empty());
    }
}

TEST_CASE("a proposal that trades travel for fewer colour changes is not called an improvement",
          "[desktop_v2][path]") {
    DocumentSession session;
    auto created = project_ops::create_empty_project({});
    REQUIRE(created.has_value());
    session.replaceProject(std::move(*created), true);
    // Ordre actuel 0, 30, 60, 90 en rouge / bleu / rouge / bleu : peu de déplacements, 3
    // changements.
    const char* colours[] = {"#FF0000", "#0000FF", "#FF0000", "#0000FF"};
    for (int i = 0; i < 4; ++i) {
        REQUIRE(session.applyBatch(batchAddRect({i * 30.0, 0}, 10, 10), "Carré").committed);
    }
    for (int i = 0; i < 4; ++i) {
        const auto id = session.project().embroidery_objects[static_cast<std::size_t>(i)].id.value;
        const std::string batch =
            "{\"schema\":\"openstitch.batch/1\",\"operations\":[{\"op\":\"set_color\","
            "\"object\":" +
            std::to_string(id) + ",\"rgb\":\"" + colours[i] + "\"}]}";
        REQUIRE(session.applyBatch(batch, "Couleur").committed);
    }
    TaskContext task;
    const auto proposal =
        proposeOrder(session.snapshot(), session.revision(), PathStrategy::ByColor, false, task);
    REQUIRE(proposal.has_value());
    CHECK(proposal->after.colorChanges < proposal->before.colorChanges);
    CHECK(proposal->after.jumpLengthMm > proposal->before.jumpLengthMm);
    CHECK_FALSE(proposal->improves()); // l'utilisateur voit le compromis et décide
}
