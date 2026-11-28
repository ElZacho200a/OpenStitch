// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "openstitch/project_ops/factory.hpp"
#include "openstitch/project_ops/inspect.hpp"
#include "openstitch/scripting/selection_script.hpp"

namespace fs = std::filesystem;
namespace po = openstitch::project_ops;
namespace sc = openstitch::scripting;
using namespace openstitch;

namespace {

document::Project digitized() {
    auto created = po::create_empty_project({});
    REQUIRE(created);
    document::Project project = std::move(*created);
    const auto report = po::digitize_image_into(
        project, fs::path(OPENSTITCH_FIXTURES_DIR) / "cli" / "logo_small.png", {});
    REQUIRE(report);
    REQUIRE(project.embroidery_objects.size() >= 2);
    return project;
}

std::vector<ObjectId> all_ids(const document::Project& project) {
    std::vector<ObjectId> ids;
    for (const auto& e : project.embroidery_objects) {
        ids.push_back(e.id);
    }
    return ids;
}

int row_spacing_um(const document::EmbroideryObject& e) {
    const auto* tatami = std::get_if<document::TatamiParams>(&e.params);
    REQUIRE(tatami != nullptr);
    return tatami->row_spacing.value;
}

bool contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

} // namespace

TEST_CASE("embedded python starts and reports its version", "[scripting]") {
    REQUIRE(sc::ensure_python());
    CHECK(sc::python_version().starts_with("3."));
}

TEST_CASE("script sets a parameter from object features and applies it", "[scripting]") {
    auto project = digitized();
    const auto ids = all_ids(project);

    // Les objets d'aire supérieure à la médiane reçoivent 0,55 mm, les autres 0,35 mm.
    const std::string script = R"(
areas = sorted(o.area_mm2 for o in selection)
median = areas[len(areas) // 2]
for o in selection:
    o.set(row_spacing_mm=0.55 if o.area_mm2 >= median else 0.35)
print("objets :", len(selection))
)";
    auto applied = sc::apply_selection_script(project, ids, script);
    REQUIRE(applied);
    CHECK(applied->report.ok);
    CHECK(applied->report.applied);
    CHECK(applied->script.operation_count == ids.size());
    CHECK(contains(applied->script.output, "objets : " + std::to_string(ids.size())));
    REQUIRE_FALSE(applied->script.changes.empty());
    CHECK(applied->script.changes.front().key == "row_spacing_mm");
    for (const auto& e : project.embroidery_objects) {
        const int spacing = row_spacing_um(e);
        CHECK((spacing == 550 || spacing == 350));
    }
}

TEST_CASE("script reads color, position and parameters by name", "[scripting]") {
    auto project = digitized();
    const auto ids = all_ids(project);
    const std::string script = R"(
for o in selection:
    assert isinstance(o.color.hue, float)
    assert o.color == o.color.hex.lower()
    assert o.width_mm >= 0 and o.height_mm >= 0
    assert abs(o.x - o.center.x) < 1e-9
    assert o.row_spacing_mm == o.params["row_spacing_mm"]
    o.angle_deg = 30 if o.x < canvas["width_mm"] / 2 else 60
)";
    auto preview = sc::run_selection_script(project, ids, script);
    REQUIRE(preview);
    CHECK(preview->operation_count >= 1);
    CHECK(contains(preview->batch_json, "\"angle_deg\""));
}

TEST_CASE("script result is deterministic", "[scripting]") {
    const auto project = digitized();
    const auto ids = all_ids(project);
    const std::string script = R"(
tags = {o.color.hex for o in selection}
for i, o in enumerate(selection):
    o.set(row_spacing_mm=0.3 + 0.01 * (i % 5), angle_deg=15 * len(tags))
)";
    const auto a = sc::run_selection_script(project, ids, script);
    const auto b = sc::run_selection_script(project, ids, script);
    REQUIRE(a);
    REQUIRE(b);
    CHECK(a->batch_json == b->batch_json);
}

TEST_CASE("script cannot change an object outside the selection", "[scripting]") {
    const auto project = digitized();
    const std::vector<ObjectId> first{project.embroidery_objects.front().id};
    const auto result =
        sc::run_selection_script(project, first, "objects[-1].set(row_spacing_mm=0.5)\n");
    REQUIRE_FALSE(result);
    CHECK(result.error().code == sc::code::kScriptError);
    CHECK(contains(result.error().message, "PermissionError"));
    CHECK(contains(result.error().message, "ligne 1"));
}

