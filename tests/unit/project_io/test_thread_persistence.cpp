// SPDX-License-Identifier: Apache-2.0
//
// HP-THR-004 : persistance du fil de nuancier d'un objet de broderie (schéma
// v6) + migration d'un .osp v5 (champ absent -> couleur libre) + import de
// nuanciers utilisateur (CSV/JSON).
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#include "archive.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/project_io/thread_chart_io.hpp"

using namespace openstitch;
namespace fs = std::filesystem;

namespace {

fs::path write_raw_osp(const std::string& name, const std::string& jsonText) {
    const auto path = fs::temp_directory_path() / name;
    std::map<std::string, project_io::detail::Blob> entries;
    entries.emplace("project.json", project_io::detail::Blob(jsonText.begin(), jsonText.end()));
    REQUIRE(project_io::detail::write_zip(path, entries).has_value());
    return path;
}

fs::path write_text(const std::string& name, const std::string& text) {
    const auto path = fs::temp_directory_path() / name;
    std::ofstream(path, std::ios::binary) << text;
    return path;
}

} // namespace

TEST_CASE("thread : round-trip du fil assigne et absence = couleur libre") {
    document::Project project;
    project.original.width = 1;
    project.original.height = 1;
    project.original.rgba.assign(4, 255);

    document::EmbroideryObject withThread;
    withThread.id = project.object_ids.next();
    withThread.rgb = {200, 16, 46};
    withThread.thread = thread_palette::ThreadKey{"user_acme", "A-1"};
    project.embroidery_objects.push_back(withThread);

    document::EmbroideryObject free;
    free.id = project.object_ids.next();
    free.rgb = {1, 2, 3};
    project.embroidery_objects.push_back(free);

    const auto path = fs::temp_directory_path() / "openstitch_thread_roundtrip.osp";
    REQUIRE(project_io::save_project(path, project).has_value());
    project_io::LoadInfo info;
    const auto loaded = project_io::load_project(path, &info);
    fs::remove(path);
    REQUIRE(loaded.has_value());
    CHECK(info.fileVersion == project_io::kSchemaVersion);
    REQUIRE(loaded->embroidery_objects.size() == 2);
    CHECK(loaded->embroidery_objects[0].thread == thread_palette::ThreadKey{"user_acme", "A-1"});
    CHECK(loaded->embroidery_objects[0].rgb == std::array<std::uint8_t, 3>{200, 16, 46});
    CHECK_FALSE(loaded->embroidery_objects[1].thread.has_value());
}

TEST_CASE("thread : un projet v5 (sans champ thread) migre vers des couleurs libres") {
    const std::string json =
        R"({"schemaVersion":5,"document":{"mmPerPx":0.5,"objectIdLast":1,"ops":[],)"
        R"("vectorObjects":[],"embroideryObjects":[{"id":1,"name":"e","sourceVector":0,)"
        R"("rgb":[1,2,3],"visible":true,)"
        R"("params":{"type":"running","stitchLength":2500,"minLength":400,"repeats":1}}]}})";
    const auto path = write_raw_osp("openstitch_thread_v5.osp", json);
    project_io::LoadInfo info;
    const auto loaded = project_io::load_project(path, &info);
    fs::remove(path);
    REQUIRE(loaded.has_value());
    CHECK(info.fileVersion == 5);
    CHECK(info.migrated);
    REQUIRE(loaded->embroidery_objects.size() == 1);
    CHECK_FALSE(loaded->embroidery_objects[0].thread.has_value());
    CHECK(loaded->embroidery_objects[0].rgb == std::array<std::uint8_t, 3>{1, 2, 3});
}

TEST_CASE("thread : champ thread malforme refuse proprement") {
    const std::string json =
        R"({"schemaVersion":6,"document":{"mmPerPx":0.5,"objectIdLast":1,"ops":[],)"
        R"("vectorObjects":[],"embroideryObjects":[{"id":1,"name":"e","sourceVector":0,)"
        R"("rgb":[1,2,3],"visible":true,"thread":{"chart":"x"},)"
        R"("params":{"type":"running","stitchLength":2500,"minLength":400,"repeats":1}}]}})";
    const auto path = write_raw_osp("openstitch_thread_bad.osp", json);
    const auto loaded = project_io::load_project(path);
    fs::remove(path);
    CHECK_FALSE(loaded.has_value());
}

TEST_CASE("import de nuancier : JSON hex et tableau, id derive du nom") {
    const std::string json = R"({"name":"Mes Fils","source":"carte 2024","threads":[
        {"code":"1","name":"Rouge","rgb":"#FF0000","range":"P40"},
        {"code":"2","name":"Vert","rgb":[0,255,0],"brand":"Acme"}]})";
    const auto path = write_text("mes_fils_test.json", json);
    const auto chart = project_io::read_thread_chart_file(path);
    fs::remove(path);
    REQUIRE(chart.has_value());
    CHECK(chart->chart_id == "user_mes_fils");
    CHECK(chart->display_name == "Mes Fils");
    CHECK(chart->source_note == "carte 2024");
    REQUIRE(chart->threads.size() == 2);
    CHECK(chart->threads[0].key == thread_palette::ThreadKey{"user_mes_fils", "1"});
    CHECK(chart->threads[0].range == "P40");
    CHECK(chart->threads[0].brand == "Mes Fils");
    CHECK(chart->threads[1].rgb == std::array<std::uint8_t, 3>{0, 255, 0});
    CHECK(chart->threads[1].brand == "Acme");
}

TEST_CASE("import de nuancier : CSV via fichier, erreurs JSON et extension inconnue") {
    const auto csv = write_text("mon_nuancier_test.csv", "code,name,hex\nA,Bleu,#0000FF\n");
    const auto fromCsv = project_io::read_thread_chart_file(csv);
    fs::remove(csv);
    REQUIRE(fromCsv.has_value());
    CHECK(fromCsv->chart_id == "user_mon_nuancier_test");
    CHECK(fromCsv->source_note.find("mon_nuancier_test.csv") != std::string::npos);

    const thread_palette::ChartImportInfo d{"user_x", "X", ""};
    CHECK_FALSE(project_io::parse_thread_chart_json("{not json", d).has_value());
    CHECK_FALSE(project_io::parse_thread_chart_json(R"({"threads":[]})", d).has_value());
    CHECK_FALSE(project_io::parse_thread_chart_json(R"({"threads":[{"code":"1","rgb":"zz"}]})", d)
                    .has_value());
    CHECK_FALSE(project_io::parse_thread_chart_json(
                    R"({"threads":[{"code":"1","rgb":"#000000"},{"code":"1","rgb":"#000001"}]})", d)
                    .has_value());
    CHECK_FALSE(
        project_io::read_thread_chart_file(fs::temp_directory_path() / "absent.csv").has_value());
    const auto txt = write_text("nuancier_test.txt", "x");
    CHECK_FALSE(project_io::read_thread_chart_file(txt).has_value());
    fs::remove(txt);
}
