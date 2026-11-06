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

// Un projet numérisé, à jour, sans rien d'autre : la base des scénarios.
GuideInput digitized() {
    GuideInput in;
    in.documentOpen = true;
    in.hasObjects = true;
    in.hasSegmentation = true;
    return in;
}
} // namespace

TEST_CASE("without objects the guide asks to open an image", "[desktop_v2][guide]") {
    const auto g = buildGuide({});
    CHECK(g.steps.size() == 4);
    CHECK(states(g) == std::vector<S>{S::Current, S::Todo, S::Todo, S::Todo});
    CHECK(g.next == NextAction::OpenImage);
    CHECK_FALSE(g.nextLabel.empty());
    GuideInput empty;
    empty.documentOpen = true;
    CHECK(buildGuide(empty).headline.find("vide") != std::string::npos);
    CHECK(g.steps[0].id == "prepare");
    CHECK(g.steps[3].id == "produce");
}

TEST_CASE("digitized objects not yet checked lead to the check phase", "[desktop_v2][guide]") {
    const auto g = buildGuide(digitized());
    CHECK(states(g) == std::vector<S>{S::Done, S::Done, S::Current, S::Todo});
    CHECK(g.next == NextAction::Analyse);
    CHECK(g.currentIndex == 2);
}

TEST_CASE("analysis errors block the check and point to the errors", "[desktop_v2][guide]") {
    auto in = digitized();
    in.analysedEver = in.analysedCurrent = true;
    in.analysisErrors = 2;
    const auto g = buildGuide(in);
    CHECK(g.steps[2].state == S::Blocked);
    CHECK(g.headline.find("2 erreur") != std::string::npos);
    CHECK(g.next == NextAction::ShowDiagnostics);
    REQUIRE_FALSE(g.issues.empty());
    CHECK(g.issues.front().blocking);
    CHECK(g.issues.front().fix == NextAction::ShowDiagnostics);
}

TEST_CASE("a clean analysis is not a machine guarantee: the machine check comes next",
          "[desktop_v2][guide]") {
    auto in = digitized();
    in.analysedEver = in.analysedCurrent = true;
    auto g = buildGuide(in);
    CHECK(g.steps[2].state == S::Done);
    CHECK(g.steps[2].detail.find("cela ne remplace pas") != std::string::npos);
    CHECK(g.steps[3].state == S::Current); // Todo promu en cours : c'est la phase à traiter
    CHECK(g.next == NextAction::CheckMachine);
    CHECK(g.currentIndex == 3);

    // Contrôle machine passé : l'export est proposé ; exporté : tout est terminé.
    in.machineCheckedCurrent = true;
    g = buildGuide(in);
    CHECK(g.next == NextAction::Export);
    in.exportedEver = in.exportedCurrent = true;
    g = buildGuide(in);
    CHECK(states(g) == std::vector<S>{S::Done, S::Done, S::Done, S::Done});
    CHECK(g.next == NextAction::None);
    CHECK(g.nextLabel.empty());
}

TEST_CASE("machine blockers stop the export and send the user back to the check",
          "[desktop_v2][guide]") {
    auto in = digitized();
    in.analysedEver = in.analysedCurrent = true;
    in.machineCheckedCurrent = true;
    in.machineBlockers = 2;
    const auto g = buildGuide(in);
    CHECK(g.steps[3].state == S::Blocked);
    CHECK(g.next == NextAction::CheckMachine);
    CHECK(g.headline.find("2 problèmes") != std::string::npos);
    // Même exporté une fois, un blocage machine actuel empêche de parler d'une phase terminée.
    in.exportedEver = in.exportedCurrent = true;
    CHECK(buildGuide(in).steps[3].state == S::Blocked);
}