TEST_CASE("script errors name the line", "[scripting]") {
    const auto project = digitized();
    const auto ids = all_ids(project);

    const auto syntax = sc::run_selection_script(project, ids, "for o in selection\n  pass\n");
    REQUIRE_FALSE(syntax);
    CHECK(syntax.error().code == sc::code::kScriptError);
    CHECK(contains(syntax.error().message, "ligne 1"));

    const auto runtime =
        sc::run_selection_script(project, ids, "x = 1\nprint('avant')\ny = selection[0].nope\n");
    REQUIRE_FALSE(runtime);
    CHECK(contains(runtime.error().message, "ligne 3"));
    CHECK(contains(runtime.error().message, "AttributeError"));
    CHECK(contains(runtime.error().detail, "avant"));
}

TEST_CASE("endless script is interrupted after the timeout", "[scripting]") {
    const auto project = digitized();
    const auto ids = all_ids(project);
    sc::ScriptOptions options;
    options.timeout = std::chrono::milliseconds{200};

    const auto start = std::chrono::steady_clock::now();
    const auto loop = sc::run_selection_script(project, ids, "while True:\n    pass\n", options);
    REQUIRE_FALSE(loop);
    CHECK(loop.error().code == sc::code::kTimeout);

    // `except Exception` ne rattrape pas l'interruption.
    const auto stubborn = sc::run_selection_script(
        project, ids, "while True:\n    try:\n        pass\n    except Exception:\n        pass\n",
        options);
    REQUIRE_FALSE(stubborn);
    CHECK(stubborn.error().code == sc::code::kTimeout);
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds{10});

    // L'interpréteur reste utilisable après une interruption.
    CHECK(sc::run_selection_script(project, ids, "pass\n"));
}

TEST_CASE("unknown parameter is refused and leaves the project unchanged", "[scripting]") {
    auto project = digitized();
    const auto ids = all_ids(project);
    const int before = row_spacing_um(project.embroidery_objects.front());
    const auto applied = sc::apply_selection_script(
        project, ids, "for o in selection:\n    o.set(row_spacing_mm=0.5, densite_magique=3)\n");
    REQUIRE_FALSE(applied);
    CHECK(applied.error().code == sc::code::kRefused);
    CHECK(row_spacing_um(project.embroidery_objects.front()) == before);
}

TEST_CASE("dry run previews without modifying the project", "[scripting]") {
    auto project = digitized();
    const auto ids = all_ids(project);
    const int before = row_spacing_um(project.embroidery_objects.front());
    const auto preview = sc::apply_selection_script(
        project, ids, "for o in selection:\n    o.set(row_spacing_mm=0.6)\n", {}, true);
    REQUIRE(preview);
    CHECK(preview->report.ok);
    CHECK_FALSE(preview->report.applied);
    CHECK(row_spacing_um(project.embroidery_objects.front()) == before);
}

TEST_CASE("set_color produces a set_color operation", "[scripting]") {
    auto project = digitized();
    const auto ids = all_ids(project);
    const auto applied = sc::apply_selection_script(
        project, ids, "for o in selection:\n    o.set_color('#12AB34')\n");
    REQUIRE(applied);
    CHECK(po::rgb_to_hex(project.embroidery_objects.front().rgb) == "#12AB34");
}

TEST_CASE("empty selection and unknown ids are rejected", "[scripting]") {
    const auto project = digitized();
    const auto none = sc::run_selection_script(project, {}, "pass\n");
    REQUIRE_FALSE(none);
    CHECK(none.error().code == sc::code::kNoSelection);
    const auto unknown = sc::run_selection_script(project, {ObjectId{999999}}, "pass\n");
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().code == sc::code::kNoSelection);
}

TEST_CASE("scripts run from another thread", "[scripting]") {
    const auto project = digitized();
    const auto ids = all_ids(project);
    bool ok = false;
    std::thread worker([&] {
        const auto result = sc::run_selection_script(
            project, ids, "for o in selection:\n    o.set(angle_deg=10)\n");
        ok = result.has_value() && result->operation_count >= 1;
    });
    worker.join();
    CHECK(ok);
}
