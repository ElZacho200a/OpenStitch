// SPDX-License-Identifier: Apache-2.0
// Persistance des objets texte (lettrage, schéma v6, HP-TXT-001) : migration d'un
// fichier v5 sans texte, lecture tolérante d'un bloc texte minimal.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <map>

#include "archive.hpp"
#include "openstitch/project_io/project_io.hpp"

using namespace openstitch;
namespace fs = std::filesystem;

namespace {

fs::path write_json(const std::string& name, const std::string& json) {
    const auto path = fs::temp_directory_path() / name;
    std::map<std::string, project_io::detail::Blob> entries;
    entries.emplace("project.json", project_io::detail::Blob(json.begin(), json.end()));
    REQUIRE(project_io::detail::write_zip(path, entries).has_value());
    return path;
}

std::string document_json(int version, const std::string& extra, const std::string& vector_extra) {
    return "{\"schemaVersion\":" + std::to_string(version) + R"(,"document":{
  "mmPerPx":0.5,"objectIdLast":3,"ops":[],)" +
           extra + R"(
  "vectorObjects":[{"id":2,"name":"v","rgb":[1,2,3],"visible":true)" +
           vector_extra + R"(,"paths":[]}],
  "embroideryObjects":[]
}})";
}

} // namespace

TEST_CASE("schema v5 file without text loads and is flagged as migrated") {
    const auto path = write_json("osp_text_v5.osp", document_json(5, "", ""));
    project_io::LoadInfo info;
    const auto loaded = project_io::load_project(path, &info);
    REQUIRE(loaded.has_value());
    CHECK(info.fileVersion == 5);
    CHECK(info.migrated);
    CHECK(loaded->text_objects.empty());
    REQUIRE(loaded->vector_objects.size() == 1);
    CHECK_FALSE(loaded->vector_objects[0].text_owner.has_value());
    std::error_code ec;
    fs::remove(path, ec);
}

TEST_CASE("minimal text block takes model defaults and clamps bad enums") {
    const auto path = write_json(
        "osp_text_min.osp",
        document_json(6, R"("textObjects":[{"id":1,"text":"Hi","align":99,"fill":-4}],)",
                      R"(,"textOwner":1)"));
    const auto loaded = project_io::load_project(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->text_objects.size() == 1);
    const document::TextObject defaults;
    const auto& t = loaded->text_objects[0];
    CHECK(t.text == "Hi");
    CHECK(t.cap_height == defaults.cap_height);
    CHECK(t.line_spacing == defaults.line_spacing);
    CHECK(t.kerning == defaults.kerning);
    CHECK(t.density == defaults.density);
    CHECK(t.max_satin_width == defaults.max_satin_width);
    CHECK(t.align == document::TextAlign::Justify); // 99 borné à la dernière valeur
    CHECK(t.fill == document::TextFill::Auto);      // -4 borné à la première
    CHECK(loaded->vector_objects[0].text_owner == ObjectId{1});
    std::error_code ec;
    fs::remove(path, ec);
}