TEST_CASE("results become stale after a change instead of silently staying valid",
          "[desktop_v2][guide]") {
    auto in = digitized();
    in.analysedEver = true; // analyse faite, mais pas pour la révision courante
    in.exportedEver = true; // exporté, mais plus à jour
    const auto g = buildGuide(in);
    CHECK(g.steps[2].state == S::Stale);
    CHECK(g.steps[3].state == S::Stale);
    CHECK(g.next == NextAction::Analyse); // d'abord relancer le contrôle
    CHECK(g.nextLabel == "Relancer le contrôle");
    bool staleIssue = false;
    for (const auto& i : g.issues) {
        staleIssue = staleIssue || (!i.blocking && i.fix == NextAction::Analyse);
    }
    CHECK(staleIssue);
}

TEST_CASE("a corrected segmentation makes the objects obsolete and offers to regenerate",
          "[desktop_v2][guide]") {
    auto in = digitized();
    in.analysedEver = in.analysedCurrent = true;
    in.segmentationDirty = true;
    in.handEdits = true;
    const auto g = buildGuide(in);
    CHECK(g.steps[1].state == S::Stale);
    CHECK(g.steps[1].detail.find("retouches manuelles") != std::string::npos);
    CHECK(g.next == NextAction::Regenerate);
    REQUIRE_FALSE(g.issues.empty());
    CHECK(g.issues.front().blocking);
    CHECK(g.currentIndex == 1);
}

TEST_CASE("warnings are not blocking and the optional check can be skipped explicitly",
          "[desktop_v2][guide]") {
    auto in = digitized();
    in.analysedEver = in.analysedCurrent = true;
    in.analysisWarnings = 3;
    auto g = buildGuide(in);
    CHECK(g.steps[2].state == S::Warning);
    CHECK(g.next == NextAction::CheckMachine); // un avertissement ne bloque pas la suite
    CHECK(stepStateLabel(S::Warning).find("avertissements") != std::string::npos);

    GuideInput skipped = digitized(); // jamais analysé, mais ignoré à la demande
    skipped.checkSkipped = true;
    g = buildGuide(skipped);
    CHECK(g.steps[2].state == S::Skipped);
    CHECK(g.next == NextAction::CheckMachine);
    CHECK(g.currentIndex == 3);
}

TEST_CASE("every phase state has a label and the guide always offers a relevant action",
          "[desktop_v2][guide]") {
    for (const auto s :
         {S::Todo, S::Current, S::Done, S::Stale, S::Blocked, S::Warning, S::Skipped}) {
        CHECK_FALSE(stepStateLabel(s).empty());
    }
    // Avant la fin, il y a toujours une action ; seul l'état « exporté à jour » n'en a pas.
    for (int mask = 0; mask < 512; ++mask) {
        GuideInput in;
        in.documentOpen = true;
        in.hasObjects = true;
        in.hasSegmentation = true;
        in.analysedEver = (mask & 1) != 0;
        in.analysedCurrent = in.analysedEver && (mask & 2) != 0;
        in.analysisErrors = (mask & 4) != 0 && in.analysedCurrent ? 1 : 0;
        in.machineCheckedCurrent = (mask & 8) != 0;
        in.machineBlockers = (mask & 16) != 0 && in.machineCheckedCurrent ? 1 : 0;
        in.exportedEver = (mask & 32) != 0;
        in.exportedCurrent = in.exportedEver && (mask & 64) != 0;
        in.segmentationDirty = (mask & 128) != 0;
        in.checkSkipped = (mask & 256) != 0;
        const auto g = buildGuide(in);
        INFO("masque " << mask);
        REQUIRE(g.steps.size() == 4);
        const bool done = g.steps[3].state == S::Done && !in.segmentationDirty &&
                          g.steps[1].state == S::Done && g.steps[2].state != S::Blocked;
        if (!done && g.next == NextAction::None) {
            // Seul cas admis sans action : tout est passé (terminé, avertissement ou ignoré).
            for (const auto& s : g.steps) {
                CHECK((s.state == S::Done || s.state == S::Warning || s.state == S::Skipped));
            }
        }
        CHECK(g.currentIndex >= 0);
        CHECK(g.currentIndex <= 3);
    }
}
