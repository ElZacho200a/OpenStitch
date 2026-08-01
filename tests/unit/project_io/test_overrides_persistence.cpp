// SPDX-License-Identifier: Apache-2.0
//
// Persistance des retouches manuelles (Lot 8.1, schemaVersion 3, ADR-014).
// Les deux premiers TEST_CASE passent par l'API publique (save_project /
// load_project). Les suivants forgent un .osp minimal a la main -- via
// `detail::write_zip` (archive.hpp, header interne : seule l'ecriture brute
// d'entrees ZIP est reutilisee, jamais un type minizip) -- pour verifier la
// validation stricte de json_serialize.cpp (JSON malformé/hors bornes) et la
// migration v1/v2 sans reconstruire tout le pipeline d'ecriture normal.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

#include "archive.hpp"
#include "openstitch/project_io/project_io.hpp"

using namespace openstitch;
namespace fs = std::filesystem;

namespace {

Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

fs::path write_raw_osp(const std::string& name, const std::string& jsonText) {
    const auto path = fs::temp_directory_path() / name;
    std::map<std::string, project_io::detail::Blob> entries;
    entries.emplace("project.json", project_io::detail::Blob(jsonText.begin(), jsonText.end()));
    REQUIRE(project_io::detail::write_zip(path, entries).has_value());
    return path;
}

// Document minimal a un objet de broderie (RunningStitchParams), avec un
// fragment JSON optionnel injecte a la fin de l'objet (overrides et
// consorts).
std::string minimal_document_json(int schema_version, const std::string& extra_fields) {
    return "{\"schemaVersion\":" + std::to_string(schema_version) + R"(,
"document":{
  "mmPerPx":0.5,
  "objectIdLast":1,
  "ops":[],
  "vectorObjects":[],
  "embroideryObjects":[
    {"id":1,"name":"e","sourceVector":0,"rgb":[1,2,3],"visible":true,
     "params":{"type":"running","stitchLength":2500,"minLength":400,"repeats":1})" +
           extra_fields + R"(}
  ]
}
})";
}

}  // namespace

// ---------------------------------------------------------------------------
// Round-trip via l'API publique
// ---------------------------------------------------------------------------

TEST_CASE("v3 : retouches manuelles d'un objet round-trip exactement (index/pos/type/trim, uint64 > 2^53)") {
    document::Project project;
    project.original.width = 1;
    project.original.height = 1;
    project.original.rgba.assign(4, 255);

    document::EmbroideryObject e;
    e.id = project.object_ids.next();
    e.params = document::RunningStitchParams{};

    document::StitchOverride ov1;
    ov1.base_index = 2;
    ov1.moved_to = um(-12'345, 6'789);
    ov1.forced_type = document::StitchPointType::Jump;
    ov1.trim_after = true;
    document::StitchOverride ov2;
    ov2.base_index = 5;
    ov2.trim_after = false;  // valeur par defaut explicite : ne doit rien casser
    e.overrides = {ov1, ov2};
    // Empreinte au-dela de 2^53 : verifie l'absence de perte de bits par
    // passage en `double` (JSON standard) -- nlohmann conserve un entier
    // positif exact en `number_unsigned`.
    e.edited_fingerprint = 0xFFFFFFFFFFFFFFFFULL;
    e.edited_point_count = 4'000'000'000u;  // > int32 max, dans les bornes uint32
    project.embroidery_objects.push_back(e);

    const auto path = fs::temp_directory_path() / "openstitch_overrides_v3.osp";
    REQUIRE(project_io::save_project(path, project).has_value());
    const auto loaded = project_io::load_project(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->embroidery_objects.size() == 1);
    const auto& le = loaded->embroidery_objects[0];
    CHECK(le.overrides == e.overrides);
    CHECK(le.edited_fingerprint == e.edited_fingerprint);
    CHECK(le.edited_point_count == e.edited_point_count);
    fs::remove(path);
}

TEST_CASE("v3 : objet Clean (overrides vide) round-trip sans champ superflu") {
    document::Project project;
    project.original.width = 1;
    project.original.height = 1;
    project.original.rgba.assign(4, 255);

    document::EmbroideryObject e;
    e.id = project.object_ids.next();
    e.params = document::RunningStitchParams{};
    project.embroidery_objects.push_back(e);  // overrides vide (Clean)

    const auto path = fs::temp_directory_path() / "openstitch_overrides_clean.osp";
    REQUIRE(project_io::save_project(path, project).has_value());
    const auto loaded = project_io::load_project(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->embroidery_objects.size() == 1);
    CHECK(loaded->embroidery_objects[0].overrides.empty());
    CHECK(loaded->embroidery_objects[0].edited_fingerprint == 0);
    CHECK(loaded->embroidery_objects[0].edited_point_count == 0);
    fs::remove(path);
}

