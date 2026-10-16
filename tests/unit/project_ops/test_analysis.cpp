// SPDX-License-Identifier: Apache-2.0
// Outils d'analyse : fidélité, épaisseurs de fil, localisation, diagnostic, balayage, diagnostic de
// numérisation. Image synthétique déterministe : aplats rouge et bleu sur fond beige, un trait
// noir.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "openstitch/document/project.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/project_ops/analysis.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "openstitch/project_ops/sweep.hpp"

using namespace openstitch;
using namespace openstitch::project_ops;
namespace fs = std::filesystem;

namespace {

void fill_rect(image::Image& img, int x0, int y0, int x1, int y1, std::uint8_t r, std::uint8_t g,
               std::uint8_t b) {
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            std::uint8_t* px = img.rgba.data() + (static_cast<std::size_t>(y) * img.width + x) * 4;
            px[0] = r;
            px[1] = g;
            px[2] = b;
            px[3] = 255;
        }
    }
}

// 160 x 110 px : fond beige (la plus grande région), zone rouge à gauche, zone bleue à droite,
// trait noir de 3 px qui les traverse.
fs::path write_test_image(const std::string& name) {
    image::Image img;
    img.width = 160;
    img.height = 110;
    img.rgba.assign(static_cast<std::size_t>(img.width) * img.height * 4, 0);
    fill_rect(img, 0, 0, 160, 110, 230, 220, 200);
    fill_rect(img, 20, 20, 75, 90, 200, 60, 40);
    fill_rect(img, 85, 20, 140, 90, 30, 80, 170);
    fill_rect(img, 20, 54, 140, 57, 5, 5, 15);
    const auto png = image::encode_png(img);
    REQUIRE(png.has_value());
    const fs::path path = fs::temp_directory_path() / ("openstitch_analysis_" + name + ".png");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(png->data()),
              static_cast<std::streamsize>(png->size()));
    REQUIRE(out.good());
    return path;
}

DigitizeSettings test_settings() {
    DigitizeSettings s;
    s.max_colors = 4;
    s.min_region_px = 4;
    s.skip_background = 1; // fond beige ignoré (image trop petite pour la détection automatique)
    s.dark_lines = true;
    return s;
}

document::Project digitized(const std::string& name, const DigitizeSettings& settings) {
    auto created = create_empty_project({});
    REQUIRE(created.has_value());
    document::Project project = std::move(*created);
    REQUIRE(digitize_image_into(project, write_test_image(name), settings).has_value());
    return project;
}

Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

// Carré tatami de côté 2*half (µm), centré en (cx, cy) µm, couleur `rgb`.
ObjectId add_square(document::Project& project, std::int32_t cx, std::int32_t cy, std::int32_t half,
                    std::array<std::uint8_t, 3> rgb) {
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.name = "carre";
    geometry::PathSet set;
    for (const auto& p : {um(cx - half, cy - half), um(cx + half, cy - half),
                          um(cx + half, cy + half), um(cx - half, cy + half)}) {
        set.outer.nodes.push_back({p, geometry::NodeType::Corner, {}, {}});
    }
    vec.paths.push_back(set);
    document::EmbroideryObject obj;
    obj.id = project.object_ids.next();
    obj.name = "objet";
    obj.source_vector = vec.id;
    obj.rgb = rgb;
    obj.params = document::TatamiParams{};
    project.vector_objects.push_back(vec);
    project.embroidery_objects.push_back(obj);
    return obj.id;
}

bool has(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

} // namespace

TEST_CASE(
    "fidelity : un motif complet laisse peu de tissu visible, un objet masque en laisse beaucoup",
    "[project_ops][analysis]") {
    auto project = digitized("fid", test_settings());
    const auto full = measure_fidelity(project);
    REQUIRE(full.has_value());
    CHECK(full->motif_mm2 > 500.0);
    CHECK(full->motif_mm2 < 900.0); // deux zones de ~260 mm2 : le fond n'est pas brodé
    CHECK(full->interior_hole_ratio < 0.08);
    CHECK(has(full->json, kFidelitySchema));
    CHECK(has(full->text, "TISSU VISIBLE"));

    // Contrôle négatif : on masque le plus grand objet, sa zone devient du tissu visible.
    std::size_t biggest = 0;
    double biggestArea = 0.0;
    for (std::size_t i = 0; i < project.embroidery_objects.size(); ++i) {
        double area = 0.0;
        for (const auto& v : project.vector_objects) {
            if (v.id == project.embroidery_objects[i].source_vector) {
                for (const auto& set : v.paths) {
                    area += std::abs(geometry::signed_area_um2(set.outer)) / 1e6;
                }
            }
        }
        if (area > biggestArea) {
            biggestArea = area;
            biggest = i;
        }
    }
    project.embroidery_objects[biggest].visible = false;
    const auto holey = measure_fidelity(project);
    REQUIRE(holey.has_value());
    CHECK(holey->interior_hole_mm2 > 0.25 * biggestArea);
    CHECK(holey->interior_hole_mm2 > 10.0 * full->interior_hole_mm2);

    // Déterminisme et carte d'erreurs.
    FidelityOptions with_map;
    with_map.error_map = true;
    const auto a = measure_fidelity(project, with_map);
    const auto b = measure_fidelity(project, with_map);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK_FALSE(a->error_map_png.empty());
    CHECK(a->json == b->json);
    CHECK(a->error_map_png == b->error_map_png);
}

