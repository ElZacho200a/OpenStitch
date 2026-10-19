// SPDX-License-Identifier: Apache-2.0
// Persistance de la méthode de l'auto-satin (lot C de docs/auto-satin-research.md) : par nom,
// schéma v7 seulement quand la méthode n'est pas le squelette, refus explicite d'une méthode
// inconnue (jamais de substitution silencieuse).
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <map>
#include <string>

#include "archive.hpp"
#include "openstitch/project_io/project_io.hpp"

using namespace openstitch;
namespace fs = std::filesystem;

namespace {

fs::path unique_path(const std::string& stem) {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() / (stem + std::to_string(unique) + ".osp");
}

document::Project project_with(document::AutoSatinParams params) {
    document::Project project;
    project.original.width = 1;
    project.original.height = 1;
    project.original.rgba.assign(4, 255);
    document::EmbroideryObject e;
    e.id = ObjectId{10};
    e.name = "satin";
    e.params = params;
    project.embroidery_objects.push_back(e);
    return project;
}

Result<document::Project> load_json(int version, const std::string& paramsJson,
                                    project_io::LoadInfo* info = nullptr) {
    const std::string json = "{\"schemaVersion\":" + std::to_string(version) +
                             R"(,"document":{"mmPerPx":0.5,"objectIdLast":9,"ops":[],)"
                             R"("vectorObjects":[],"embroideryObjects":[{"id":4,"name":"a",)"
                             R"("sourceVector":1,"rgb":[1,2,3],"params":)" +
                             paramsJson + "}]}}";
    const auto path = unique_path("openstitch_strategy_json_");
    std::map<std::string, project_io::detail::Blob> entries;
    entries.emplace("project.json", project_io::detail::Blob(json.begin(), json.end()));
    REQUIRE(project_io::detail::write_zip(path, entries).has_value());
    auto loaded = project_io::load_project(path, info);
    fs::remove(path);
    return loaded;
}

} // namespace

TEST_CASE("auto-satin : la methode et l'angle de balayage font l'aller-retour") {
    for (const auto strategy :
         {document::AutoSatinStrategy::Skeleton, document::AutoSatinStrategy::ParallelSweep,
          document::AutoSatinStrategy::MedialAxis}) {
        document::AutoSatinParams p;
        p.strategy = strategy;
        p.sweep_angle = Angle{0.7};
        const auto path = unique_path("openstitch_strategy_rt_");
        REQUIRE(project_io::save_project(path, project_with(p)).has_value());
        const auto loaded = project_io::load_project(path);
        fs::remove(path);
        REQUIRE(loaded.has_value());
        REQUIRE(loaded->embroidery_objects.size() == 1);
        CHECK(std::get<document::AutoSatinParams>(loaded->embroidery_objects[0].params) == p);
    }
}

TEST_CASE(
    "auto-satin : un guide en courbe fait l'aller-retour, un guide droit n'ecrit pas de path") {
    document::AutoSatinParams p;
    p.strategy = document::AutoSatinStrategy::ParallelSweep;
    const auto pt = [](std::int32_t x, std::int32_t y) {
        return Vec2um{Micrometers{x}, Micrometers{y}};
    };
    p.guides.push_back({pt(1'000, 2'000), Angle{0.4}, true, {}});
    p.guides.push_back(
        {pt(5'000, 0), Angle{0.1}, true, {pt(5'000, 0), pt(9'000, 3'000), pt(12'000, 0)}});
    const auto path = unique_path("openstitch_guide_curve_rt_");
    REQUIRE(project_io::save_project(path, project_with(p)).has_value());
    const auto loaded = project_io::load_project(path);
    fs::remove(path);
    REQUIRE(loaded.has_value());
    const auto& back = std::get<document::AutoSatinParams>(loaded->embroidery_objects[0].params);
    CHECK(back == p);
    CHECK(back.guides[0].path.empty());
    CHECK(back.guides[1].path.size() == 3);
}

TEST_CASE("auto-satin : le fichier reste en v6 tant que la methode est le squelette") {
    const auto path = unique_path("openstitch_strategy_v6_");
    REQUIRE(project_io::save_project(path, project_with({})).has_value());
    project_io::LoadInfo info;
    REQUIRE(project_io::load_project(path, &info).has_value());
    fs::remove(path);
    CHECK(info.fileVersion == project_io::kSchemaVersion);
}

TEST_CASE("auto-satin : une autre methode ecrit le schema v7") {
    for (const auto strategy :
         {document::AutoSatinStrategy::ParallelSweep, document::AutoSatinStrategy::MedialAxis}) {
        document::AutoSatinParams p;
        p.strategy = strategy;
        const auto path = unique_path("openstitch_strategy_v7_");
        REQUIRE(project_io::save_project(path, project_with(p)).has_value());
        project_io::LoadInfo info;
        REQUIRE(project_io::load_project(path, &info).has_value());
        fs::remove(path);
        CHECK(info.fileVersion == project_io::kSchemaVersionAutoSatinStrategy);
        CHECK_FALSE(info.migrated);
    }
}

TEST_CASE("auto-satin : un fichier sans la cle est lu avec le squelette") {
    const auto loaded = load_json(6, R"({"type":"autoSatin"})");
    REQUIRE(loaded.has_value());
    const auto& p = std::get<document::AutoSatinParams>(loaded->embroidery_objects[0].params);
    CHECK(p.strategy == document::AutoSatinStrategy::Skeleton);
    CHECK_FALSE(p.sweep_angle.has_value());
}

TEST_CASE("auto-satin : une methode inconnue est refusee explicitement") {
    for (const char* bad :
         {R"({"type":"autoSatin","strategy":"voronoi_v9"})",
          R"({"type":"autoSatin","strategy":42})", R"({"type":"autoSatin","strategy":null})"}) {
        INFO(bad);
        const auto loaded = load_json(7, bad);
        REQUIRE_FALSE(loaded.has_value());
        CHECK(loaded.error().message.find("auto-satin") != std::string::npos);
    }
}

TEST_CASE("auto-satin : un angle de balayage invalide est refuse") {
    const auto loaded = load_json(7, R"({"type":"autoSatin","sweepAngle":"vite"})");
    REQUIRE_FALSE(loaded.has_value());
}

TEST_CASE("auto-satin : une version de schema plus recente que la version lisible est refusee") {
    const auto loaded =
        load_json(project_io::kMaxReadableSchemaVersion + 1, R"({"type":"autoSatin"})");
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().category == ErrorCategory::UnsupportedFormat);
}

TEST_CASE("auto-satin : le presse-papiers transporte la methode") {
    document::AutoSatinParams p;
    p.strategy = document::AutoSatinStrategy::MedialAxis;
    p.sweep_angle = Angle{1.1};
    document::ObjectFragment fragment;
    document::VectorObject vec;
    vec.id = ObjectId{9};
    vec.name = "v";
    fragment.vectors.push_back(vec);
    fragment.embroideries = project_with(p).embroidery_objects;
    fragment.embroideries[0].source_vector = vec.id;
    const auto text = project_io::fragment_to_json(fragment);
    const auto back = project_io::fragment_from_json(text);
    if (!back) {
        FAIL(back.error().message);
    }
    REQUIRE(back->embroideries.size() == 1);
    CHECK(std::get<document::AutoSatinParams>(back->embroideries[0].params) == p);
}
