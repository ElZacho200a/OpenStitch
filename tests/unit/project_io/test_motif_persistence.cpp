// SPDX-License-Identifier: Apache-2.0
//
// Persistance des motifs (HP-STI-002 / HP-STI-006) : round-trip exact des deux types, et lecture
// d'un objet sans aucune cle de reglage (defauts du modele, jamais une erreur).
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <map>
#include <string>

#include "archive.hpp"
#include "openstitch/project_io/project_io.hpp"

using namespace openstitch;
namespace fs = std::filesystem;

namespace {

document::Project base_project() {
    document::Project project;
    project.original.width = 1;
    project.original.height = 1;
    project.original.rgba.assign(4, 255);
    return project;
}

} // namespace

TEST_CASE("motif run: osp round-trip keeps every setting") {
    document::Project project = base_project();
    document::MotifRunParams params;
    params.motif = document::MotifId::Heart;
    params.size = Micrometers{4'500};
    params.spacing = Micrometers{7'000};
    params.stitch_length = Micrometers{2'400};
    params.min_length = Micrometers{600};
    params.alternate = true;
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.params = params;
    project.embroidery_objects.push_back(emb);

    const auto path = fs::temp_directory_path() / "openstitch_motif_run_roundtrip.osp";
    REQUIRE(project_io::save_project(path, project).has_value());
    const auto loaded = project_io::load_project(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->embroidery_objects.size() == 1);
    REQUIRE(loaded->embroidery_objects[0].is_motif_run());
    CHECK(std::get<document::MotifRunParams>(loaded->embroidery_objects[0].params) == params);
    fs::remove(path);
}

TEST_CASE("motif fill: osp round-trip keeps every setting") {
    document::Project project = base_project();
    document::MotifFillParams params;
    params.motif = document::MotifId::Scallop;
    params.size = Micrometers{5'500};
    params.spacing = Micrometers{6'500};
    params.angle = Angle{0.75};
    params.stagger = false;
    params.inset = Micrometers{350};
    params.stitch_length = Micrometers{2'200};
    params.min_length = Micrometers{700};
    params.hidden_underpath = false;
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.params = params;
    project.embroidery_objects.push_back(emb);

    const auto path = fs::temp_directory_path() / "openstitch_motif_fill_roundtrip.osp";
    REQUIRE(project_io::save_project(path, project).has_value());
    const auto loaded = project_io::load_project(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->embroidery_objects.size() == 1);
    REQUIRE(loaded->embroidery_objects[0].is_motif_fill());
    CHECK(std::get<document::MotifFillParams>(loaded->embroidery_objects[0].params) == params);
    fs::remove(path);
}

TEST_CASE("motif types: missing keys fall back to the model defaults") {
    const auto load_type = [](const std::string& type, const std::string& file) {
        const std::string json =
            R"({"schemaVersion":3,"document":{"mmPerPx":0.5,"objectIdLast":1,"ops":[],)"
            R"("vectorObjects":[],"embroideryObjects":[{"id":1,"name":"e","sourceVector":0,)"
            R"("rgb":[1,2,3],"visible":true,"params":{"type":")" +
            type + R"("}}]}})";
        const auto path = fs::temp_directory_path() / file;
        std::map<std::string, project_io::detail::Blob> entries;
        entries.emplace("project.json", project_io::detail::Blob(json.begin(), json.end()));
        REQUIRE(project_io::detail::write_zip(path, entries).has_value());
        auto loaded = project_io::load_project(path);
        fs::remove(path);
        REQUIRE(loaded.has_value());
        REQUIRE(loaded->embroidery_objects.size() == 1);
        return loaded->embroidery_objects[0];
    };
    const auto run = load_type("motifRun", "openstitch_motif_run_defaults.osp");
    REQUIRE(run.is_motif_run());
    CHECK(std::get<document::MotifRunParams>(run.params) == document::MotifRunParams{});
    const auto fill = load_type("motifFill", "openstitch_motif_fill_defaults.osp");
    REQUIRE(fill.is_motif_fill());
    CHECK(std::get<document::MotifFillParams>(fill.params) == document::MotifFillParams{});
}