TEST_CASE("fidelity : refus nomme sans image source", "[project_ops][analysis]") {
    document::Project project;
    add_square(project, 0, 0, 5'000, {200, 30, 30});
    CHECK_FALSE(measure_fidelity(project).has_value());
}

TEST_CASE("density : deux objets superposes epaississent le fil", "[project_ops][analysis]") {
    document::Project one;
    add_square(one, 0, 0, 10'000, {200, 30, 30});
    document::Project two;
    add_square(two, 0, 0, 10'000, {200, 30, 30});
    add_square(two, 0, 0, 10'000, {30, 30, 200});
    DensityOptions options;
    options.heatmap = true;
    const auto a = measure_density(one, options);
    const auto b = measure_density(two, options);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(a->layers_max < 2.5);
    CHECK(b->layers_max > 1.6 * a->layers_max);
    CHECK_FALSE(b->heatmap_png.empty());
    CHECK(has(b->json, kDensitySchema));
    // Une zone chaude se signale dès que le seuil est franchi.
    options.hotspot_layers = 1.5;
    const auto hot = measure_density(two, options);
    REQUIRE(hot.has_value());
    CHECK(hot->hotspot_area_mm2 > 100.0);
}

TEST_CASE("locate : le dernier objet cousu est celui qu'on voit", "[project_ops][analysis]") {
    document::Project project;
    const auto first = add_square(project, 0, 0, 10'000, {200, 30, 30});
    const auto second = add_square(project, 0, 0, 10'000, {30, 30, 200});
    LocateQuery q;
    q.x_mm = 0.0;
    q.y_mm = 0.0;
    const auto at = locate_objects(project, q);
    REQUIRE(at.has_value());
    REQUIRE(at->objects.size() == 2);
    CHECK(at->objects.front() == second.value);
    CHECK(at->objects.back() == first.value);
    CHECK(has(at->json, kLocateSchema));
    // Loin du motif : du tissu.
    q.x_mm = 200.0;
    q.y_mm = 200.0;
    const auto away = locate_objects(project, q);
    REQUIRE(away.has_value());
    CHECK(away->objects.empty());
    CHECK(has(away->text, "tissu est visible"));
}

TEST_CASE("diagnose : fils quasi identiques, points hors de l'image, projet sain",
          "[project_ops][analysis]") {
    auto project = digitized("diag", test_settings());
    const auto healthy = diagnose_project(project);
    REQUIRE(healthy.has_value());
    CHECK(healthy->errors == 0);
    CHECK(has(healthy->json, kDiagnoseSchema));

    // Un objet très loin de l'image source : géométrie aberrante.
    auto far = project;
    add_square(far, 900'000, 0, 5'000, {200, 60, 40});
    const auto bad = diagnose_project(far);
    REQUIRE(bad.has_value());
    CHECK(bad->errors >= 1);
    CHECK(has(bad->json, "motif-hors-image"));

    // Deux fils presque identiques.
    document::Project twins;
    add_square(twins, 0, 0, 5'000, {8, 8, 15});
    add_square(twins, 20'000, 0, 5'000, {12, 18, 28});
    const auto dup = diagnose_project(twins);
    REQUIRE(dup.has_value());
    CHECK(has(dup->json, "fils-quasi-identiques"));
}

TEST_CASE("sweep : tableau, cles inconnues refusees, determinisme", "[project_ops][analysis]") {
    CHECK_FALSE(apply_digitize_setting(*std::make_unique<DigitizeSettings>(), "pas-une-option", "1")
                    .has_value());
    DigitizeSettings probe;
    CHECK_FALSE(apply_digitize_setting(probe, "island-cleanup", "beaucoup").has_value());
    REQUIRE(apply_digitize_setting(probe, "island-cleanup", "5").has_value());
    CHECK(probe.island_cleanup_mm2 == 5.0);
    REQUIRE(apply_digitize_setting(probe, "no-underpath", "1").has_value());
    CHECK_FALSE(probe.fill_hidden_underpath);

    SweepOptions options;
    options.base = test_settings();
    options.axes = {{"island-cleanup", {"0", "5"}}, {"curve-tolerance", {"0", "0.4"}}};
    options.sheet = true;
    options.sheet_columns = 2;
    options.sheet_tile_px = 160;
    const auto image = write_test_image("sweep");
    const auto report = run_sweep(image, options);
    REQUIRE(report.has_value());
    REQUIRE(report->rows.size() == 4);
    for (const auto& r : report->rows) {
        CHECK(r.ok);
        CHECK(r.stitches > 100);
    }
    CHECK(has(report->markdown, "island-cleanup=0 curve-tolerance=0"));
    CHECK(has(report->markdown, "island-cleanup=5 curve-tolerance=0.4"));
    CHECK(has(report->json, "openstitch.sweep/1"));
    CHECK_FALSE(report->sheet_png.empty());
    const auto again = run_sweep(image, options);
    REQUIRE(again.has_value());
    CHECK(again->json == report->json);
    CHECK(again->markdown == report->markdown);

    // Un axe inconnu est refusé avant tout calcul.
    options.axes = {{"inconnu", {"1"}}};
    CHECK_FALSE(run_sweep(image, options).has_value());
}

TEST_CASE("digitize --debug-dir ecrit masques, couverture et decisions",
          "[project_ops][analysis]") {
    const fs::path dir = fs::temp_directory_path() / "openstitch_analysis_debugdir";
    fs::remove_all(dir);
    DigitizeSettings s = test_settings();
    s.debug_dir = dir.string();
    s.dark_lines_bars_mm = 2.0;
    (void)digitized("dbg", s);
    for (const char* f :
         {"regions.png", "traits.png", "couverture.png", "fidelite.png", "decisions.json"}) {
        INFO(f);
        CHECK(fs::exists(dir / f));
        CHECK(fs::file_size(dir / f) > 20);
    }
    std::ifstream in(dir / "decisions.json");
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(has(text, "effective_lightness"));
    CHECK(has(text, "thread_rgb"));
}

// Invariants : quelle que soit la combinaison d'options de numérisation, le résultat reste sain.
// Ce test aurait attrapé tout de suite le défaut des poignées de Bézier (motif de 2 130 mm de large
// pour une image de 299 mm avec `--curve-tolerance 0.4 --dark-lines`).
namespace {

void check_invariants(const fs::path& image, double image_width_mm, double image_height_mm,
                      const DigitizeSettings& base) {
    SweepOptions options;
    options.base = base;
    options.axes = {{"curve-tolerance", {"0", "0.4"}},
                    {"dark-lines", {"0", "1"}},
                    {"satin-bands", {"0", "1"}},
                    {"no-underpath", {"0", "1"}}};
    const auto report = run_sweep(image, options);
    REQUIRE(report.has_value());
    REQUIRE(report->rows.size() == 16);
    for (const auto& r : report->rows) {
        INFO(r.label);
        REQUIRE(r.ok);
        CHECK(r.stitches > 0);
        CHECK(r.errors == 0);
        CHECK(r.width_mm <= image_width_mm + 6.0); // jamais plus large que l'image source
        CHECK(r.height_mm <= image_height_mm + 6.0);
        CHECK(r.hole_percent < 15.0); // le tissu ne domine jamais le motif
        CHECK(r.lost_percent < 25.0);
    }
    const auto again = run_sweep(image, options);
    REQUIRE(again.has_value());
    CHECK(again->json == report->json); // déterminisme de toute la chaîne
}

} // namespace

// Image de stress déterministe : fond beige, une centaine de formes de 2 à 40 px dans six couleurs
// (dont de minuscules îlots), traversées de traits sombres fins : beaucoup de petits contours.
fs::path write_stress_image(const std::string& name) {
    image::Image img;
    img.width = 360;
    img.height = 260;
    img.rgba.assign(static_cast<std::size_t>(img.width) * img.height * 4, 0);
    fill_rect(img, 0, 0, img.width, img.height, 230, 220, 200);
    std::uint64_t state = 0x2545F4914F6CDD1DULL;
    const auto next = [&state](std::uint32_t mod) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<std::uint32_t>(state >> 33) % mod;
    };
    const std::array<std::array<std::uint8_t, 3>, 6> colours = {{{200, 60, 40},
                                                                 {30, 80, 170},
                                                                 {250, 190, 20},
                                                                 {2, 38, 85},
                                                                 {250, 237, 198},
                                                                 {15, 100, 186}}};
    fill_rect(img, 20, 20, 340, 240, 30, 80, 170);
    for (int i = 0; i < 140; ++i) {
        const auto& c = colours[next(6)];
        const int w = 2 + static_cast<int>(next(i < 100 ? 24 : 6));
        const int h = 2 + static_cast<int>(next(i < 100 ? 24 : 6));
        const int x = 20 + static_cast<int>(next(320 - w));
        const int y = 20 + static_cast<int>(next(220 - h));
        for (int yy = y; yy < y + h; ++yy) {
            for (int xx = x; xx < x + w; ++xx) {
                const double dx = (xx - (x + w / 2.0)) / (w / 2.0);
                const double dy = (yy - (y + h / 2.0)) / (h / 2.0);
                if (dx * dx + dy * dy <= 1.0) { // ellipses
                    fill_rect(img, xx, yy, xx + 1, yy + 1, c[0], c[1], c[2]);
                }
            }
        }
    }
    for (int i = 0; i < 70; ++i) { // traits sombres fins, presque rectilignes
        const int x = 20 + static_cast<int>(next(300));
        const int y = 20 + static_cast<int>(next(220));
        const int len = 15 + static_cast<int>(next(60));
        const bool horizontal = next(2) == 0;
        const int slope = static_cast<int>(next(3)) - 1;
        for (int t = 0; t < len; ++t) {
            const int xx = horizontal ? x + t : x + t * slope / 2;
            const int yy = horizontal ? y + t * slope / 3 : y + t;
            if (xx > 20 && xx < 338 && yy > 20 && yy < 238) {
                fill_rect(img, xx, yy, xx + 2 + static_cast<int>(next(2)), yy + 2, 5, 5, 15);
            }
        }
    }
    const auto png = image::encode_png(img);
    REQUIRE(png.has_value());
    const fs::path path = fs::temp_directory_path() / ("openstitch_analysis_" + name + ".png");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(png->data()),
              static_cast<std::streamsize>(png->size()));
    REQUIRE(out.good());
    return path;
}

TEST_CASE("invariants : image de stress (petits contours), 16 combinaisons d'options",
          "[project_ops][invariants]") {
    DigitizeSettings base;
    base.max_colors = 7;
    base.min_region_px = 3;
    base.skip_background = 1;
    base.curve_tolerance_mm = 0.0;
    check_invariants(write_stress_image("stress"), 360 * 25.4 / 96.0, 260 * 25.4 / 96.0, base);
}

TEST_CASE("invariants : image synthetique, 16 combinaisons d'options",
          "[project_ops][invariants]") {
    DigitizeSettings base = test_settings();
    base.dark_lines = false; // l'axe `dark-lines` le règle
    // 160 x 110 px à 96 dpi.
    check_invariants(write_test_image("inv"), 160 * 25.4 / 96.0, 110 * 25.4 / 96.0, base);
}

TEST_CASE("invariants : fixture du depot, 16 combinaisons d'options", "[project_ops][invariants]") {
    const fs::path fixture = fs::path(OPENSTITCH_FIXTURES_DIR) / "tentabrode.png";
    const auto info = image::read_image_info(fixture);
    REQUIRE(info.has_value());
    DigitizeSettings base;
    base.max_colors = 6;
    base.dpi = 150.0;
    check_invariants(fixture, info->width_px * 25.4 / 150.0, info->height_px * 25.4 / 150.0, base);
}

TEST_CASE("flow fill converts elongated fills to directional with guides",
          "[project_ops][analysis]") {
    const auto count_directional = [](const document::Project& p) {
        std::size_t n = 0;
        for (const auto& e : p.embroidery_objects) {
            n += std::holds_alternative<document::DirectionalFillParams>(e.params) ? 1 : 0;
        }
        return n;
    };
    const auto digitize = [&](bool flow, double elongation) {
        auto settings = test_settings();
        settings.flow_fill = flow;
        settings.flow_min_area_mm2 = 1.0;
        settings.flow_min_elongation = elongation;
        auto created = create_empty_project({});
        REQUIRE(created.has_value());
        document::Project project = std::move(*created);
        const auto report = digitize_image_into(project, write_test_image("flow"), settings);
        REQUIRE(report.has_value());
        return std::pair{std::move(project), report->flow_fill};
    };
    const auto [off, offCount] = digitize(false, 0.0);
    CHECK(offCount == 0);
    CHECK(count_directional(off) == 0);

    const auto [forced, forcedCount] = digitize(true, 0.0);
    CHECK(forcedCount >= 1);
    CHECK(count_directional(forced) == forcedCount);
    for (const auto& e : forced.embroidery_objects) {
        if (const auto* d = std::get_if<document::DirectionalFillParams>(&e.params)) {
            CHECK_FALSE(d->guides.empty());
        }
    }
    const auto [strict, strictCount] = digitize(true, 1e9);
    CHECK(strictCount == 0);
}
