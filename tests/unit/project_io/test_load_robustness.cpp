// SPDX-License-Identifier: Apache-2.0
//
// Robustesse du chargement/enregistrement .osp : JSON valide mais structure
// invalide (aucune exception ne doit fuir), version trop récente, migration
// signalée + copie de sécurité, export machine atomique.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#include "archive.hpp"
#include "openstitch/formats/format_registry.hpp"
#include "openstitch/project_io/machine_file.hpp"
#include "openstitch/project_io/project_io.hpp"

using namespace openstitch;
namespace fs = std::filesystem;

namespace {

fs::path write_raw(const std::string& name, const std::string& jsonText) {
    const auto path = fs::temp_directory_path() / name;
    std::map<std::string, project_io::detail::Blob> entries;
    entries.emplace("project.json", project_io::detail::Blob(jsonText.begin(), jsonText.end()));
    REQUIRE(project_io::detail::write_zip(path, entries).has_value());
    return path;
}

} // namespace

TEST_CASE("load_project: structure JSON invalide ne leve aucune exception") {
    // `document` est un nombre : at()/value() levent type_error en interne.
    const auto p1 = write_raw("osp_rob_doc_number.osp", R"({"schemaVersion":5,"document":42})");
    auto r1 = project_io::load_project(p1);
    CHECK_FALSE(r1.has_value());

    // schemaVersion de mauvais type.
    const auto p2 =
        write_raw("osp_rob_ver_string.osp", R"({"schemaVersion":"cinq","document":{}})");
    CHECK_FALSE(project_io::load_project(p2).has_value());

    // Racine qui n'est pas un objet.
    const auto p3 = write_raw("osp_rob_root_array.osp", "[1,2,3]");
    CHECK_FALSE(project_io::load_project(p3).has_value());

    // Section document absente.
    const auto p4 = write_raw("osp_rob_no_doc.osp", R"({"schemaVersion":5})");
    CHECK_FALSE(project_io::load_project(p4).has_value());

    for (const auto& p : {p1, p2, p3, p4}) {
        std::error_code ec;
        fs::remove(p, ec);
    }
}

TEST_CASE("load_project: version plus recente que supportee donne un message actionnable") {
    const auto path = write_raw("osp_rob_future.osp", R"({"schemaVersion":999,"document":{}})");
    auto r = project_io::load_project(path);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().category == ErrorCategory::UnsupportedFormat);
    CHECK(r.error().message.find("plus r") != std::string::npos);
    CHECK(r.error().message.find("Mettez") != std::string::npos);
    std::error_code ec;
    fs::remove(path, ec);
}

TEST_CASE("load_project: LoadInfo signale la version lue et la migration") {
    const auto path = fs::temp_directory_path() / "osp_rob_info.osp";
    document::Project project;
    REQUIRE(project_io::save_project(path, project).has_value());

    project_io::LoadInfo info;
    auto loaded = project_io::load_project(path, &info);
    REQUIRE(loaded.has_value());
    CHECK(info.fileVersion == project_io::kSchemaVersion);
    CHECK_FALSE(info.migrated);
    std::error_code ec;
    fs::remove(path, ec);
}

TEST_CASE("backup_before_migrated_save: copie .vN.osp.bak une seule fois") {
    const auto dir = fs::temp_directory_path() / "osp_rob_bak";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const auto path = dir / "monprojet.osp";
    {
        std::ofstream out(path, std::ios::binary);
        out << "ancien contenu";
    }
    const auto bak = project_io::migration_backup_path(path, 3);
    CHECK(bak.filename().string() == "monprojet.v3.osp.bak");

    REQUIRE(project_io::backup_before_migrated_save(path, 3).has_value());
    REQUIRE(fs::exists(bak));

    // Le fichier est ensuite réécrit : une 2e sauvegarde ne doit pas écraser la copie.
    {
        std::ofstream out(path, std::ios::binary);
        out << "nouveau contenu";
    }
    REQUIRE(project_io::backup_before_migrated_save(path, 3).has_value());
    std::ifstream in(bak, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(content == "ancien contenu");
    fs::remove_all(dir, ec);
}

TEST_CASE("export_machine_file: echec ne detruit pas l'export existant") {
    const auto dir = fs::temp_directory_path() / "osp_rob_export";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const auto path = dir / "existant.dst";
    {
        std::ofstream out(path, std::ios::binary);
        out << "ancien dst";
    }
    // Un répertoire occupe l'emplacement du .tmp : l'écriture échoue avant tout renommage.
    fs::create_directories(dir / "existant.dst.tmp");
    document::Project project;
    auto r = project_io::export_machine_file(project, "dst", path);
    CHECK_FALSE(r.has_value());
    std::ifstream in(path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(content == "ancien dst");
    fs::remove_all(dir, ec);
}