// ---------------------------------------------------------------------------
// Migration v1/v2 : champs absents -> Clean
// ---------------------------------------------------------------------------

TEST_CASE("migration v1 -> v3 : embroideryObjects sans overrides charge en etat Clean") {
    const auto path =
        write_raw_osp("openstitch_migrate_v1.osp", minimal_document_json(1, ""));
    const auto loaded = project_io::load_project(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->embroidery_objects.size() == 1);
    CHECK(loaded->embroidery_objects[0].overrides.empty());
    CHECK(loaded->embroidery_objects[0].edited_fingerprint == 0);
    CHECK(loaded->embroidery_objects[0].edited_point_count == 0);
    fs::remove(path);
}

TEST_CASE("migration v2 -> v3 : embroideryObjects sans overrides charge en etat Clean") {
    const auto path =
        write_raw_osp("openstitch_migrate_v2.osp", minimal_document_json(2, ""));
    const auto loaded = project_io::load_project(path);
    REQUIRE(loaded.has_value());
    CHECK(loaded->embroidery_objects[0].overrides.empty());
    fs::remove(path);
}

// ---------------------------------------------------------------------------
// uint64 exact au-dela de 2^53, lu depuis un litteral JSON externe
// ---------------------------------------------------------------------------

TEST_CASE("editedFingerprint : litteral JSON uint64 max conserve sans perte de bits") {
    const auto path = write_raw_osp(
        "openstitch_fp_max.osp",
        minimal_document_json(3, R"(,"overrides":[{"index":0,"trimAfter":true}],)"
                                 R"("editedFingerprint":18446744073709551615,"editedPointCount":42)"));
    const auto loaded = project_io::load_project(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->embroidery_objects.size() == 1);
    CHECK(loaded->embroidery_objects[0].edited_fingerprint == 0xFFFFFFFFFFFFFFFFULL);
    CHECK(loaded->embroidery_objects[0].edited_point_count == 42);
    fs::remove(path);
}

// ---------------------------------------------------------------------------
// Validation stricte : JSON malformé / hors bornes -> erreur utile
// ---------------------------------------------------------------------------

TEST_CASE("overrides corrompues : index non entier -> InvalidFile, message utile") {
    const auto path = write_raw_osp("openstitch_bad_index.osp",
        minimal_document_json(3, R"(,"overrides":[{"index":"abc"}])"));
    const auto loaded = project_io::load_project(path);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().category == ErrorCategory::InvalidFile);
    CHECK_FALSE(loaded.error().message.empty());
    fs::remove(path);
}

TEST_CASE("overrides corrompues : index negatif refuse") {
    const auto path = write_raw_osp("openstitch_neg_index.osp",
        minimal_document_json(3, R"(,"overrides":[{"index":-1}])"));
    const auto loaded = project_io::load_project(path);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().category == ErrorCategory::InvalidFile);
}

TEST_CASE("overrides corrompues : coordonnee au-dela de int32 refusee (pas de troncature silencieuse)") {
    const auto path = write_raw_osp("openstitch_overflow_pos.osp",
        minimal_document_json(3, R"(,"overrides":[{"index":0,"pos":{"x":99999999999,"y":0}}])"));
    const auto loaded = project_io::load_project(path);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().category == ErrorCategory::InvalidFile);
}

TEST_CASE("overrides corrompues : type inconnu refuse") {
    const auto path = write_raw_osp("openstitch_bad_type.osp",
        minimal_document_json(3, R"(,"overrides":[{"index":0,"type":"banana"}])"));
    const auto loaded = project_io::load_project(path);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().category == ErrorCategory::InvalidFile);
}

TEST_CASE("overrides corrompues : trimAfter non booleen refuse") {
    const auto path = write_raw_osp("openstitch_bad_trim.osp",
        minimal_document_json(3, R"(,"overrides":[{"index":0,"trimAfter":"yes"}])"));
    const auto loaded = project_io::load_project(path);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().category == ErrorCategory::InvalidFile);
}

TEST_CASE("overrides corrompues : entree sans index refusee") {
    const auto path = write_raw_osp("openstitch_missing_index.osp",
        minimal_document_json(3, R"(,"overrides":[{"trimAfter":true}])"));
    const auto loaded = project_io::load_project(path);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().category == ErrorCategory::InvalidFile);
}

TEST_CASE("overrides corrompues : editedPointCount au-dela de uint32 refuse") {
    const auto path = write_raw_osp("openstitch_overflow_count.osp",
        minimal_document_json(3, R"(,"overrides":[{"index":0,"trimAfter":true}],"editedPointCount":4294967296)"));
    const auto loaded = project_io::load_project(path);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().category == ErrorCategory::InvalidFile);
}
