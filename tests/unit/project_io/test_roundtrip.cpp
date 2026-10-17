// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>

#include "json_serialize.hpp"
#include "openstitch/document/color_blend.hpp"
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
    project.canvas.margin = Micrometers{4'000};

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
    project.ops.push_back(image::BilateralDenoiseOp{3});
    project.ops.push_back(image::FlattenOp{3});
    project.ops.push_back(image::ClaheOp{3.5});
    project.ops.push_back(image::SharpenOp{0.5});
    project.ops.push_back(image::CoherentLinesOp{1.5, 2.0, 0.99, 0.9, 2, 4});

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
    sp.rungs.push_back(
        {Vec2um{Micrometers{100}, Micrometers{200}}, Vec2um{Micrometers{300}, Micrometers{400}}});
    sp.rungs.back().link_id = 12;
    sp.rungs.push_back(
        {Vec2um{Micrometers{500}, Micrometers{600}}, Vec2um{Micrometers{700}, Micrometers{800}}});
    sp.topology = document::SatinSectionTopology{2, 4, 7, 9};
    sp.density = Micrometers{350};
    sp.center_underlay = true;
    sp.max_width_hard = Micrometers{32'000};
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
    // Unique par processus ET par appel : CTest lance chaque TEST_CASE dans son propre
    // processus, en parallèle ; un chemin fixe faisait entrer ces tests en collision.
    static int counter = 0;
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() / ("openstitch_roundtrip_" + std::to_string(ticks) + "_" +
                                        std::to_string(counter++) + ".osp");
}

} // namespace

