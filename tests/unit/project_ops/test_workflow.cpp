// SPDX-License-Identifier: Apache-2.0
// Cycle complet de l'API programmatique sur de vraies images du depot :
// image -> projet -> inspection -> lot -> sauvegarde -> rechargement -> export DST.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <nlohmann/json.hpp>

#include "openstitch/formats/dst.hpp"
#include "openstitch/project_io/machine_file.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "openstitch/project_ops/inspect.hpp"
#include "openstitch/project_ops/operations.hpp"
#include "openstitch/project_ops/project_file.hpp"
#include "openstitch/project_ops/quality.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

using namespace openstitch;
namespace po = openstitch::project_ops;
namespace fs = std::filesystem;

namespace {

fs::path fixtures() {
    return fs::path(OPENSTITCH_FIXTURES_DIR);
}

fs::path temp_dir(const std::string& name) {
    const auto dir = fs::temp_directory_path() / ("openstitch_workflow_" + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

document::Project digitize(const fs::path& image, po::DigitizeSettings settings = {}) {
    auto created = po::create_empty_project({});
    REQUIRE(created);
    document::Project project = std::move(*created);
    const auto report = po::digitize_image_into(project, image, settings);
    REQUIRE(report);
    REQUIRE(report->embroidery_objects > 0);
    return project;
}

std::string bytes_of(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("full cycle image to osp to edit to reload to dst", "[project_ops][workflow]") {
    const auto dir = temp_dir("cycle");
    auto project = digitize(fixtures() / "tentabrode.png");
    const fs::path osp = dir / "p.osp";
    REQUIRE(po::write_project(osp, project));

    // Rouvre, inspecte, modifie par un lot, sauvegarde sous un autre nom.
    auto opened = po::open_project(osp);
    REQUIRE(opened);
    const auto id = opened->project.embroidery_objects.front().id.value;
    const std::string batch = R"({"schema":"openstitch.batch/1","operations":[)"
                              R"({"op":"set_color","object":)" +
                              std::to_string(id) +
                              R"(,"rgb":"#102030"},)"
                              R"({"op":"set_params","object":)" +
                              std::to_string(id) +
                              R"(,"params":{"row_spacing_mm":0.5,"angle_deg":33}}]})";
    const auto report = po::apply_batch(opened->project, batch);
    REQUIRE(report.ok);
    CHECK(report.applied);
    const fs::path edited = dir / "edited.osp";
    REQUIRE(po::write_project(edited, opened->project));

    // Rechargement : les modifications persistent, les ids sont stables.
    const auto reloaded = po::open_project(edited);
    REQUIRE(reloaded);
    const auto* obj = reloaded->project.findEmbroidery(ObjectId{id});
    REQUIRE(obj != nullptr);
    CHECK(obj->rgb == std::array<std::uint8_t, 3>{0x10, 0x20, 0x30});
    const auto* tatami = std::get_if<document::TatamiParams>(&obj->params);
    REQUIRE(tatami != nullptr);
    CHECK(tatami->row_spacing.value == 500);
    CHECK(reloaded->project.embroidery_objects.size() == project.embroidery_objects.size());

    // Les points sont regeneres depuis le document ; export DST valide.
    const auto seq = stitch_generation::effective_sequence(reloaded->project);
    REQUIRE(seq);
    const fs::path dst = dir / "p.dst";
    REQUIRE(project_io::export_machine_file(reloaded->project, "dst", dst));
    const auto readBack = formats::read_dst_file(dst);
    REQUIRE(readBack);
    CHECK_FALSE(readBack->empty());

    // Contrôle qualité sur le projet edité.
    const auto quality = po::build_quality_report(reloaded->project);
    REQUIRE(quality);
    CHECK(quality->json.find("openstitch.quality/1") != std::string::npos);
}

TEST_CASE("a failing batch leaves memory and file untouched", "[project_ops][workflow]") {
    const auto dir = temp_dir("atomic");
    auto project = digitize(fixtures() / "cli" / "logo_small.png");
    const fs::path osp = dir / "p.osp";
    REQUIRE(po::write_project(osp, project));
    const std::string fileBefore = bytes_of(osp);
    const auto inspectBefore = po::inspect_project_json(project);
    REQUIRE(inspectBefore);

    const auto id = project.embroidery_objects.front().id.value;
    const std::string batch = R"({"schema":"openstitch.batch/1","operations":[)"
                              R"({"op":"set_color","object":)" +
                              std::to_string(id) +
                              R"(,"rgb":"#FF0000"},)"
                              R"({"op":"set_params","object":)" +
                              std::to_string(id) + R"(,"params":{"row_spacing_mm":999}}]})";
    const auto report = po::apply_batch(project, batch);
    CHECK_FALSE(report.ok);
    CHECK_FALSE(report.applied);
    CHECK(*po::inspect_project_json(project) == *inspectBefore);
    CHECK(bytes_of(osp) == fileBefore);
    CHECK(report.to_json().find("E_OUT_OF_RANGE") != std::string::npos);
}

TEST_CASE("dry run validates without modifying", "[project_ops][workflow]") {
    auto project = digitize(fixtures() / "cli" / "logo_small.png");
    const auto before = po::inspect_project_json(project);
    const auto id = project.embroidery_objects.front().id.value;
    po::BatchOptions options;
    options.dry_run = true;
    const auto report = po::apply_operation(
        project, R"({"op":"set_color","object":)" + std::to_string(id) + R"(,"rgb":"#00FF00"})",
        options);
    CHECK(report.ok);
    CHECK_FALSE(report.applied);
    CHECK(*po::inspect_project_json(project) == *before);
}

TEST_CASE("inspect parameter names are accepted by set_params", "[project_ops][workflow]") {
    auto project = digitize(fixtures() / "cli" / "logo_small.png");
    for (const auto& e : project.embroidery_objects) {
        const auto text = po::inspect_object_json(project, e.id);
        REQUIRE(text);
        const auto doc = nlohmann::json::parse(*text);
        nlohmann::json params = nlohmann::json::object();
        for (const auto& [k, v] : doc["object"]["params"].items()) {
            if (v.is_number() || v.is_boolean() || v.is_string()) {
                if (k == "guides" || k == "break_lines" || k == "rungs" ||
                    k.rfind("rail_", 0) == 0 || k == "density_gradient" || k == "border_width_mm") {
                    continue;
                }
                params[k] = v;
            }
        }
        nlohmann::json op = {{"op", "set_params"}, {"object", e.id.value}, {"params", params}};
        po::BatchOptions options;
        options.dry_run = true;
        const auto report = po::apply_operation(project, op.dump(), options);
        INFO(op.dump());
        INFO(report.to_json());
        CHECK(report.ok);
    }
}

TEST_CASE("aliases chain created objects inside one batch", "[project_ops][workflow]") {
    auto created = po::create_empty_project({});
    REQUIRE(created);
    auto project = std::move(*created);
    const std::string batch =
        R"({"schema":"openstitch.batch/1","operations":[)"
        R"({"op":"add_shape","as":"a","shape":"rect","width_mm":20,"height_mm":10,"rgb":"#336699"},)"
        R"({"op":"translate","object":"$a","dx_mm":5,"dy_mm":-3},)"
        R"({"op":"set_params","object":"$a","params":{"angle_deg":45}}]})";
    const auto report = po::apply_batch(project, batch);
    INFO(report.to_json());
    REQUIRE(report.ok);
    REQUIRE(project.embroidery_objects.size() == 1);
    const auto seq = stitch_generation::effective_sequence(project);
    REQUIRE(seq);
    CHECK_FALSE(seq->empty());
}

TEST_CASE("auto_satin on a narrow band goes through the real engine", "[project_ops][workflow]") {
    auto created = po::create_empty_project({});
    REQUIRE(created);
    auto project = std::move(*created);
    const auto report = po::apply_batch(
        project, R"({"schema":"openstitch.batch/1","operations":[)"
                 R"({"op":"add_shape","as":"b","shape":"rect","width_mm":30,"height_mm":3},)"
                 R"({"op":"auto_satin","object":"$b"}]})");
    INFO(report.to_json());
    REQUIRE(report.ok);
    CHECK(project.embroidery_objects.front().is_auto_satin());

    // Un disque est refuse par le moteur : message de refus, projet inchange.
    auto other = std::move(*po::create_empty_project({}));
    const auto refused = po::apply_batch(
        other, R"({"schema":"openstitch.batch/1","operations":[)"
               R"({"op":"add_shape","as":"c","shape":"ellipse","width_mm":20,"height_mm":20},)"
               R"({"op":"auto_satin","object":"$c"}]})");
    CHECK_FALSE(refused.ok);
    CHECK(other.embroidery_objects.empty());
}

TEST_CASE("an unknown object or operation is a named refusal", "[project_ops][workflow]") {
    auto project = digitize(fixtures() / "cli" / "logo_small.png");
    const auto r1 =
        po::apply_operation(project, R"({"op":"set_color","object":987654,"rgb":"#000000"})");
    CHECK_FALSE(r1.ok);
    CHECK(r1.to_json().find("E_UNKNOWN_OBJECT") != std::string::npos);
    const auto r2 = po::apply_operation(project, R"({"op":"explode"})");
    CHECK(r2.to_json().find("E_UNKNOWN_OP") != std::string::npos);
    const auto r3 = po::apply_batch(project, "{pas du json");
    CHECK_FALSE(r3.ok);
    CHECK(r3.to_json().find("E_SCHEMA") != std::string::npos);
}

TEST_CASE("quality svg and report on a digitized project", "[project_ops][workflow]") {
    const auto project = digitize(fixtures() / "cli" / "logo_small.png");
    const auto svg = po::render_quality_svg(project, {.directions = true});
    REQUIRE(svg);
    CHECK(svg->find("<svg") != std::string::npos);
    const auto report = po::build_quality_report(project);
    REQUIRE(report);
    CHECK(report->passed());
}
