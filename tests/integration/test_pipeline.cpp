// SPDX-License-Identifier: Apache-2.0
// Test d'intégration : la chaîne complète, d'une image à un DST relu.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <map>
#include <numeric>

#include "openstitch/autodigitize/autodigitize.hpp"
#include "openstitch/formats/dst.hpp"
#include "openstitch/formats/svg.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/optimization/order.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/satin_coverage/coverage.hpp"
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
        std::uint8_t* px = img.rgba.data() + (static_cast<std::size_t>(y) * img.width + x) * 4;
        px[0] = r;
        px[1] = g;
        px[2] = b;
        px[3] = 255;
    };
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 60; ++x) {
            const double dx = x - 30;
            const double dy = y - 20;
            if (dx * dx + dy * dy <= 12 * 12) {
                set(x, y, 210, 40, 40); // disque rouge
            } else if (y >= 17 && y < 23) {
                set(x, y, 40, 40, 210); // bande bleue
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

} // namespace

TEST_CASE("chaine complete : image -> segmentation -> auto -> points -> DST -> relecture") {
    document::Project project;
    project.mm_per_px = Millimeters{0.5}; // 60 px -> 30 mm de large
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
    CHECK(stats.stitches > 50); // un vrai motif rempli

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
    for (auto& v : autoResult->vectors)
        project.vector_objects.push_back(std::move(v));
    for (auto& e : autoResult->embroideries)
        project.embroidery_objects.push_back(std::move(e));

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
    const fs::path fixture =
        fs::path{OPENSTITCH_TEST_SOURCE_DIR} / "tests" / "fixtures" / "tentabrode.png";
    const auto loaded = image::load_image(fixture);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->width > 1000);
    REQUIRE(loaded->height > 1300);

    document::Project project;
    project.original = subsample(*loaded, 8);
    project.mm_per_px = Millimeters{0.8};
    auto segmented =
        segmentation::segment(project.original, {.max_colors = 8, .min_region_px = 24});
    REQUIRE(segmented.has_value());
    REQUIRE(segmented->region_count() > 10);
    project.segmentation = std::move(*segmented);

    const auto options = autodigitize::AutoOptions{.mm_per_px = project.mm_per_px};
    auto digitized =
        autodigitize::auto_digitize(*project.segmentation, project.object_ids, options);
    REQUIRE(digitized.has_value());
    REQUIRE_FALSE(digitized->embroideries.empty());
    bool hasTopologicalSatin = false;
    for (const auto& embroidery : digitized->embroideries) {
        if (!embroidery.is_satin())
            continue;
        const auto& satin = std::get<document::SatinParams>(embroidery.params);
        hasTopologicalSatin = hasTopologicalSatin || satin.rungs.size() >= 2;
    }
    REQUIRE(hasTopologicalSatin);
    for (auto& vector : digitized->vectors)
        project.vector_objects.push_back(std::move(vector));
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

// DIAGNOSTIC TEMPORAIRE : couverture géométrique satin sur une image réelle
// et complexe (pas une forme synthétique) -- pour le "diagnostic total de
// l'état de l'Auto-Satin" demandé par l'utilisateur. Rejoue la même chaîne
// que "fixture tentabrode" ci-dessus (segment -> auto_digitize, mêmes
// réglages), puis calcule la couverture réelle de CHAQUE région ayant reçu
// au moins un objet satin, avec satin_coverage::analyze_satin_coverage.
TEST_CASE("DIAGNOSTIC TEMPORAIRE couverture satin (tentabrode)") {
    const fs::path fixture =
        fs::path{OPENSTITCH_TEST_SOURCE_DIR} / "tests" / "fixtures" / "tentabrode.png";
    const auto loaded = image::load_image(fixture);
    REQUIRE(loaded.has_value());

    document::Project project;
    project.original = subsample(*loaded, 8);
    project.mm_per_px = Millimeters{0.8};
    auto segmented =
        segmentation::segment(project.original, {.max_colors = 8, .min_region_px = 24});
    REQUIRE(segmented.has_value());
    project.segmentation = std::move(*segmented);

    const auto options = autodigitize::AutoOptions{.mm_per_px = project.mm_per_px};
    auto digitized =
        autodigitize::auto_digitize(*project.segmentation, project.object_ids, options);
    REQUIRE(digitized.has_value());

    std::map<ObjectId, std::vector<document::SatinParams>> satinBySourceVector;
    for (const auto& emb : digitized->embroideries) {
        if (emb.is_satin()) {
            satinBySourceVector[emb.source_vector].push_back(
                std::get<document::SatinParams>(emb.params));
        }
    }
    std::fprintf(stderr, "DIAG regions avec satin: %zu / %zu vecteurs\n",
                 satinBySourceVector.size(), digitized->vectors.size());

    // Le repli tatami (§ satin.md, "Repli tatami sur une branche auto-satin
    // rejetee") cree un vecteur SEPARE (meme source_region, id different) --
    // la couverture RÉELLEMENT livree pour la region d'origine, c'est satin
    // + repli combines, jamais le satin seul. Indexe les replis par
    // source_region pour les recombiner ci-dessous.
    std::map<RegionId, double> fallbackAreaByRegion;
    for (const auto& v : digitized->vectors) {
        if (v.name.find("zone non couverte") != std::string::npos && v.source_region) {
            fallbackAreaByRegion[*v.source_region] +=
                v.paths.empty() ? 0.0 : geometry::path_set_area_um2(v.paths.front()) / 1e6;
        }
    }
    std::fprintf(stderr, "DIAG replis tatami declenches: %zu (aire totale %.2fmm2)\n",
                 fallbackAreaByRegion.size(),
                 std::accumulate(fallbackAreaByRegion.begin(), fallbackAreaByRegion.end(), 0.0,
                                 [](double s, const auto& kv) { return s + kv.second; }));

    double sumTargetMm2 = 0.0;
    double sumSatinCoveredMm2 = 0.0;
    double sumFallbackMm2 = 0.0;
    double sumStillMissingMm2 = 0.0;
    std::size_t regionsPassed = 0;
    std::size_t regionsFailed = 0;
    for (const auto& [vecId, satins] : satinBySourceVector) {
        const auto vecIt =
            std::find_if(digitized->vectors.begin(), digitized->vectors.end(),
                         [&](const document::VectorObject& v) { return v.id == vecId; });
        if (vecIt == digitized->vectors.end() || vecIt->paths.empty())
            continue;
        const auto& main = *std::max_element(
            vecIt->paths.begin(), vecIt->paths.end(), [](const auto& a, const auto& b) {
                return geometry::path_set_area_um2(a) < geometry::path_set_area_um2(b);
            });

        std::vector<satin_coverage::SatinColumnInput> columns;
        for (const auto& sp : satins) {
            satin_coverage::SatinColumnInput in;
            in.rail_a = sp.rail_a;
            in.rail_b = sp.rail_b;
            for (const auto& r : sp.rungs)
                in.rungs.emplace_back(r.a, r.b);
            in.density = sp.density;
            columns.push_back(std::move(in));
        }
        const auto report = satin_coverage::analyze_satin_coverage(main, columns);
        if (!report) {
            std::fprintf(stderr, "DIAG vecteur %llu : erreur couverture : %s\n",
                         static_cast<unsigned long long>(vecId.value),
                         report.error().message.c_str());
            continue;
        }
        const double fallbackMm2 = vecIt->source_region
                                       ? fallbackAreaByRegion.count(*vecIt->source_region)
                                             ? fallbackAreaByRegion.at(*vecIt->source_region)
                                             : 0.0
                                       : 0.0;
        const double trueCoveredMm2 =
            std::min(report->target_area_mm2, report->covered_area_mm2 + fallbackMm2);
        const double trueRatio =
            report->target_area_mm2 > 0.0 ? trueCoveredMm2 / report->target_area_mm2 * 100.0 : 0.0;
        sumTargetMm2 += report->target_area_mm2;
        sumSatinCoveredMm2 += report->covered_area_mm2;
        sumFallbackMm2 += fallbackMm2;
        sumStillMissingMm2 += std::max(0.0, report->target_area_mm2 - trueCoveredMm2);
        (trueRatio >= 99.5) ? ++regionsPassed : ++regionsFailed;
        if (report->target_area_mm2 > 1.0) { // ignore le bruit sous-mm2
            std::fprintf(stderr,
                         "DIAG vecteur %llu : %zu colonne(s), cible=%.2fmm2 satin_seul=%.1f%% "
                         "repli=%.2fmm2 REEL=%.1f%% (coeur satin seul=%.1f%%, %zu trou(s) satin, "
                         "plus grand=%.2fmm2)\n",
                         static_cast<unsigned long long>(vecId.value), columns.size(),
                         report->target_area_mm2, report->raw_coverage_ratio * 100.0, fallbackMm2,
                         trueRatio, report->core_coverage_ratio * 100.0,
                         report->missing_regions.size(), report->largest_missing_area_mm2);
        }
    }
    const double aggSatinOnly =
        sumTargetMm2 > 0.0 ? sumSatinCoveredMm2 / sumTargetMm2 * 100.0 : 0.0;
    const double aggTrue =
        sumTargetMm2 > 0.0 ? (sumTargetMm2 - sumStillMissingMm2) / sumTargetMm2 * 100.0 : 0.0;
    std::fprintf(stderr,
                 "DIAG agrege : cible totale=%.1fmm2 -- satin seul=%.1f%% -- satin+repli tatami "
                 "(REEL)=%.1f%% -- encore manquant apres repli=%.2fmm2 -- regions (>=99.5%%) "
                 "PASS=%zu FAIL=%zu\n",
                 sumTargetMm2, aggSatinOnly, aggTrue, sumStillMissingMm2, regionsPassed,
                 regionsFailed);
    CHECK(false); // toujours en échec : diagnostic uniquement, jamais un test de non-régression
}
