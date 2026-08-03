// SPDX-License-Identifier: Apache-2.0
// Test d'intégration : la chaîne complète, d'une image à un DST relu.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>

#include "openstitch/autodigitize/autodigitize.hpp"
#include "openstitch/formats/dst.hpp"
#include "openstitch/image/image.hpp"
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

// Réduction déterministe réservée au test : l'original complet reste versionné
// comme oracle, mais sa segmentation (>1,5 Mpx) rendrait la suite courante trop
// lente. Un pixel sur huit dans chaque axe conserve spirale, trous et branches
// tout en divisant le travail d'environ soixante-quatre fois.
image::Image subsample(const image::Image& source, int factor) {
    image::Image out;
    out.width = source.width / factor;
    out.height = source.height / factor;
    out.rgba.resize(static_cast<std::size_t>(out.width) * out.height * 4);
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            const auto src = (static_cast<std::size_t>(y * factor) * source.width + x * factor) * 4;
            const auto dst = (static_cast<std::size_t>(y) * out.width + x) * 4;
            std::copy_n(source.rgba.begin() + static_cast<std::ptrdiff_t>(src), 4,
                        out.rgba.begin() + static_cast<std::ptrdiff_t>(dst));
        }
    }
    return out;
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

TEST_CASE("fixture tentabrode : pipeline complexe deterministe et sans geometrie invalide") {
    const fs::path fixture = fs::path{OPENSTITCH_TEST_SOURCE_DIR} / "tests" / "fixtures" /
                             "tentabrode.png";
    const auto loaded = image::load_image(fixture);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->width > 1000);
    REQUIRE(loaded->height > 1300);

    document::Project project;
    project.original = subsample(*loaded, 8);
    project.mm_per_px = Millimeters{0.8};
    auto segmented = segmentation::segment(project.original, {.max_colors = 8, .min_region_px = 24});
    REQUIRE(segmented.has_value());
    REQUIRE(segmented->region_count() > 10);
    project.segmentation = std::move(*segmented);

    const auto options = autodigitize::AutoOptions{.mm_per_px = project.mm_per_px};
    auto digitized = autodigitize::auto_digitize(*project.segmentation, project.object_ids, options);
    REQUIRE(digitized.has_value());
    REQUIRE_FALSE(digitized->embroideries.empty());
    bool hasTopologicalSatin = false;
    for (const auto& embroidery : digitized->embroideries) {
        if (!embroidery.is_satin()) continue;
        const auto& satin = std::get<document::SatinParams>(embroidery.params);
        hasTopologicalSatin = hasTopologicalSatin || satin.rungs.size() >= 2;
    }
    REQUIRE(hasTopologicalSatin);
    for (auto& vector : digitized->vectors) project.vector_objects.push_back(std::move(vector));
    for (auto& embroidery : digitized->embroideries)
        project.embroidery_objects.push_back(std::move(embroidery));

    const auto first = stitch_generation::generate_sequence(project);
    const auto second = stitch_generation::generate_sequence(project);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE_FALSE(first->commands.empty());
    CHECK(first->commands == second->commands);

    const auto stats = stitch::compute_stats(*first);
    CHECK(stats.stitches > 1000);
    CHECK(stats.bounds.min.x.value <= stats.bounds.max.x.value);
    CHECK(stats.bounds.min.y.value <= stats.bounds.max.y.value);
}

TEST_CASE("DIAGNOSTIC TEMPORAIRE osp utilisateur") {
    const auto loaded = project_io::load_project("C:/Users/zache/Pictures/Cathebrode.osp");
    REQUIRE(loaded.has_value());
    const auto& project = *loaded;
    UNSCOPED_INFO("vector_objects=" << project.vector_objects.size()
                                     << " embroidery_objects=" << project.embroidery_objects.size());
    int nSatin = 0, nTatami = 0, nRunning = 0, nSatinWithRungs = 0, nInvisible = 0;
    for (const auto& e : project.embroidery_objects) {
        if (!e.visible) ++nInvisible;
        if (std::holds_alternative<document::SatinParams>(e.params)) {
            ++nSatin;
            const auto& sp = std::get<document::SatinParams>(e.params);
            if (sp.rungs.size() >= 2) ++nSatinWithRungs;
        } else if (std::holds_alternative<document::TatamiParams>(e.params)) {
            ++nTatami;
        } else if (std::holds_alternative<document::RunningStitchParams>(e.params)) {
            ++nRunning;
        }
    }
    UNSCOPED_INFO("satin=" << nSatin << " (avec barreaux=" << nSatinWithRungs << ") tatami="
                            << nTatami << " running=" << nRunning << " invisible=" << nInvisible);

    const auto seq = stitch_generation::generate_sequence(project);
    UNSCOPED_INFO("generate_sequence ok=" << seq.has_value());
    if (!seq.has_value()) {
        UNSCOPED_INFO("erreur: " << seq.error().message);
    } else {
        const auto stats = stitch::compute_stats(*seq);
        UNSCOPED_INFO("stitches=" << stats.stitches << " jumps=" << stats.jumps);
    }
    CHECK(false);
}
