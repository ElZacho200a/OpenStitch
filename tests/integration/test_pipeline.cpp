// SPDX-License-Identifier: Apache-2.0
// Test d'intégration : la chaîne complète, d'une image à un DST relu.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "openstitch/autodigitize/autodigitize.hpp"
#include "openstitch/formats/dst.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/segmentation/segmentation.hpp"
#include "openstitch/stitch_generation/generate.hpp"

using namespace openstitch;
namespace fs = std::filesystem;

namespace {

// Logo synthétique bicolore : disque rouge sur bande bleue, fond transparent.
image::Image make_logo() {
    image::Image img;
    img.width = 60;
    img.height = 40;
    img.rgba.assign(static_cast<std::size_t>(img.width) * img.height * 4, 0);
    const auto set = [&](int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        std::uint8_t* px = img.rgba.data() +
                           (static_cast<std::size_t>(y) * img.width + x) * 4;
        px[0] = r; px[1] = g; px[2] = b; px[3] = 255;
    };
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 60; ++x) {
            const double dx = x - 30;
            const double dy = y - 20;
            if (dx * dx + dy * dy <= 12 * 12) {
                set(x, y, 210, 40, 40);   // disque rouge
            } else if (y >= 17 && y < 23) {
                set(x, y, 40, 40, 210);   // bande bleue
            }
        }
    }
    return img;
}

}  // namespace

TEST_CASE("chaine complete : image -> segmentation -> auto -> points -> DST -> relecture") {
    document::Project project;
    project.mm_per_px = Millimeters{0.5};  // 60 px -> 30 mm de large
    project.original = make_logo();

    // Segmentation.
    auto seg = segmentation::segment(project.original, {.max_colors = 3, .min_region_px = 8});
    REQUIRE(seg.has_value());
    CHECK(seg->region_count() >= 2);
    project.segmentation = std::move(*seg);

    // Numérisation automatique -> objets éditables.
    autodigitize::AutoOptions opts;
    opts.mm_per_px = project.mm_per_px;
    auto autoResult = autodigitize::auto_digitize(*project.segmentation, project.object_ids, opts);
    REQUIRE(autoResult.has_value());
    REQUIRE_FALSE(autoResult->embroideries.empty());
    for (auto& v : autoResult->vectors) {
        project.vector_objects.push_back(std::move(v));
    }
    for (auto& e : autoResult->embroideries) {
        project.embroidery_objects.push_back(std::move(e));
    }

    // Génération des points.
    auto sequence = stitch_generation::generate_sequence(project);
    REQUIRE(sequence.has_value());
    const auto stats = stitch::compute_stats(*sequence);
    CHECK(stats.stitches > 50);  // un vrai motif rempli

    // Export DST puis relecture.
    auto bytes = formats::encode_dst(*sequence);
    REQUIRE(bytes.has_value());
    auto decoded = formats::decode_dst(*bytes);
    REQUIRE(decoded.has_value());
    const auto decodedStats = stitch::compute_stats(*decoded);

    // Le nombre de points cousus survit à l'aller-retour DST.
    CHECK(decodedStats.stitches == stats.stitches);
    // Le motif tient dans un cadre raisonnable (30 x 20 mm environ).
    const double wMm = (decodedStats.bounds.max.x.value - decodedStats.bounds.min.x.value) / 1000.0;
    CHECK(wMm > 5.0);
    CHECK(wMm < 40.0);
}

TEST_CASE("chaine complete : projet sauvegarde, recharge, regenere a l'identique") {
    document::Project project;
    project.mm_per_px = Millimeters{0.5};
    project.original = make_logo();
    auto seg = segmentation::segment(project.original, {.max_colors = 3, .min_region_px = 8});
    REQUIRE(seg.has_value());
    project.segmentation = std::move(*seg);
    auto autoResult = autodigitize::auto_digitize(*project.segmentation, project.object_ids,
                                                  {.mm_per_px = project.mm_per_px});
    REQUIRE(autoResult.has_value());
    for (auto& v : autoResult->vectors) project.vector_objects.push_back(std::move(v));
    for (auto& e : autoResult->embroideries) project.embroidery_objects.push_back(std::move(e));

    const auto before = stitch::compute_stats(*stitch_generation::generate_sequence(project));

    const auto path = fs::temp_directory_path() / "openstitch_pipeline.osp";
    REQUIRE(project_io::save_project(path, project).has_value());
    auto reloaded = project_io::load_project(path);
    REQUIRE(reloaded.has_value());
    fs::remove(path);

    // Les points regeneres depuis le projet recharge sont identiques.
    auto after = stitch_generation::generate_sequence(*reloaded);
    REQUIRE(after.has_value());
    const auto afterStats = stitch::compute_stats(*after);
    CHECK(afterStats.stitches == before.stitches);
    CHECK(afterStats.color_changes == before.color_changes);
}
