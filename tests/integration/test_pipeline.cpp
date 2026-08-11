// SPDX-License-Identifier: Apache-2.0
// Test d'intégration : la chaîne complète, d'une image à un DST relu.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <map>

#include "openstitch/autodigitize/autodigitize.hpp"
#include "openstitch/formats/dst.hpp"
#include "openstitch/formats/svg.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/optimization/order.hpp"
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

// DIAGNOSTIC TEMPORAIRE : analyse complète chaîne brute (image -> segmentation
// -> auto_digitize -> points) sur l'image GISTRE.png ELLE-MÊME, avec les
// réglages PAR DÉFAUT de la boîte de dialogue Segmenter (main_window.cpp :
// max_colors=8, min_region_px=16, smoothing_radius_px=3) et le mm_per_px du
// projet réel de l'utilisateur (0,1245 mm/px, cadre 200x200 mm) -- pas la
// segmentation déjà stockée dans le .osp, contrairement au diagnostic
// "pointes fines" ci-dessous : celui-ci rejoue TOUTE la chaîne pour auditer
// segmentation/vectorisation, pas seulement auto-satin.
TEST_CASE("DIAGNOSTIC TEMPORAIRE chaine brute (GISTRE.png)") {
    const auto loaded = image::load_image("C:/Users/zache/Pictures/GISTRE.png");
    REQUIRE(loaded.has_value());
    // Alpha d'un pixel de fond (coin haut-gauche, hors du sceau) : si l'image
    // source n'a pas de canal alpha, OpenCV le remplit à 255 (opaque) --
    // vérifie si le fond "transparent" attendu par la segmentation
    // (labels[i]==0 pour alpha==0) existe réellement ou non pour ce fichier.
    std::fprintf(stderr, "DIAG image: %dx%d source_had_alpha=%d alpha(0,0)=%d\n", loaded->width,
                loaded->height, loaded->source_had_alpha ? 1 : 0, loaded->rgba[3]);

    auto seg = segmentation::segment(
        *loaded, {.max_colors = 8, .min_region_px = 16, .smoothing_radius_px = 3});
    REQUIRE(seg.has_value());

    std::size_t liveRegions = 0;
    std::size_t totalPixels = 0;
    std::vector<std::pair<std::size_t, RegionId>> bySize;  // (pixels, id)
    for (const auto& slot : seg->region_slots) {
        if (!slot) continue;
        ++liveRegions;
        totalPixels += slot->pixel_count;
        bySize.emplace_back(slot->pixel_count, slot->id);
    }
    std::sort(bySize.begin(), bySize.end(), std::greater<>());
    std::fprintf(stderr, "DIAG regions vivantes=%zu pixels segmentes=%zu image totale=%d\n",
                liveRegions, totalPixels, loaded->width * loaded->height);
    for (std::size_t i = 0; i < bySize.size() && i < 10; ++i) {
        const auto* region = seg->find(bySize[i].second);
        std::fprintf(stderr, "DIAG top%zu: id=%llu pixels=%zu (%.1f%%) rgb=(%d,%d,%d)\n", i,
                    static_cast<unsigned long long>(bySize[i].second.value), bySize[i].first,
                    100.0 * static_cast<double>(bySize[i].first) / static_cast<double>(totalPixels),
                    region ? region->rgb[0] : -1, region ? region->rgb[1] : -1,
                    region ? region->rgb[2] : -1);
    }

    document::Project project;
    project.mm_per_px = Millimeters{0.12445550715619166};
    project.original = *loaded;
    project.segmentation = std::move(*seg);

    // Comme main_window.cpp::autoDigitize() coche desormais par defaut quand
    // l'image source n'a pas de canal alpha (§ audit ci-dessus).
    const autodigitize::AutoOptions options{.mm_per_px = project.mm_per_px,
                                            .skip_largest_region = !loaded->source_had_alpha};
    const auto result =
        autodigitize::auto_digitize(*project.segmentation, project.object_ids, options);
    REQUIRE(result.has_value());
    for (auto v : result->vectors) project.vector_objects.push_back(std::move(v));
    for (auto e : result->embroideries) project.embroidery_objects.push_back(std::move(e));

    int nSatin = 0, nTatami = 0, nRunning = 0, nSatinDegenerate = 0;
    double maxObjectAreaMm2 = 0.0;
    for (const auto& e : project.embroidery_objects) {
        if (auto* sp = std::get_if<document::SatinParams>(&e.params)) {
            ++nSatin;
            for (const auto& rung : sp->rungs) {
                if (length_um(rung.a - rung.b) < 290.0) {
                    ++nSatinDegenerate;
                    break;
                }
            }
        } else if (std::holds_alternative<document::TatamiParams>(e.params)) {
            ++nTatami;
        } else if (std::holds_alternative<document::RunningStitchParams>(e.params)) {
            ++nRunning;
        }
        if (const auto* vec = project.findObject(e.source_vector)) {
            for (const auto& set : vec->paths) {
                double area = std::abs(geometry::signed_area_um2(set.outer)) / 1e6;
                for (const auto& hole : set.holes) area -= std::abs(geometry::signed_area_um2(hole)) / 1e6;
                maxObjectAreaMm2 = std::max(maxObjectAreaMm2, area);
            }
        }
    }
    UNSCOPED_INFO("embroidery: satin=" << nSatin << " (dont barreau<0,29mm=" << nSatinDegenerate
                                       << ") tatami=" << nTatami << " running=" << nRunning);
    UNSCOPED_INFO("plus grand objet (aire nette): " << maxObjectAreaMm2 << " mm2 (canevas="
                                                    << (200.0 * 200.0) << " mm2)");

    const auto sequence = stitch_generation::generate_sequence(project);
    UNSCOPED_INFO("generate_sequence ok=" << sequence.has_value());
    if (sequence.has_value()) {
        const auto stats = stitch::compute_stats(*sequence);
        UNSCOPED_INFO("stitches=" << stats.stitches << " jumps=" << stats.jumps
                                  << " color_changes=" << stats.color_changes);
        const auto svgResult = formats::write_svg_file(
            "C:/Users/zache/Pictures/GISTRE_result.svg", *sequence);
        UNSCOPED_INFO("svg ecrit=" << svgResult.has_value());

        // Barres épaisses visibles dans le SVG : objets contribuant le plus de
        // points, pour identifier si un fill particulier "balaie" mal une
        // forme large plutôt qu'un problème d'ORDRE entre objets.
        std::map<std::uint64_t, std::size_t> stitchesPerObject;
        for (const auto& cmd : sequence->commands) {
            if (cmd.type == stitch::CommandType::Stitch) {
                ++stitchesPerObject[cmd.source.value];
            }
        }
        std::vector<std::pair<std::size_t, std::uint64_t>> byCount;
        for (const auto& [objId, count] : stitchesPerObject) {
            byCount.emplace_back(count, objId);
        }
        std::sort(byCount.begin(), byCount.end(), std::greater<>());
        for (std::size_t i = 0; i < byCount.size() && i < 8; ++i) {
            const auto* emb = project.findEmbroidery(ObjectId{byCount[i].second});
            const char* kind = "?";
            double areaMm2 = 0.0;
            double bboxW = 0.0, bboxH = 0.0;
            if (emb != nullptr) {
                if (emb->is_satin()) kind = "satin";
                else if (emb->is_tatami()) kind = "tatami";
                else kind = "running";
                if (const auto* vec = project.findObject(emb->source_vector)) {
                    std::int32_t minx = 0, maxx = 0, miny = 0, maxy = 0;
                    bool first = true;
                    for (const auto& set : vec->paths) {
                        for (const auto& n : set.outer.nodes) {
                            if (first) { minx = maxx = n.pos.x.value; miny = maxy = n.pos.y.value; first = false; }
                            minx = std::min(minx, n.pos.x.value);
                            maxx = std::max(maxx, n.pos.x.value);
                            miny = std::min(miny, n.pos.y.value);
                            maxy = std::max(maxy, n.pos.y.value);
                        }
                        areaMm2 += std::abs(geometry::signed_area_um2(set.outer)) / 1e6;
                    }
                    bboxW = (maxx - minx) / 1000.0;
                    bboxH = (maxy - miny) / 1000.0;
                }
            }
            std::size_t pieceCount = 0;
            std::size_t maxNodesInPiece = 0;
            if (emb != nullptr) {
                if (const auto* vec = project.findObject(emb->source_vector)) {
                    pieceCount = vec->paths.size();
                    for (const auto& set : vec->paths) {
                        maxNodesInPiece = std::max(maxNodesInPiece, set.outer.nodes.size());
                    }
                }
            }
            std::fprintf(stderr,
                        "DIAG top-objet stitch #%zu: id=%llu type=%s stitches=%zu aire=%.1fmm2 "
                        "bbox=%.1fx%.1fmm morceaux=%zu max_noeuds_par_morceau=%zu\n",
                        i, static_cast<unsigned long long>(byCount[i].second), kind, byCount[i].first,
                        areaMm2, bboxW, bboxH, pieceCount, maxNodesInPiece);
        }
    } else {
        UNSCOPED_INFO("erreur generate_sequence: " << sequence.error().message);
    }

    // L'ordre des objets est celui de leur création (numérisation automatique,
    // par région de segmentation -- sans cohérence spatiale). apps/desktop
    // expose optimize_order comme une étape MANUELLE (bouton "Optimiser
    // l'ordre de couture") -- jamais appliquée automatiquement. Vérifie ici
    // l'effet réel d'une telle optimisation sur ce projet, pour juger si le
    // désordre visible dans le SVG ci-dessus est un défaut du moteur ou
    // simplement une étape que l'utilisateur n'a pas encore lancée.
    const auto centroidOf = [&](ObjectId sourceVec) -> Vec2um {
        const auto* vec = project.findObject(sourceVec);
        if (vec == nullptr || vec->paths.empty()) return Vec2um{};
        std::int64_t sx = 0, sy = 0;
        std::size_t n = 0;
        for (const auto& node : vec->paths.front().outer.nodes) {
            sx += node.pos.x.value;
            sy += node.pos.y.value;
            ++n;
        }
        if (n == 0) return Vec2um{};
        return Vec2um{Micrometers{static_cast<std::int32_t>(sx / static_cast<std::int64_t>(n))},
                     Micrometers{static_cast<std::int32_t>(sy / static_cast<std::int64_t>(n))}};
    };
    std::vector<optimization::OrderItem> items;
    for (const auto& obj : project.embroidery_objects) {
        items.push_back({obj.id, obj.rgb, centroidOf(obj.source_vector), false});
    }
    const auto costBefore = optimization::compute_cost(items);
    const auto order =
        optimization::optimize_order(items, optimization::OrderStrategy::ColorThenProximity);
    std::vector<document::EmbroideryObject> reordered;
    reordered.reserve(project.embroidery_objects.size());
    for (const ObjectId id : order) {
        if (const auto* e = project.findEmbroidery(id)) {
            reordered.push_back(*e);
        }
    }
    project.embroidery_objects = std::move(reordered);
    std::vector<optimization::OrderItem> itemsAfter;
    for (const auto& obj : project.embroidery_objects) {
        itemsAfter.push_back({obj.id, obj.rgb, centroidOf(obj.source_vector), false});
    }
    const auto costAfter = optimization::compute_cost(itemsAfter);
    std::fprintf(stderr,
                "DIAG ordre optimise (ColorThenProximity) : trajet %.1f -> %.1f mm, "
                "changements de fil %zu -> %zu\n",
                costBefore.travel_um / 1000.0, costAfter.travel_um / 1000.0,
                costBefore.color_changes, costAfter.color_changes);

    const auto sequence2 = stitch_generation::generate_sequence(project);
    if (sequence2.has_value()) {
        const auto stats2 = stitch::compute_stats(*sequence2);
        std::fprintf(stderr, "DIAG apres reordonnancement : stitches=%zu jumps=%zu\n",
                    stats2.stitches, stats2.jumps);
        formats::write_svg_file("C:/Users/zache/Pictures/GISTRE_result_ordered.svg", *sequence2);
    }
    CHECK(false);
}

