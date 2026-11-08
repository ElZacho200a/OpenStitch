// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>
#include <filesystem>

#include "digitize_pipeline.hpp"
#include "fix_service.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;

namespace {

document::Project digitized() {
    DigitizeRequest request;
    request.image = std::filesystem::path(OPENSTITCH_FIXTURES_DIR) / "tentabrode.png";
    request.target_height_mm = 80.0;
    request.settings.max_colors = 4;
    auto result = run_digitize(request, TaskContext{});
    REQUIRE(result.has_value());
    return std::move(result->project);
}

std::size_t dstStitchCount(const document::Project& project) {
    auto sequence = stitch_generation::effective_sequence(project);
    REQUIRE(sequence.has_value());
    return sequence->commands.size();
}

} // namespace

TEST_CASE("fix plan carries the analysed revision and only applicable fixes", "[desktop_v2][fix]") {
    const auto snapshot = std::make_shared<const document::Project>(digitized());
    const auto plan = plan_fixes(snapshot, 7, TaskContext{});
    REQUIRE(plan.has_value());
    CHECK(plan->revision == 7);
    std::size_t safe = 0;
    for (const auto& p : plan->proposals) {
        CHECK(isApplicableFix(p.kind));
        CHECK_FALSE(p.label.empty());
        safe += p.safe ? 1 : 0;
    }
    CHECK(plan->safeCount == safe);
}

TEST_CASE("a fix batch is one undoable step and leaves everything else untouched",
          "[desktop_v2][fix]") {
    auto project = digitized();
    project.finishing.enabled = false;
    project.finishing.split_long_stitches = false;
    FixPlan plan;
    plan.revision = 1;
    FixProposal finishing;
    finishing.kind = stitch_analysis::FixKind::EnableFinishing;
    finishing.label = "Activer les finitions";
    finishing.safe = true;
    FixProposal split = finishing;
    split.kind = stitch_analysis::FixKind::SplitLongStitches;
    plan.proposals = {finishing, split, finishing};

    const auto before = project.finishing;
    const auto objectsBefore = project.embroidery_objects.size();
    auto build = build_fix_command(project, plan, {}, "Corriger tout ce qui est sûr");
    REQUIRE(build.command != nullptr);
    CHECK(build.applied == 1); // les finitions sont fusionnées en UNE commande
    build.command->apply(project);
    CHECK(project.finishing.enabled);
    CHECK(project.finishing.split_long_stitches);
    CHECK(project.embroidery_objects.size() == objectsBefore);
    build.command->revert(project);
    CHECK(project.finishing == before);
}

TEST_CASE("nothing applicable gives no command and counts the skipped fixes", "[desktop_v2][fix]") {
    auto project = digitized();
    FixPlan plan;
    FixProposal stale;
    stale.kind = stitch_analysis::FixKind::ClampStitchLength;
    stale.object = ObjectId{999999}; // objet disparu depuis l'analyse
    stale.safe = true;
    plan.proposals = {stale};
    const auto build = build_fix_command(project, plan, {}, "x");
    CHECK(build.command == nullptr);
    CHECK(build.applied == 0);
    CHECK(build.skipped == 1);
}

TEST_CASE("thread reduction previews the new count and applies as one reversible step",
          "[desktop_v2][fix]") {
    auto project = digitized();
    const auto snapshot = std::make_shared<const document::Project>(project);
    const auto preview = plan_thread_reduction(snapshot, 3, 2, TaskContext{});
    REQUIRE(preview.has_value());
    CHECK(preview->revision == 3);
    REQUIRE_FALSE(preview->plan.empty());
    CHECK(preview->plan.after <= 2);
    CHECK(preview->plan.before > preview->plan.after);

    const auto stitchesBefore = dstStitchCount(project);
    const auto colours = [&] {
        std::vector<std::array<std::uint8_t, 3>> v;
        for (const auto& o : project.embroidery_objects) {
            v.push_back(o.rgb);
        }
        return v;
    };
    const auto original = colours();
    auto command = build_thread_reduction_command(*preview, "Réduire à 2 fils");
    REQUIRE(command != nullptr);
    command->apply(project);
    CHECK(colours() != original);
    command->revert(project);
    CHECK(colours() == original);
    CHECK(dstStitchCount(project) == stitchesBefore);
}

TEST_CASE("thread reduction at or above the current count is an empty plan", "[desktop_v2][fix]") {
    const auto snapshot = std::make_shared<const document::Project>(digitized());
    const auto preview = plan_thread_reduction(snapshot, 1, 99, TaskContext{});
    REQUIRE(preview.has_value());
    CHECK(preview->plan.empty());
    CHECK(build_thread_reduction_command(*preview, "x") == nullptr);
}
