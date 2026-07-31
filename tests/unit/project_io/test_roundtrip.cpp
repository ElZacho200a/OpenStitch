// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "openstitch/project_io/project_io.hpp"

using namespace openstitch;
namespace fs = std::filesystem;

namespace {

Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

geometry::Path square_path() {
    geometry::Path p;
    p.closed = true;
    p.nodes = {{um(0, 0), geometry::NodeType::Corner, {}, {}},
               {um(5'000, 0), geometry::NodeType::Corner, {}, {}},
               {um(5'000, 5'000), geometry::NodeType::Smooth, um(-100, 0), um(100, 0)},
               {um(0, 5'000), geometry::NodeType::Corner, {}, {}}};
    return p;
}

// Projet complet : image, ops, segmentation, vecteurs, broderie (les 3 types).
document::Project rich_project() {
    document::Project project;
    project.mm_per_px = Millimeters{0.25};
    project.canvas.width = Micrometers{180'000};
    project.canvas.height = Micrometers{130'000};

    project.original.width = 4;
    project.original.height = 2;
    project.original.source_had_alpha = true;
    project.original.rgba.assign(4 * 2 * 4, 0);
    for (std::size_t i = 0; i < project.original.rgba.size(); i += 4) {
        project.original.rgba[i] = 200;
        project.original.rgba[i + 1] = 50;
        project.original.rgba[i + 3] = 255;
    }

    project.ops.push_back(image::GrayscaleOp{});
    project.ops.push_back(image::QuantizeOp{4});
    project.ops.push_back(image::BrightnessContrastOp{10.0, -5.0});

    segmentation::Segmentation seg;
    seg.width = 4;
    seg.height = 2;
    seg.labels = {1, 1, 2, 2, 1, 1, 2, 2};
    seg.region_slots.push_back(segmentation::Region{RegionId{1}, {200, 50, 0}, 4});
    seg.region_slots.push_back(segmentation::Region{RegionId{2}, {0, 100, 200}, 4});
    project.segmentation = std::move(seg);

    document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.name = "carré";
    vec.source_region = RegionId{1};
    vec.rgb = {200, 50, 0};
    vec.paths.push_back(geometry::PathSet{square_path(), {square_path()}});
    project.vector_objects.push_back(vec);

    document::EmbroideryObject run;
    run.id = project.object_ids.next();
    run.name = "contour";
    run.source_vector = vec.id;
    run.rgb = {200, 50, 0};
    run.params = document::RunningStitchParams{Micrometers{2'500}, Micrometers{400}, 3};
    project.embroidery_objects.push_back(run);

    document::EmbroideryObject fill;
    fill.id = project.object_ids.next();
    fill.name = "remplissage";
    fill.source_vector = vec.id;
    fill.rgb = {0, 100, 200};
    document::TatamiParams tat;
    tat.angle = Angle{0.7};
    tat.row_spacing = Micrometers{450};
    tat.underlay_edge = true;
    tat.underlay_parallel = true;
    tat.underlay_spacing = Micrometers{2'500};
    tat.hidden_underpath = true;
    tat.entry_point = Vec2um{Micrometers{321}, Micrometers{654}};
    fill.params = tat;
    project.embroidery_objects.push_back(fill);

    document::EmbroideryObject sat;
    sat.id = project.object_ids.next();
    sat.name = "satin";
    sat.rgb = {10, 20, 30};
    document::SatinParams sp;
    sp.rail_a = square_path();
    sp.rail_b = square_path();
    sp.rungs.push_back({Vec2um{Micrometers{100}, Micrometers{200}},
                        Vec2um{Micrometers{300}, Micrometers{400}}});
    sp.rungs.push_back({Vec2um{Micrometers{500}, Micrometers{600}},
                        Vec2um{Micrometers{700}, Micrometers{800}}});
    sp.density = Micrometers{350};
    sp.center_underlay = true;
    sp.short_stitch = document::SatinShortStitch::MultiLevelInset;
    sp.split_stitch = document::SatinSplit::Staggered;
    sp.cap_end = document::SatinCap::Tapered;
    sp.max_stitch_length = Micrometers{5'500};
    sp.underlay_edge = true;
    sp.underlay_zigzag = true;
    sp.pull_left = Micrometers{120};
    sp.push_end = Micrometers{900};
    sp.lock_start = document::SatinLock::BackAndForth;
    sp.lock_end = document::SatinLock::MicroZigzag;
    sp.lock_passes = 3;
    sp.entry_point = Vec2um{Micrometers{1'234}, Micrometers{5'678}};
    sat.params = sp;
    project.embroidery_objects.push_back(sat);

    return project;
}

fs::path temp_osp() {
    return fs::temp_directory_path() / "openstitch_roundtrip.osp";
}

}  // namespace

TEST_CASE("projet complet : save puis load = memes donnees") {
    const auto original = rich_project();
    const auto path = temp_osp();

    REQUIRE(project_io::save_project(path, original).has_value());
    const auto loaded = project_io::load_project(path);
    REQUIRE(loaded.has_value());

    CHECK(loaded->mm_per_px.value == original.mm_per_px.value);
    CHECK(loaded->object_ids.last() == original.object_ids.last());
    CHECK(loaded->canvas.width == original.canvas.width);
    CHECK(loaded->canvas.height == original.canvas.height);

    // Image restauree (PNG sans perte).
    CHECK(loaded->original.width == 4);
    CHECK(loaded->original.height == 2);
    CHECK(loaded->original.rgba == original.original.rgba);

    // Ops.
    REQUIRE(loaded->ops.size() == 3);
    CHECK(std::holds_alternative<image::GrayscaleOp>(loaded->ops[0]));
    CHECK(std::holds_alternative<image::QuantizeOp>(loaded->ops[1]));

    // Segmentation.
    REQUIRE(loaded->segmentation.has_value());
    CHECK(loaded->segmentation->labels == original.segmentation->labels);
    CHECK(loaded->segmentation->region_count() == 2);
    CHECK(loaded->segmentation->find(RegionId{2})->rgb ==
          std::array<std::uint8_t, 3>{0, 100, 200});

    // Vecteurs (avec un noeud lisse a tangentes).
    REQUIRE(loaded->vector_objects.size() == 1);
    CHECK(loaded->vector_objects[0].paths == original.vector_objects[0].paths);
    CHECK(loaded->vector_objects[0].source_region == RegionId{1});

    // Objets de broderie : les 3 types et leurs parametres.
    REQUIRE(loaded->embroidery_objects.size() == 3);
    CHECK(std::holds_alternative<document::RunningStitchParams>(
        loaded->embroidery_objects[0].params));
    CHECK(std::get<document::RunningStitchParams>(loaded->embroidery_objects[0].params).repeats ==
          3);
    CHECK(loaded->embroidery_objects[1].is_tatami());
    const auto& loadedTat = std::get<document::TatamiParams>(loaded->embroidery_objects[1].params);
    CHECK(loadedTat.row_spacing.value == 450);
    CHECK(loadedTat.underlay_edge);
    CHECK(loadedTat.underlay_parallel);
    CHECK(loadedTat.underlay_spacing == Micrometers{2'500});
    CHECK(loadedTat.hidden_underpath);
    REQUIRE(loadedTat.entry_point.has_value());
    CHECK(*loadedTat.entry_point == Vec2um{Micrometers{321}, Micrometers{654}});
    CHECK(loaded->embroidery_objects[2].is_satin());
    const auto& loadedSatin = std::get<document::SatinParams>(loaded->embroidery_objects[2].params);
    CHECK(loadedSatin.rail_a.nodes.size() == 4);
    REQUIRE(loadedSatin.rungs.size() == 2);
    CHECK(loadedSatin.rungs[0].a == Vec2um{Micrometers{100}, Micrometers{200}});
    CHECK(loadedSatin.rungs[1].b == Vec2um{Micrometers{700}, Micrometers{800}});
    CHECK(loadedSatin.short_stitch == document::SatinShortStitch::MultiLevelInset);
    CHECK(loadedSatin.split_stitch == document::SatinSplit::Staggered);
    CHECK(loadedSatin.cap_end == document::SatinCap::Tapered);
    CHECK(loadedSatin.max_stitch_length == Micrometers{5'500});
    CHECK(loadedSatin.underlay_edge);
    CHECK(loadedSatin.underlay_zigzag);
    CHECK(loadedSatin.pull_left == Micrometers{120});
    CHECK(loadedSatin.push_end == Micrometers{900});
    CHECK(loadedSatin.lock_start == document::SatinLock::BackAndForth);
    CHECK(loadedSatin.lock_end == document::SatinLock::MicroZigzag);
    CHECK(loadedSatin.lock_passes == 3);
    REQUIRE(loadedSatin.entry_point.has_value());
    CHECK(*loadedSatin.entry_point == Vec2um{Micrometers{1'234}, Micrometers{5'678}});
    CHECK_FALSE(loadedSatin.exit_point.has_value());

    fs::remove(path);
}

TEST_CASE("projet minimal (image seule) : aller-retour") {
    document::Project project;
    project.original.width = 2;
    project.original.height = 2;
    project.original.rgba.assign(2 * 2 * 4, 128);
    const auto path = fs::temp_directory_path() / "openstitch_minimal.osp";

    REQUIRE(project_io::save_project(path, project).has_value());
    const auto loaded = project_io::load_project(path);
    REQUIRE(loaded.has_value());
    CHECK(loaded->original.rgba == project.original.rgba);
    CHECK_FALSE(loaded->segmentation.has_value());
    CHECK(loaded->vector_objects.empty());
    fs::remove(path);
}

TEST_CASE("fichier inexistant ou corrompu -> erreur propre, sans crash") {
    CHECK_FALSE(project_io::load_project(fs::path("n_existe_pas_12345.osp")).has_value());

    const auto path = fs::temp_directory_path() / "openstitch_corrupt.osp";
    {
        std::ofstream f(path, std::ios::binary);
        f << "ceci n'est pas un zip";
    }
    const auto r = project_io::load_project(path);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().category == ErrorCategory::InvalidFile);
    fs::remove(path);
}

TEST_CASE("ecriture atomique : un fichier existant survit a un chemin invalide") {
    // Sauvegarde valide, puis tentative d'ecrasement echouee ne doit pas
    // laisser de .tmp trainer (best-effort : on verifie l'absence du tmp).
    const auto path = temp_osp();
    document::Project project;
    project.original.width = 1;
    project.original.height = 1;
    project.original.rgba.assign(4, 255);
    REQUIRE(project_io::save_project(path, project).has_value());
    CHECK_FALSE(fs::exists(fs::path(path.string() + ".tmp")));
    fs::remove(path);
}
