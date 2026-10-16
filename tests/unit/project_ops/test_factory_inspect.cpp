// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "openstitch/formats/dst.hpp"
#include "openstitch/project_io/machine_file.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "openstitch/project_ops/inspect.hpp"
#include "openstitch/project_ops/project_file.hpp"
#include "openstitch/project_ops/report.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

using namespace openstitch;
namespace po = openstitch::project_ops;
namespace fs = std::filesystem;

namespace {

fs::path logo() {
    return fs::path(OPENSTITCH_FIXTURES_DIR) / "cli" / "logo_small.png";
}

fs::path temp_dir(const std::string& name) {
    const auto dir = fs::temp_directory_path() / ("openstitch_project_ops_" + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

document::Project digitized() {
    auto created = po::create_empty_project({});
    REQUIRE(created);
    document::Project project = std::move(*created);
    const auto report = po::digitize_image_into(project, logo(), {});
    REQUIRE(report);
    REQUIRE(report->embroidery_objects > 0);
    return project;
}

bool contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

} // namespace

TEST_CASE("factory creates an empty project and rejects an invalid frame", "[project_ops]") {
    po::NewProjectOptions options;
    options.width_mm = 120.0;
    options.height_mm = 80.0;
    options.margin_mm = 5.0;
    const auto project = po::create_empty_project(options);
    REQUIRE(project);
    CHECK(project->canvas.width.value == 120'000);
    CHECK(project->canvas.height.value == 80'000);
    CHECK(project->canvas.margin.value == 5'000);
    CHECK(project->embroidery_objects.empty());

    options.width_mm = 0.0;
    CHECK_FALSE(po::create_empty_project(options));
    options.width_mm = 100.0;
    options.margin_mm = 60.0; // plus de la moitie du cadre
    CHECK_FALSE(po::create_empty_project(options));
    options.margin_mm = 0.0;
    options.lock = "inconnu";
    CHECK_FALSE(po::create_empty_project(options));
}

TEST_CASE("digitize builds real editable objects from a repository image", "[project_ops]") {
    const auto project = digitized();
    CHECK(project.hasImage());
    CHECK(project.segmentation.has_value());
    CHECK(project.vector_objects.size() >= project.embroidery_objects.size());
    const auto seq = stitch_generation::effective_sequence(project);
    REQUIRE(seq);
    CHECK(stitch::compute_stats(*seq).stitches > 0);
}

TEST_CASE("digitize refuses a project that already has objects", "[project_ops]") {
    auto project = digitized();
    const auto again = po::digitize_image_into(project, logo(), {});
    REQUIRE_FALSE(again);
    CHECK(contains(again.error().message, "contient déjà"));
}

TEST_CASE("digitize reports an unreadable image without touching the project", "[project_ops]") {
    auto created = po::create_empty_project({});
    REQUIRE(created);
    const auto report = po::digitize_image_into(*created, "pas_une_image.png", {});
    REQUIRE_FALSE(report);
    CHECK(created->embroidery_objects.empty());
}

TEST_CASE("inspect json is stable, documented and deterministic", "[project_ops]") {
    const auto project = digitized();
    const auto a = po::inspect_project_json(project);
    const auto b = po::inspect_project_json(project);
    REQUIRE(a);
    REQUIRE(b);
    CHECK(*a == *b);
    CHECK(contains(*a, "\"schema\": \"openstitch.project-inspect/1\""));
    CHECK(contains(*a, "\"embroidery_objects\""));
    CHECK(contains(*a, "\"row_spacing_mm\""));
    CHECK(contains(*a, "\"first_command_index\""));

    const auto light = po::inspect_project_json(project, {.with_stitches = false});
    REQUIRE(light);
    CHECK_FALSE(contains(*light, "first_command_index"));

    const ObjectId id = project.embroidery_objects.front().id;
    const auto one = po::inspect_object_json(project, id, {.with_geometry = true});
    REQUIRE(one);
    CHECK(contains(*one, "openstitch.object-inspect/1"));
    CHECK(contains(*one, "nodes_mm"));
    CHECK_FALSE(po::inspect_object_json(project, ObjectId{999'999}));

    const auto text = po::inspect_project_text(project);
    REQUIRE(text);
    CHECK(contains(*text, "tatami"));
}

TEST_CASE("save reload keeps ids and inspect output identical", "[project_ops]") {
    const auto dir = temp_dir("roundtrip");
    const auto project = digitized();
    const fs::path osp = dir / "p.osp";
    REQUIRE(po::write_project(osp, project));
    const auto opened = po::open_project(osp);
    REQUIRE(opened);
    CHECK_FALSE(opened->info.migrated);
    const auto before = po::inspect_project_json(project);
    const auto after = po::inspect_project_json(opened->project);
    REQUIRE(before);
    REQUIRE(after);
    CHECK(*before == *after);
    for (std::size_t i = 0; i < project.embroidery_objects.size(); ++i) {
        CHECK(project.embroidery_objects[i].id == opened->project.embroidery_objects[i].id);
    }
    CHECK(opened->project.object_ids.last() == project.object_ids.last());
}

TEST_CASE("write_project honours no-clobber and missing parent directory", "[project_ops]") {
    const auto dir = temp_dir("clobber");
    const auto project = digitized();
    const fs::path osp = dir / "p.osp";
    REQUIRE(po::write_project(osp, project));
    CHECK_FALSE(po::write_project(osp, project, nullptr, {}, true));
    CHECK_FALSE(po::write_project(dir / "absent" / "p.osp", project));
}

TEST_CASE("an osp exports to a valid dst", "[project_ops]") {
    const auto dir = temp_dir("dst");
    const auto project = digitized();
    const fs::path dst = dir / "p.dst";
    REQUIRE(project_io::export_machine_file(project, "dst", dst));
    const auto stats = formats::read_dst_file(dst);
    REQUIRE(stats);
    CHECK_FALSE(stats->empty());
}

TEST_CASE("select_objects filters by combined criteria and rejects unknown ones", "[project_ops]") {
    const auto project = digitized();
    const auto all = po::select_objects(project, {});
    REQUIRE(all);
    CHECK(all->size() == project.embroidery_objects.size());

    const auto tatami = po::select_objects(project, {"type=tatami"});
    REQUIRE(tatami);
    CHECK(tatami->size() == project.embroidery_objects.size());
    const auto none = po::select_objects(project, {"type=satin"});
    REQUIRE(none);
    CHECK(none->empty());

    const auto color = po::rgb_to_hex(project.embroidery_objects.front().rgb);
    const auto byColor = po::select_objects(project, {"color=" + color, "visible=true"});
    REQUIRE(byColor);
    CHECK_FALSE(byColor->empty());

    CHECK_FALSE(po::select_objects(project, {"couleur=rouge"}));
    CHECK_FALSE(po::select_objects(project, {"type=hexagone"}));
    CHECK_FALSE(po::select_objects(project, {"sans_egal"}));
    CHECK_FALSE(po::select_objects(project, {"within=1,2,3"}));
}

TEST_CASE("check_integrity flags a dangling source vector", "[project_ops]") {
    auto project = digitized();
    CHECK(po::check_integrity(project).empty());
    project.embroidery_objects.front().source_vector = ObjectId{424'242};
    const auto problems = po::check_integrity(project);
    REQUIRE_FALSE(problems.empty());
    CHECK(contains(problems.front(), "introuvable"));
}

TEST_CASE("diff reports identical projects and a changed object", "[project_ops]") {
    const auto a = digitized();
    auto b = a;
    const auto same = po::diff_projects_json(a, b);
    REQUIRE(same);
    CHECK(contains(*same, "\"identical\": true"));
    b.embroidery_objects.front().rgb = {1, 2, 3};
    const auto diff = po::diff_projects_json(a, b);
    REQUIRE(diff);
    CHECK(contains(*diff, "\"identical\": false"));
    CHECK(contains(*diff, "#010203"));
}

TEST_CASE("color hex round trip and key value operation json", "[project_ops]") {
    CHECK(po::rgb_to_hex({255, 0, 16}) == "#FF0010");
    CHECK(po::rgb_from_hex("#ff0010") == std::array<std::uint8_t, 3>{255, 0, 16});
    CHECK_FALSE(po::rgb_from_hex("#12345"));
    CHECK_FALSE(po::rgb_from_hex("#GG0000"));

    const auto op = po::operation_json_from_args(
        "set_color", {"object=12", "rgb=#FF0000", "params={\"angle_deg\":30}", "visible=true"});
    REQUIRE(op);
    CHECK(contains(*op, "\"op\":\"set_color\""));
    CHECK(contains(*op, "\"object\":12"));
    CHECK(contains(*op, "\"rgb\":\"#FF0000\""));
    CHECK(contains(*op, "\"angle_deg\":30"));
    CHECK(contains(*op, "\"visible\":true"));
    CHECK_FALSE(po::operation_json_from_args("x", {"sans_egal"}));
    CHECK_FALSE(po::operation_json_from_args("x", {"a=1", "a=2"}));
}

TEST_CASE("error json carries a stable code", "[project_ops]") {
    const auto text = po::error_json("project apply", "E_REFUSED", "refus", "piste");
    CHECK(contains(text, "openstitch.error/1"));
    CHECK(contains(text, "\"code\": \"E_REFUSED\""));
    CHECK(contains(text, "\"ok\": false"));
}

TEST_CASE("digitize report json lists objects by type", "[project_ops]") {
    auto created = po::create_empty_project({});
    REQUIRE(created);
    document::Project project = std::move(*created);
    const auto report = po::digitize_image_into(project, logo(), {});
    REQUIRE(report);
    const auto json = po::digitize_report_json(project, *report, {}, "logo.png", "logo.osp");
    REQUIRE(json);
    CHECK(contains(*json, "openstitch.digitize-report/1"));
    CHECK(contains(*json, "ids_by_type"));
    CHECK(contains(*json, "\"decision\": \"automatic\""));
}

TEST_CASE("inspecting an empty project works and reports an empty sequence", "[project_ops]") {
    auto created = po::create_empty_project({});
    REQUIRE(created);
    const auto json = po::inspect_project_json(*created);
    REQUIRE(json);
    CHECK(contains(*json, "\"stitches\": 0"));
    CHECK(contains(*json, "\"embroidery_objects\": []"));
    CHECK(po::inspect_project_text(*created));
}

TEST_CASE("digitize applies fill inset, overlap and underpath settings to created tatami",
          "[project_ops]") {
    const auto tatamiParams = [](const document::Project& project) {
        std::vector<document::TatamiParams> out;
        for (const auto& e : project.embroidery_objects) {
            if (const auto* tp = std::get_if<document::TatamiParams>(&e.params)) {
                out.push_back(*tp);
            }
        }
        return out;
    };
    const auto make = [&](const po::DigitizeSettings& settings) {
        auto created = po::create_empty_project({});
        REQUIRE(created);
        document::Project project = std::move(*created);
        REQUIRE(po::digitize_image_into(project, logo(), settings));
        return tatamiParams(project);
    };
    const auto defaults = make({});
    REQUIRE_FALSE(defaults.empty());
    CHECK(std::all_of(defaults.begin(), defaults.end(),
                      [](const auto& t) { return t.inset.value == 200 && t.hidden_underpath; }));

    po::DigitizeSettings custom;
    custom.fill_inset_mm = 0.0;
    custom.fill_hidden_underpath = false;
    const auto changed = make(custom);
    REQUIRE_FALSE(changed.empty());
    CHECK(std::all_of(changed.begin(), changed.end(),
                      [](const auto& t) { return t.inset.value == 0 && !t.hidden_underpath; }));
}