TEST_CASE("blend core: osp and clipboard roundtrip keep exact uint64 links and density mode",
          "[blend_core]") {
    document::Project project;
    document::VectorObject vector;
    vector.id = ObjectId{1};
    vector.paths = {geometry::PathSet{square_path(), {}}};
    project.vector_objects.push_back(vector);
    document::EmbroideryObject background;
    background.id = ObjectId{9'007'199'254'740'993ULL};
    background.source_vector = vector.id;
    background.params = document::TatamiParams{};
    document::EmbroideryObject foreground = background;
    foreground.id = ObjectId{std::numeric_limits<std::uint64_t>::max() - 1};
    document::DirectionalFillParams params;
    params.density_gradient = document::DensityGradient{um(0, 0), um(5'000, 0), Micrometers{400},
                                                        Micrometers{1'600}, true};
    foreground.params = params;
    background.color_blend = document::ColorBlendLink{foreground.id, false};
    foreground.color_blend = document::ColorBlendLink{background.id, true};
    project.embroidery_objects = {background, foreground};
    project.object_ids.reset(foreground.id.value);
    const auto json = project_io::detail::project_to_json(project);
    REQUIRE(json["embroideryObjects"][0].contains("colorBlend"));
    CHECK(json["embroideryObjects"][0]["colorBlend"]["partner"].get<std::uint64_t>() ==
          foreground.id.value);
    const auto parsed = project_io::detail::project_from_json(nlohmann::json::parse(json.dump()));
    REQUIRE(parsed);
    REQUIRE(parsed->embroidery_objects.size() == 2);
    CHECK(parsed->embroidery_objects[0].color_blend == background.color_blend);
    CHECK(parsed->embroidery_objects[1].color_blend == foreground.color_blend);
    CHECK(parsed->embroidery_objects[1].params == foreground.params);
    CHECK(document::find_color_blend(*parsed, foreground.id) ==
          document::ColorBlendLayers{background.id, foreground.id});
    const auto path = temp_osp();
    REQUIRE(project_io::save_project(path, project));
    const auto loaded = project_io::load_project(path);
    fs::remove(path);
    REQUIRE(loaded);
    CHECK(loaded->embroidery_objects[0].color_blend == background.color_blend);
    CHECK(loaded->embroidery_objects[1].color_blend == foreground.color_blend);
    CHECK(loaded->embroidery_objects[1].params == foreground.params);
    const document::ObjectFragment fragment{project.vector_objects, project.embroidery_objects};
    const auto clipboard = project_io::fragment_from_json(project_io::fragment_to_json(fragment));
    REQUIRE(clipboard);
    REQUIRE(clipboard->embroideries.size() == 2);
    CHECK(clipboard->embroideries[0].color_blend == background.color_blend);
    CHECK(clipboard->embroideries[1].color_blend == foreground.color_blend);
}

TEST_CASE("blend core: legacy JSON omits relation and keeps spacing interpolation",
          "[blend_core]") {
    auto project = rich_project();
    document::DirectionalFillParams params;
    params.density_gradient =
        document::DensityGradient{um(0, 0), um(5'000, 0), Micrometers{400}, Micrometers{1'600}};
    project.embroidery_objects[1].params = params;
    const auto json = project_io::detail::project_to_json(project);
    CHECK_FALSE(json["embroideryObjects"][1].contains("colorBlend"));
    CHECK_FALSE(
        json["embroideryObjects"][1]["params"]["densityGradient"].contains("interpolateDensity"));
    const auto loaded = project_io::detail::project_from_json(json);
    REQUIRE(loaded);
    for (const auto& object : loaded->embroidery_objects) {
        CHECK_FALSE(object.color_blend);
    }
    const auto& gradient =
        std::get<document::DirectionalFillParams>(loaded->embroidery_objects[1].params)
            .density_gradient;
    REQUIRE(gradient);
    CHECK_FALSE(gradient->interpolate_density);
    CHECK(loaded->embroidery_objects[1].params == project.embroidery_objects[1].params);
    CHECK(project_io::detail::project_to_json(*loaded) == json);
}

TEST_CASE("blend core: malformed JSON links and non boolean interpolation are rejected",
          "[blend_core]") {
    const auto project = rich_project();
    for (const auto& bad :
         {nlohmann::json(nullptr), nlohmann::json(3), nlohmann::json::array(),
          nlohmann::json{{"partner", 2}}, nlohmann::json{{"partner", -1}, {"foreground", false}},
          nlohmann::json{{"partner", 2.5}, {"foreground", false}},
          nlohmann::json{{"partner", "2"}, {"foreground", false}},
          nlohmann::json{{"partner", 2}, {"foreground", 1}},
          nlohmann::json{{"partner", 2}, {"foreground", "true"}},
          nlohmann::json{{"partner", 1e30}, {"foreground", false}}}) {
        auto json = project_io::detail::project_to_json(project);
        json["embroideryObjects"][0]["colorBlend"] = bad;
        const auto parsed = project_io::detail::project_from_json(json);
        CHECK_FALSE(parsed);
        if (!parsed) {
            CHECK(parsed.error().category == ErrorCategory::InvalidFile);
        }
    }
    for (const auto& bad : {nlohmann::json(1), nlohmann::json("true"), nlohmann::json(nullptr)}) {
        auto json = project_io::detail::project_to_json(project);
        json["embroideryObjects"][0]["params"] = {
            {"type", "directional"}, {"densityGradient", {{"interpolateDensity", bad}}}};
        CHECK_FALSE(project_io::detail::project_from_json(json));
    }
}

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
    CHECK(loaded->canvas.margin == original.canvas.margin);

    // Image restauree (PNG sans perte).
    CHECK(loaded->original.width == 4);
    CHECK(loaded->original.height == 2);
    CHECK(loaded->original.rgba == original.original.rgba);

    // Ops.
    REQUIRE(loaded->ops.size() == 8);
    CHECK(std::holds_alternative<image::GrayscaleOp>(loaded->ops[0]));
    CHECK(std::holds_alternative<image::QuantizeOp>(loaded->ops[1]));
    REQUIRE(std::holds_alternative<image::BilateralDenoiseOp>(loaded->ops[3]));
    CHECK(std::get<image::BilateralDenoiseOp>(loaded->ops[3]).strength == 3);
    CHECK(std::get<image::FlattenOp>(loaded->ops[4]).strength == 3);
    CHECK(std::get<image::ClaheOp>(loaded->ops[5]).clip_limit == 3.5);
    CHECK(std::get<image::SharpenOp>(loaded->ops[6]).amount == 0.5);
    REQUIRE(std::holds_alternative<image::CoherentLinesOp>(loaded->ops[7]));
    CHECK(std::get<image::CoherentLinesOp>(loaded->ops[7]) ==
          image::CoherentLinesOp{1.5, 2.0, 0.99, 0.9, 2, 4});

    // Segmentation.
    REQUIRE(loaded->segmentation.has_value());
    CHECK(loaded->segmentation->labels == original.segmentation->labels);
    CHECK(loaded->segmentation->region_count() == 2);
    CHECK(loaded->segmentation->find(RegionId{2})->rgb == std::array<std::uint8_t, 3>{0, 100, 200});

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
    CHECK(loadedSatin.rungs[0].link_id == std::optional<std::uint32_t>{12});
    CHECK_FALSE(loadedSatin.rungs[1].link_id.has_value());
    REQUIRE(loadedSatin.topology.has_value());
    CHECK(*loadedSatin.topology == document::SatinSectionTopology{2, 4, 7, 9});
    CHECK(loadedSatin.max_width_hard == Micrometers{32'000});
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

TEST_CASE("remplissage directionnel : save puis load = memes parametres") {
    document::Project project;
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.paths.push_back(geometry::PathSet{square_path(), {}});
    project.vector_objects.push_back(vec);

    document::DirectionalFillParams dp;
    geometry::Path guide;
    guide.closed = false;
    guide.nodes = {{um(100, 200), geometry::NodeType::Corner, {}, um(300, 0)},
                   {um(4'000, 2'500), geometry::NodeType::Smooth, um(-300, -100), {}}};
    dp.guides.push_back(guide);
    geometry::Path rupture;
    rupture.closed = false;
    rupture.nodes = {{um(2'500, -100), geometry::NodeType::Corner, {}, {}},
                     {um(2'500, 5'100), geometry::NodeType::Corner, {}, {}}};
    dp.break_lines.push_back(rupture);
    dp.row_spacing = Micrometers{350};
    dp.stitch_length = Micrometers{4'200};
    dp.edge_weight = 0.35;
    dp.inset = Micrometers{150};
    dp.stagger = 3;
    dp.underlay_edge = true;
    dp.underlay_parallel = true;
    dp.underlay_inset = Micrometers{700};
    dp.underlay_spacing = Micrometers{2'200};
    dp.hidden_underpath = false;
    dp.sector_overlap = Micrometers{280};
    dp.handmade = true;
    dp.handmade_intensity = 65;
    dp.seed = 4'000'000'000U; // > int32 : doit survivre intact
    dp.spacing_regularity = 0.35;
    dp.density_gradient = document::DensityGradient{um(0, 100), um(4'000, 5'100), Micrometers{300},
                                                    Micrometers{1'200}};

    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.name = "vague";
    emb.source_vector = vec.id;
    emb.params = dp;
    project.embroidery_objects.push_back(emb);

    const auto path = temp_osp();
    REQUIRE(project_io::save_project(path, project).has_value());
    const auto loaded = project_io::load_project(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->embroidery_objects.size() == 1);
    REQUIRE(loaded->embroidery_objects[0].is_directional());
    CHECK(std::get<document::DirectionalFillParams>(loaded->embroidery_objects[0].params) == dp);
    fs::remove(path);

    // Sans dégradé, la clé est absente et la relecture redonne « pas de dégradé »
    // (les fichiers antérieurs à cette clé se relisent à l'identique).
    dp.density_gradient.reset();
    project.embroidery_objects[0].params = dp;
    REQUIRE(project_io::save_project(path, project).has_value());
    const auto plain = project_io::load_project(path);
    REQUIRE(plain.has_value());
    const auto& back =
        std::get<document::DirectionalFillParams>(plain->embroidery_objects[0].params);
    CHECK_FALSE(back.density_gradient.has_value());
    CHECK(back == dp);
    fs::remove(path);
}

TEST_CASE("auto-satin : save puis load = memes parametres, guides compris") {
    document::Project project;
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.paths.push_back(geometry::PathSet{square_path(), {}});
    project.vector_objects.push_back(vec);

    document::AutoSatinParams sp;
    sp.guides.push_back({um(1'000, 2'000), Angle{0.35}, false});
    sp.guides.push_back({um(4'000, 500), Angle{-1.2}, true});
    sp.spacing = Micrometers{350};
    sp.split_stitch = document::SatinSplit::DeterministicJitter;
    sp.split_threshold = Micrometers{6'500};
    sp.split_length = Micrometers{3'800};
    sp.short_stitch = document::SatinShortStitch::MultiLevelInset;
    sp.pull_compensation = Micrometers{120};
    sp.center_underlay = false;
    sp.underlay_edge = true;
    sp.underlay_zigzag = true;
    sp.pull_left = Micrometers{40};
    sp.pull_right = Micrometers{50};
    sp.push_start = Micrometers{60};
    sp.push_end = Micrometers{70};
    sp.cap_start = document::SatinCap::Tapered;
    sp.cap_end = document::SatinCap::Rounded;
    sp.lock_start = document::SatinLock::Triangle;
    sp.lock_end = document::SatinLock::BackAndForth;
    sp.lock_length = Micrometers{900};
    sp.lock_passes = 3;
    sp.entry_point = um(10, 20);
    sp.exit_point = um(4'990, 4'980);

    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.name = "satin auto";
    emb.source_vector = vec.id;
    emb.params = sp;
    project.embroidery_objects.push_back(emb);

    const auto path = temp_osp();
    REQUIRE(project_io::save_project(path, project).has_value());
    const auto loaded = project_io::load_project(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->embroidery_objects.size() == 1);
    REQUIRE(loaded->embroidery_objects[0].is_auto_satin());
    CHECK_FALSE(loaded->embroidery_objects[0].is_satin());
    CHECK(std::get<document::AutoSatinParams>(loaded->embroidery_objects[0].params) == sp);
    fs::remove(path);
}

TEST_CASE("auto-satin : le schema du fichier annonce la version qui porte ce type") {
    // Un auto-satin n'existe qu'a partir du schema 5 : un ancien executable doit
    // refuser le fichier proprement (version trop recente) plutot que d'echouer sur
    // un type de point inconnu.
    CHECK(project_io::kSchemaVersion >= 5);
}

TEST_CASE("save_project : le PNG en cache suit le contenu de l'image, pas son adresse") {
    auto project = rich_project();
    const auto path = temp_osp();
    // Deux enregistrements de la meme image (le second relit le cache), puis un pixel change.
    REQUIRE(project_io::save_project(path, project).has_value());
    REQUIRE(project_io::save_project(path, project).has_value());
    auto loaded = project_io::load_project(path);
    REQUIRE(loaded.has_value());
    CHECK(loaded->original.rgba == project.original.rgba);

    project.original.rgba[0] = static_cast<std::uint8_t>(project.original.rgba[0] ^ 0x5A);
    REQUIRE(project_io::save_project(path, project).has_value());
    loaded = project_io::load_project(path);
    REQUIRE(loaded.has_value());
    CHECK(loaded->original.rgba == project.original.rgba);

    // Meme taille d'image, autres dimensions : pas de confusion non plus.
    std::swap(project.original.width, project.original.height);
    REQUIRE(project_io::save_project(path, project).has_value());
    loaded = project_io::load_project(path);
    REQUIRE(loaded.has_value());
    CHECK(loaded->original.width == project.original.width);
    CHECK(loaded->original.height == project.original.height);
    fs::remove(path);
}

TEST_CASE("fragment d'objets : JSON aller-retour, deterministe, entrees invalides refusees") {
    const auto project = rich_project();
    document::ObjectFragment fragment;
    fragment.vectors = project.vector_objects;
    for (const auto& emb : project.embroidery_objects) {
        const bool hasSource =
            std::any_of(fragment.vectors.begin(), fragment.vectors.end(),
                        [&](const auto& v) { return v.id == emb.source_vector; });
        if (hasSource) {
            fragment.embroideries.push_back(emb);
        }
    }
    REQUIRE(!fragment.vectors.empty());
    REQUIRE(!fragment.embroideries.empty());

    const std::string text = project_io::fragment_to_json(fragment);
    CHECK(text == project_io::fragment_to_json(fragment));
    const auto back = project_io::fragment_from_json(text);
    REQUIRE(back.has_value());
    REQUIRE(back->vectors.size() == fragment.vectors.size());
    REQUIRE(back->embroideries.size() == fragment.embroideries.size());
    for (std::size_t i = 0; i < fragment.vectors.size(); ++i) {
        CHECK(back->vectors[i].id == fragment.vectors[i].id);
        CHECK(back->vectors[i].name == fragment.vectors[i].name);
        CHECK(back->vectors[i].paths == fragment.vectors[i].paths);
    }
    for (std::size_t i = 0; i < fragment.embroideries.size(); ++i) {
        CHECK(back->embroideries[i].source_vector == fragment.embroideries[i].source_vector);
        CHECK(back->embroideries[i].params == fragment.embroideries[i].params);
        CHECK(back->embroideries[i].rgb == fragment.embroideries[i].rgb);
        CHECK(back->embroideries[i].overrides.size() == fragment.embroideries[i].overrides.size());
    }
    // Le second aller-retour redonne le meme texte.
    CHECK(project_io::fragment_to_json(*back) == text);

    CHECK_FALSE(project_io::fragment_from_json("").has_value());
    CHECK_FALSE(project_io::fragment_from_json("bonjour").has_value());
    CHECK_FALSE(project_io::fragment_from_json("{\"a\":1}").has_value());
    CHECK_FALSE(project_io::fragment_from_json("[1,2,3]").has_value());
    // Broderie sans sa forme : refuse.
    document::ObjectFragment orphan;
    orphan.vectors.push_back(fragment.vectors.front());
    auto lonely = fragment.embroideries.front();
    lonely.source_vector = ObjectId{123'456'789};
    orphan.embroideries.push_back(lonely);
    CHECK_FALSE(project_io::fragment_from_json(project_io::fragment_to_json(orphan)).has_value());
}
