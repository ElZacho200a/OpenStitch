// SPDX-License-Identifier: Apache-2.0
//
// HP-FMT-021 : table de migrations versionnees v(n) -> v(n+1) du schema .osp.
// Les fichiers d'anciennes versions sont CONSTRUITS ici (project.json forge a la main puis
// archive via detail::write_zip), un par version, et relus par load_project.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <variant>

#include <nlohmann/json.hpp>

#include "archive.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/project_io/migrations.hpp"
#include "openstitch/project_io/project_io.hpp"

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

// Document minimal d'un seul objet satin, SANS rungs, canvas ni maxWidthHard (v1).
std::string satin_document(int version, const std::string& extraParams) {
    const std::string rail =
        R"({"closed":false,"nodes":[{"pos":[0,0],"smooth":false},{"pos":[10000,0],"smooth":false}]})";
    return "{\"schemaVersion\":" + std::to_string(version) +
           R"(,"document":{"mmPerPx":0.5,"objectIdLast":1,"ops":[],"vectorObjects":[],)"
           R"("embroideryObjects":[{"id":1,"name":"s","sourceVector":0,"rgb":[1,2,3],)"
           R"("visible":true,"params":{"type":"satin","railA":)" +
           rail + R"(,"railB":)" + rail +
           R"(,"density":400,"pullCompensation":0,"centerUnderlay":true,"maxWidth":9000)" +
           extraParams + R"(}}]}})";
}

const document::SatinParams& only_satin(const document::Project& project) {
    REQUIRE(project.embroidery_objects.size() == 1);
    const auto* satin = std::get_if<document::SatinParams>(&project.embroidery_objects[0].params);
    REQUIRE(satin != nullptr);
    return *satin;
}

} // namespace

TEST_CASE("migrations : la table couvre v1 a la version lisible sans trou") {
    const auto steps = project_io::migration_steps();
    REQUIRE(steps.size() + 1 == static_cast<std::size_t>(project_io::kMaxReadableSchemaVersion));
    for (std::size_t i = 0; i < steps.size(); ++i) {
        CHECK(steps[i].from == static_cast<int>(i) + 1);
        CHECK(steps[i].to == steps[i].from + 1);
        CHECK_FALSE(steps[i].description.empty());
    }
}

TEST_CASE("migrations : v7 -> v8 est documentee et ne reecrit pas un document sans texte") {
    const auto steps = project_io::migration_steps();
    REQUIRE(steps.size() >= 7);
    CHECK(steps[6].from == 7);
    CHECK(steps[6].to == 8);
    CHECK(project_io::kMaxReadableSchemaVersion == project_io::kSchemaVersionTextTypography);
    const auto migrated = project_io::migrate_project_json(satin_document(7, ""));
    REQUIRE(migrated.has_value());
    const auto root = nlohmann::json::parse(*migrated);
    CHECK(root.at("schemaVersion") == 8);
    CHECK_FALSE(root.at("document").contains("textObjects"));
}

TEST_CASE("migrations : chaque version lisible se relit et reporte sa version") {
    for (int version = 1; version <= project_io::kMaxReadableSchemaVersion; ++version) {
        // maxWidthHard n'existe qu'a partir de v4 dans les fichiers reels.
        const std::string extra = version >= 4 ? R"(,"maxWidthHard":30000)" : "";
        const auto path = write_raw_osp("osp_migration_v" + std::to_string(version) + ".osp",
                                        satin_document(version, extra));
        project_io::LoadInfo info;
        const auto loaded = project_io::load_project(path, &info);
        REQUIRE(loaded.has_value());
        CHECK(info.fileVersion == version);
        CHECK(info.migrated == (version < project_io::kSchemaVersion));
        const auto& satin = only_satin(*loaded);
        CHECK(satin.max_width.value == 9000);
        CHECK(satin.rungs.empty());
        CHECK(satin.max_width_hard.value == (version >= 4 ? 30000 : 48000));
        fs::remove(path);
    }
}

TEST_CASE("migrations : v1 -> v2 materialise rungs et canvas par defaut") {
    const auto migrated = project_io::migrate_project_json(satin_document(1, ""));
    REQUIRE(migrated.has_value());
    const auto root = nlohmann::json::parse(*migrated);
    CHECK(root.at("schemaVersion") == project_io::kMaxReadableSchemaVersion);
    const auto& doc = root.at("document");
    CHECK(doc.at("canvas").at("width") == 100000);
    CHECK(doc.at("canvas").at("height") == 100000);
    const auto& params = doc.at("embroideryObjects").at(0).at("params");
    CHECK(params.at("rungs").is_array());
    CHECK(params.at("rungs").empty());
    CHECK(params.at("maxWidthHard") == 48000);
}

TEST_CASE("migrations : v3 -> v4 conserve un maxWidthHard deja present et ne touche pas v4+") {
    const auto from3 =
        project_io::migrate_project_json(satin_document(3, R"(,"maxWidthHard":20000)"));
    REQUIRE(from3.has_value());
    CHECK(nlohmann::json::parse(*from3)
              .at("document")
              .at("embroideryObjects")
              .at(0)
              .at("params")
              .at("maxWidthHard") == 20000);

    // Un fichier v4 ne repasse PAS par v1 -> v2 : pas de canvas injecte.
    const auto from4 = project_io::migrate_project_json(satin_document(4, ""));
    REQUIRE(from4.has_value());
    CHECK_FALSE(nlohmann::json::parse(*from4).at("document").contains("canvas"));
}

TEST_CASE("migrations : sortie deterministe octet pour octet") {
    const auto a = project_io::migrate_project_json(satin_document(1, ""));
    const auto b = project_io::migrate_project_json(satin_document(1, ""));
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(*a == *b);
    // Idempotence : re-migrer un fichier deja a jour ne change rien.
    const auto again = project_io::migrate_project_json(*a);
    REQUIRE(again.has_value());
    CHECK(*again == *a);
}

TEST_CASE("migrations : version future refusee avec un message clair") {
    const int future = project_io::kMaxReadableSchemaVersion + 1;
    const auto path =
        write_raw_osp("osp_migration_future.osp", satin_document(future, R"(,"maxWidthHard":1)"));
    const auto loaded = project_io::load_project(path);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().category == ErrorCategory::UnsupportedFormat);
    CHECK(loaded.error().message.find("v" + std::to_string(future)) != std::string::npos);
    CHECK(loaded.error().message.find(
              "v" + std::to_string(project_io::kMaxReadableSchemaVersion)) != std::string::npos);
    fs::remove(path);

    const auto direct = project_io::migrate_project_json(satin_document(99, ""));
    REQUIRE_FALSE(direct.has_value());
    CHECK(direct.error().category == ErrorCategory::UnsupportedFormat);
}

TEST_CASE("migrations : version absente, nulle ou JSON illisible refuses") {
    CHECK_FALSE(project_io::migrate_project_json(R"({"document":{}})").has_value());
    CHECK_FALSE(
        project_io::migrate_project_json(R"({"schemaVersion":0,"document":{}})").has_value());
    CHECK_FALSE(project_io::migrate_project_json("pas du json").has_value());
    CHECK(project_io::declared_schema_version("pas du json") == 0);
    CHECK(project_io::declared_schema_version(R"({"schemaVersion":5})") == 5);
}
