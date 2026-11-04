// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>
#include <string>

#include "tool_registry.hpp"

// Le fichier d'en-tête historique ne dépend pas de Qt (simple enum) : il sert de référence de
// parité, donc un outil ajouté à l'ancien bureau sans équivalent V2 fait échouer ce test.
#include OPENSTITCH_LEGACY_TOOLS_HEADER

using namespace openstitch::desktop_v2;
namespace legacy = openstitch::desktop;

namespace {
const char* legacyName(legacy::Tool t) {
    switch (t) {
    case legacy::Tool::Select:
        return "Select";
    case legacy::Tool::Pan:
        return "Pan";
    case legacy::Tool::Rect:
        return "Rect";
    case legacy::Tool::DrawRectangle:
        return "DrawRectangle";
    case legacy::Tool::DrawEllipse:
        return "DrawEllipse";
    case legacy::Tool::DrawPolygon:
        return "DrawPolygon";
    case legacy::Tool::DrawPolygonRegular:
        return "DrawPolygonRegular";
    case legacy::Tool::DrawBezier:
        return "DrawBezier";
    case legacy::Tool::DrawFreeform:
        return "DrawFreeform";
    case legacy::Tool::DrawSatinColumn:
        return "DrawSatinColumn";
    case legacy::Tool::DrawDirectionGuide:
        return "DrawDirectionGuide";
    case legacy::Tool::DrawBreakLine:
        return "DrawBreakLine";
    case legacy::Tool::Cut:
        return "Cut";
    case legacy::Tool::Text:
        return "Text";
    case legacy::Tool::Measure:
        return "Measure";
    case legacy::Tool::Eyedropper:
        return "Eyedropper";
    case legacy::Tool::Brush:
        return "Brush";
    case legacy::Tool::Bucket:
        return "Bucket";
    case legacy::Tool::ClickToStitch:
        return "ClickToStitch";
    case legacy::Tool::DetailBrush:
        return "DetailBrush";
    }
    return "";
}
} // namespace

TEST_CASE("builtin tool registry has no id or shortcut conflict", "[desktop_v2][tools]") {
    const auto registry = ToolRegistry::builtin();
    const auto conflicts = registry.conflicts();
    for (const auto& c : conflicts) {
        UNSCOPED_INFO(c);
    }
    CHECK(conflicts.empty());
}

TEST_CASE("every legacy canvas tool has a V2 equivalent", "[desktop_v2][tools][parity]") {
    const auto registry = ToolRegistry::builtin();
    // Les 20 enumerateurs historiques, de Select à DetailBrush (numérotation contiguë).
    for (int i = static_cast<int>(legacy::Tool::Select);
         i <= static_cast<int>(legacy::Tool::DetailBrush); ++i) {
        const std::string name = legacyName(static_cast<legacy::Tool>(i));
        INFO("outil historique " << name);
        REQUIRE_FALSE(name.empty());
        CHECK(registry.findByLegacy(name) != nullptr);
    }
    CHECK(registry.tools().size() == 20);
}

TEST_CASE("workspaces always expose selection and pan", "[desktop_v2][tools]") {
    const auto registry = ToolRegistry::builtin();
    for (const auto w :
         {Workspace::Design, Workspace::Digitize, Workspace::Optimize, Workspace::Produce}) {
        const auto tools = registry.forWorkspace(w);
        bool select = false;
        bool pan = false;
        for (const auto* t : tools) {
            select = select || t->id == "select";
            pan = pan || t->id == "pan";
        }
        INFO(workspaceId(w));
        CHECK(select);
        CHECK(pan);
    }
}

TEST_CASE("registry detects a duplicated shortcut", "[desktop_v2][tools]") {
    ToolRegistry r;
    r.add({"a", "A", "X", kAllWorkspaces, Gesture::None, ""});
    r.add({"b", "B", "X", workspaceBit(Workspace::Design), Gesture::None, ""});
    CHECK(r.conflicts().size() == 1);
    ToolRegistry disjoint;
    disjoint.add({"a", "A", "X", workspaceBit(Workspace::Design), Gesture::None, ""});
    disjoint.add({"b", "B", "X", workspaceBit(Workspace::Produce), Gesture::None, ""});
    CHECK(disjoint.conflicts().empty());
}

#include "object_labels.hpp"

TEST_CASE("stitch type ids follow the StitchParams variant order", "[desktop_v2][labels]") {
    openstitch::document::EmbroideryObject o;
    o.params = openstitch::document::TatamiParams{};
    CHECK(stitchTypeId(o) == "tatami");
    o.params = openstitch::document::SatinParams{};
    CHECK(stitchTypeId(o) == "satin");
    o.params = openstitch::document::AppliqueParams{};
    CHECK(stitchTypeId(o) == "applique");
    o.params = openstitch::document::RunningStitchParams{};
    CHECK(stitchTypeId(o) == "running");
}
