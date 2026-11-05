// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "workflow_guide.hpp"

using namespace openstitch::desktop_v2;

namespace {
std::vector<StepState> states(const Guide& g) {
    std::vector<StepState> out;
    for (const auto& s : g.steps) {
        out.push_back(s.state);
    }
    return out;
}
using S = StepState;
} // namespace

TEST_CASE("without objects the guide asks to import", "[desktop_v2][guide]") {
    const auto g = buildGuide({});
    CHECK(g.steps.size() == 4);
    CHECK(states(g) == std::vector<S>{S::Current, S::Todo, S::Todo, S::Todo});
    CHECK(g.next == NextAction::OpenImage);
    CHECK_FALSE(g.nextLabel.empty());
    GuideInput empty;
    empty.documentOpen = true;
    CHECK(buildGuide(empty).headline.find("vide") != std::string::npos);
}

TEST_CASE("objects not yet analysed lead to the check step", "[desktop_v2][guide]") {
    GuideInput in;
    in.documentOpen = true;
    in.hasObjects = true;
    const auto g = buildGuide(in);
    CHECK(states(g) == std::vector<S>{S::Done, S::Current, S::Todo, S::Todo});
    CHECK(g.next == NextAction::Analyse);
}

TEST_CASE("analysis errors keep the guide on refine", "[desktop_v2][guide]") {
    GuideInput in;
    in.documentOpen = true;
    in.hasObjects = true;
    in.analysedCurrent = true;
    in.analysisErrors = 2;
    const auto g = buildGuide(in);
    CHECK(states(g) == std::vector<S>{S::Done, S::Current, S::Todo, S::Todo});
    CHECK(g.headline.find("2 erreur") != std::string::npos);
    CHECK(g.next == NextAction::Analyse);
}

TEST_CASE("a clean analysis proposes the export, then the guide is finished",
          "[desktop_v2][guide]") {
    GuideInput in;
    in.documentOpen = true;
    in.hasObjects = true;
    in.analysedCurrent = true;
    auto g = buildGuide(in);
    CHECK(states(g) == std::vector<S>{S::Done, S::Done, S::Done, S::Current});
    CHECK(g.next == NextAction::Export);
    in.exportedCurrent = true;
    g = buildGuide(in);
    CHECK(states(g) == std::vector<S>{S::Done, S::Done, S::Done, S::Done});
    CHECK(g.next == NextAction::None);
    CHECK(g.nextLabel.empty());
}