// DIAGNOSTIC TEMPORAIRE : vérification du correctif "pointes de colonne
// satin quasi nulles" (§ audit satin sur logo circulaire GISTRE, projet réel
// fourni par l'utilisateur — retour : "le satin fait n'importe quoi"). Ré-
// exécute auto_digitize sur la segmentation RÉELLE déjà stockée dans le
// .osp (donc la même vectorisation/squelette que la numérisation d'origine)
// et vérifie qu'aucun barreau de satin n'est plus fin que le plancher voulu
// (`tip_min_width`). Avant correctif : 178 des 190 satins de cette image
// (94 %) avaient au moins un barreau sous 50 µm, la marche de `extend_tip`
// pouvant franchir le plancher en un seul pas avant que la condition d'arrêt
// n'ait la main (§ satin_column.cpp, `extend_tip`) ; après correctif : 0.
// CHECK(false) délibéré : force Catch2 à afficher les UNSCOPED_INFO même en
// cas de succès des autres assertions (même pattern que le diagnostic
// Cathebrode ci-dessous).
TEST_CASE("DIAGNOSTIC TEMPORAIRE satin pointes fines (GISTRE)") {
    const auto loaded = project_io::load_project("C:/Users/zache/Pictures/GISTRE.osp");
    REQUIRE(loaded.has_value());
    auto project = *loaded;
    REQUIRE(project.segmentation.has_value());

    const auto result =
        autodigitize::auto_digitize(*project.segmentation, project.object_ids, {});
    REQUIRE(result.has_value());

    int nSatin = 0;
    int nDegenerate = 0;
    double worstMinSeparationUm = std::numeric_limits<double>::max();
    for (const auto& e : result->embroideries) {
        if (auto* sp = std::get_if<document::SatinParams>(&e.params)) {
            ++nSatin;
            // Largeur via les BARREAUX (paires a/b déjà appariées au même
            // point d'axe) -- comparer deux rails aplatis INDÉPENDAMMENT par
            // index brut serait invalide (nombre de points différent selon
            // la courbure locale de chacun : l'index i ne désigne alors pas
            // la même position le long des deux rails).
            for (const auto& rung : sp->rungs) {
                worstMinSeparationUm = std::min(worstMinSeparationUm, length_um(rung.a - rung.b));
            }
        }
    }
    // Plancher voulu (tip_min_width) : 300 µm. Marge de 10 µm pour le bruit
    // d'arrondi au µm des positions de rail.
    for (const auto& e : result->embroideries) {
        if (auto* sp = std::get_if<document::SatinParams>(&e.params)) {
            for (const auto& rung : sp->rungs) {
                if (length_um(rung.a - rung.b) < 290.0) {
                    ++nDegenerate;
                }
            }
        }
    }
    UNSCOPED_INFO("satin objects=" << nSatin << " barreaux sous 0,29 mm=" << nDegenerate
                                   << " pire separation=" << worstMinSeparationUm << " um");
    CHECK(false);
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
