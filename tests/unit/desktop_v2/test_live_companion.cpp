// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include "document_session.hpp"
#include "live_companion.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "shape_batch.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
using json = nlohmann::json;

namespace {
struct Fixture {
    DocumentSession session;
    std::uint64_t id{0};
    bool answer{true};
    int asked{0};
    int notified{0};
    LiveCompanion companion;

    Fixture()
        : companion(
              session,
              [this](const mcp::ApprovalRequest&) {
                  ++asked;
                  return answer;
              },
              [] { return std::string("demo.osp"); }) {
        auto created = project_ops::create_empty_project({});
        REQUIRE(created.has_value());
        session.replaceProject(std::move(*created), true);
        REQUIRE(session.applyBatch(batchAddRect({0, 0}, 20, 10), "Rectangle").committed);
        id = session.project().embroidery_objects.front().id.value;
        (void)session.subscribe([this](ChangeKind) { ++notified; });
    }

    [[nodiscard]] std::string recolor() const {
        return "{\"operations\":[{\"op\":\"set_color\",\"object\":" + std::to_string(id) +
               ",\"rgb\":\"#FF0000\"}],\"dry_run\":false}";
    }
    [[nodiscard]] std::array<std::uint8_t, 3> colour() const {
        return session.project().embroidery_objects.front().rgb;
    }
};
} // namespace

TEST_CASE("an agent change goes through the user's own undo history after approval",
          "[desktop_v2][companion]") {
    Fixture f;
    const auto before = f.colour();
    const auto depth = f.session.undoNames().size();
    const auto revision = f.session.revision();

    const json out = json::parse(f.companion.service().call("project_apply", f.recolor()));
    REQUIRE_FALSE(out["isError"].get<bool>());
    CHECK(f.asked == 1);
    CHECK(f.colour() == std::array<std::uint8_t, 3>{255, 0, 0});
    CHECK(f.session.undoNames().size() == depth + 1); // même historique, une seule étape
    CHECK(f.session.revision() > revision);
    CHECK(f.notified >= 1); // l'interface est prévenue comme pour un Annuler
    CHECK(f.session.dirty());

    REQUIRE(f.companion.events().size() == 1);
    CHECK(f.companion.events().front().outcome == CompanionEvent::Outcome::Applied);
    CHECK(f.companion.appliedCount() == 1);

    REQUIRE(f.session.undo()); // l'utilisateur annule la modification de l'agent
    CHECK(f.colour() == before);
}

TEST_CASE("a refused agent change leaves the document untouched", "[desktop_v2][companion]") {
    Fixture f;
    f.answer = false;
    const auto before = f.colour();
    const auto depth = f.session.undoNames().size();
    const auto revision = f.session.revision();
    const json out = json::parse(f.companion.service().call("project_apply", f.recolor()));
    CHECK(out["isError"].get<bool>());
    CHECK(f.colour() == before);
    CHECK(f.session.undoNames().size() == depth);
    CHECK(f.session.revision() == revision);
    CHECK(f.companion.refusedCount() == 1);
    CHECK(f.companion.appliedCount() == 0);
}

TEST_CASE("a dry run never asks for approval nor changes the document", "[desktop_v2][companion]") {
    Fixture f;
    const auto revision = f.session.revision();
    std::string dry = f.recolor();
    dry.replace(dry.find("false"), 5, "true");
    const json out = json::parse(f.companion.service().call("project_apply", dry));
    CHECK_FALSE(out["isError"].get<bool>());
    CHECK(f.asked == 0);
    CHECK(f.session.revision() == revision);
    CHECK(f.companion.events().empty());
}

TEST_CASE("replacing the document closes the agent's session", "[desktop_v2][companion]") {
    Fixture f;
    const auto old = f.session.liveBridge().session_id();
    auto fresh = project_ops::create_empty_project({});
    REQUIRE(fresh.has_value());
    f.session.replaceProject(std::move(*fresh), true);
    CHECK(f.session.liveBridge().session_id() != old);
    // Un lot calculé sur l'ancien document ne peut plus être appliqué : l'identifiant est périmé.
    const auto stale = f.session.liveBridge().commit(old, f.session.liveBridge().revision(),
                                                     f.recolor(), "obsolète", true);
    CHECK_FALSE(stale.ok);
    CHECK(f.companion.appliedCount() == 0);
}

TEST_CASE("a closed companion refuses every agent call", "[desktop_v2][companion]") {
    Fixture f;
    f.companion.close();
    const json out = json::parse(f.companion.service().call("project_apply", f.recolor()));
    CHECK(out["isError"].get<bool>());
    CHECK(f.asked == 0);
}
