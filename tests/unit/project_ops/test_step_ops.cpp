// SPDX-License-Identifier: Apache-2.0
// Étapes du flux Studio exposées en opérations : image_prep, image_size, vectorize_segmentation,
// enchaînées avec segment_image et convert_vectors comme le fait l'agent du compagnon.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "openstitch/document/project.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/project_ops/errors.hpp"
#include "openstitch/project_ops/operations.hpp"
#include "openstitch/project_ops/revectorize.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

using namespace openstitch;
using Catch::Matchers::WithinAbs;

namespace {

// 40 x 20 px : moitié gauche rouge, moitié droite bleue ; 0,5 mm/px => 20 x 10 mm.
document::Project two_color_project() {
    document::Project project;
    image::Image img;
    img.width = 40;
    img.height = 20;
    img.rgba.resize(static_cast<std::size_t>(img.width * img.height * 4));
    for (int y = 0; y < img.height; ++y) {
        for (int x = 0; x < img.width; ++x) {
            const auto i = static_cast<std::size_t>((y * img.width + x) * 4);
            const bool left = x < img.width / 2;
            img.rgba[i] = left ? 220 : 20;
            img.rgba[i + 1] = 20;
            img.rgba[i + 2] = left ? 20 : 220;
            img.rgba[i + 3] = 255;
        }
    }
    project.imageSource().original = std::move(img);
    project.imageSource().mm_per_px = Millimeters{0.5};
    return project;
}

project_ops::BatchReport run(document::Project& project, const std::string& op) {
    return project_ops::apply_operation(project, op);
}

} // namespace

TEST_CASE("step ops: segment_image fine_structures keeps a one pixel line",
          "[project_ops][steps]") {
    // 80 x 60 blanc, diagonale noire d'un pixel : sans l'option, lissage et nettoyage l'effacent.
    document::Project project;
    image::Image img;
    img.width = 80;
    img.height = 60;
    img.rgba.assign(static_cast<std::size_t>(img.width * img.height * 4), 255);
    std::vector<std::size_t> line;
    for (int x = 4; x < 76; ++x) {
        const int y = 4 + (x - 4) * 52 / 72;
        const auto i = static_cast<std::size_t>(y * img.width + x);
        img.rgba[i * 4] = img.rgba[i * 4 + 1] = img.rgba[i * 4 + 2] = 10;
        line.push_back(i);
    }
    project.imageSource().original = std::move(img);
    project.imageSource().mm_per_px = Millimeters{0.25};

    const auto kept = [&](const document::Project& p) {
        const auto& seg = *p.imageSource().segmentation;
        const auto background = seg.labels[0];
        return static_cast<std::size_t>(std::count_if(
            line.begin(), line.end(), [&](std::size_t i) { return seg.labels[i] != background; }));
    };

    auto plain = project;
    REQUIRE(
        run(plain,
            R"({"op":"segment_image","max_colors":3,"min_region_px":16,"smoothing_radius_px":1})")
            .ok);
    CHECK(kept(plain) * 2 < line.size()); // la ligne est perdue

    auto protectedRun = project;
    REQUIRE(
        run(protectedRun,
            R"({"op":"segment_image","max_colors":3,"min_region_px":16,"smoothing_radius_px":1,"fine_structures":true})")
            .ok);
    CHECK(kept(protectedRun) * 10 >= line.size() * 9); // au moins 90 % conservée

    // Un booléen est exigé : refus nommé, projet intact.
    auto wrong = project;
    CHECK_FALSE(run(wrong, R"({"op":"segment_image","fine_structures":"oui"})").ok);
    CHECK_FALSE(wrong.imageSource().segmentation.has_value());
}

TEST_CASE("step ops: image to stitches through every Studio step", "[project_ops][steps]") {
    auto project = two_color_project();

    // Image : prétraitement empilé, puis taille physique (proportions conservées).
    REQUIRE(run(project, R"({"op":"image_prep","kind":"median","strength":1})").ok);
    CHECK(project.imageSource().ops.size() == 1);
    REQUIRE(run(project, R"({"op":"image_size","width_mm":80})").ok);
    CHECK_THAT(project.imageSource().mm_per_px.value, WithinAbs(2.0, 1e-9));

    // Segmentation, puis Vecteurs : une forme par région.
    REQUIRE(run(project, R"({"op":"segment_image","max_colors":2,"min_region_px":4})").ok);
    REQUIRE(project.imageSource().segmentation);
    const auto regions = project.imageSource().segmentation->region_count();
    CHECK(regions == 2);
    const auto vectorized = run(project, R"({"op":"vectorize_segmentation"})");
    REQUIRE(vectorized.ok);
    CHECK(project.vector_objects.size() == regions);
    for (const auto& shape : project.vector_objects) {
        CHECK(project.fromActiveImage(shape));
    }

    // Relancer sans changement : rien à faire, aucun doublon.
    REQUIRE(run(project, R"({"op":"vectorize_segmentation"})").ok);
    CHECK(project.vector_objects.size() == regions);
    CHECK(project_ops::revectorize(project, {}).add.empty());

    // Points : les formes deviennent des objets de broderie.
    REQUIRE(run(project, R"({"op":"convert_vectors"})").ok);
    CHECK(project.embroidery_objects.size() == regions);

    // Retour à la segmentation : une région disparaît. Sans prune_orphans sa forme reste (comme
    // dans Studio) ; avec, la forme et son objet de broderie partent en une seule opération.
    const auto& slots = project.imageSource().segmentation->region_slots;
    std::vector<std::uint64_t> ids;
    for (const auto& slot : slots) {
        if (slot) {
            ids.push_back(slot->id.value);
        }
    }
    REQUIRE(ids.size() == 2);
    REQUIRE(run(project, R"({"op":"seg_merge_regions","keep":)" + std::to_string(ids[0]) +
                             R"(,"absorb":)" + std::to_string(ids[1]) + "}")
                .ok);
    REQUIRE(run(project, R"({"op":"vectorize_segmentation"})").ok);
    CHECK(project.vector_objects.size() == 2);
    const auto pruned = run(project, R"({"op":"vectorize_segmentation","prune_orphans":true})");
    REQUIRE(pruned.ok);
    CHECK(project.vector_objects.size() == 1);
    CHECK(project.embroidery_objects.size() == 1);
    CHECK(project.embroidery_objects.front().source_vector == project.vector_objects.front().id);
}

TEST_CASE("step ops: vectorize_segmentation fine tolerance bounds", "[project_ops][steps]") {
    // VEC-ADAPT-01 lot A : bornes partagées avec Studio ; 0,1 px accepté, en dessous refusé.
    auto project = two_color_project();
    REQUIRE(run(project, R"({"op":"segment_image","max_colors":2,"min_region_px":4})").ok);
    CHECK(run(project, R"({"op":"vectorize_segmentation","curves":true,"curve_tolerance_px":0.1})")
              .ok);
    CHECK_FALSE(
        run(project, R"({"op":"vectorize_segmentation","curves":true,"curve_tolerance_px":0.05})")
            .ok);
    CHECK(run(project, R"({"op":"vectorize_segmentation","precision_mm":0.01})").ok);
    CHECK(run(project, R"({"op":"vectorize_segmentation","precision_mm":0})").ok);
    CHECK_FALSE(run(project, R"({"op":"vectorize_segmentation","precision_mm":-0.01})").ok);
    CHECK(project_ops::kCurveToleranceMinPx == 0.1);
    CHECK(project_ops::kPrecisionMinMm == 0.0);
}

TEST_CASE("step ops: image_prep crop and refusals", "[project_ops][steps]") {
    auto project = two_color_project();
    REQUIRE(run(project, R"({"op":"image_prep","kind":"crop","x":0,"y":0,"w":20,"h":20})").ok);
    CHECK(project.imageSource().ops.size() == 1);

    CHECK_FALSE(run(project, R"({"op":"image_prep","kind":"crop","x":10,"y":0,"w":20,"h":20})").ok);
    CHECK_FALSE(run(project, R"({"op":"image_prep","kind":"brightness_contrast"})").ok);
    const auto unknown = run(project, R"({"op":"image_prep","kind":"sepia"})");
    CHECK_FALSE(unknown.ok);
    CHECK(unknown.outcomes.front().code == project_ops::code::kOutOfRange);
    CHECK_FALSE(run(project, R"({"op":"image_size","width_mm":50,"height_mm":20})").ok);
    CHECK(project.imageSource().ops.size() == 1);

    document::Project empty;
    CHECK_FALSE(run(empty, R"({"op":"image_prep","kind":"grayscale"})").ok);
    CHECK_FALSE(run(empty, R"({"op":"vectorize_segmentation"})").ok);
    // Sans segmentation : rien à faire, mais pas d'échec (Studio l'enchaîne à chaque conversion).
    CHECK(run(empty, R"({"op":"overlap_fills"})").ok);
}

TEST_CASE("step ops: overlap_fills closes the gap between neighbouring tatamis",
          "[project_ops][steps]") {
    auto project = two_color_project();
    REQUIRE(run(project, R"({"op":"image_size","width_mm":80})").ok);
    REQUIRE(run(project, R"({"op":"segment_image","max_colors":2,"min_region_px":4})").ok);
    REQUIRE(run(project, R"({"op":"vectorize_segmentation"})").ok);
    REQUIRE(run(project, R"({"op":"convert_vectors","overlap_mm":0})").ok); // écart brut
    REQUIRE(project.vector_objects.size() == 2);
    for (const auto& e : project.embroidery_objects) {
        REQUIRE(e.is_tatami());
    }
    const auto overlap_area = [&project] {
        const auto both = geometry::intersect_polygons(project.vector_objects[0].paths,
                                                       project.vector_objects[1].paths);
        double area = 0.0;
        if (both) {
            for (const auto& set : *both) {
                area += std::abs(geometry::signed_area_um2(set.outer));
            }
        }
        return area / 1e6; // mm²
    };
    // Contours par les centres des pixels : un pixel d'écart, aucune surface commune.
    CHECK(overlap_area() == 0.0);

    const auto widened = run(project, R"({"op":"overlap_fills","overlap_mm":0.5})");
    REQUIRE(widened.ok);
    // Les deux tatamis débordent l'un sous l'autre le long de la frontière (40 mm de haut).
    CHECK(overlap_area() > 20.0);
    for (const auto& e : project.embroidery_objects) {
        CHECK(std::get<document::TatamiParams>(e.params).inset.value == 0);
    }
    // Deuxième passe : rien à faire (pas de croissance cumulative).
    const double after_first = overlap_area();
    REQUIRE(run(project, R"({"op":"overlap_fills"})").ok);
    CHECK(overlap_area() == after_first);

    // convert_vectors seul (défaut) pose déjà ce débord : agent et CLI sans liseré.
    auto direct = two_color_project();
    REQUIRE(run(direct, R"({"op":"image_size","width_mm":80})").ok);
    REQUIRE(run(direct, R"({"op":"segment_image","max_colors":2,"min_region_px":4})").ok);
    REQUIRE(run(direct, R"({"op":"vectorize_segmentation"})").ok);
    REQUIRE(run(direct, R"({"op":"convert_vectors"})").ok);
    for (const auto& e : direct.embroidery_objects) {
        CHECK(std::get<document::TatamiParams>(e.params).inset.value == 0);
    }
}

namespace {

// 120 x 80 px à 0,25 mm/px : fond clair, ciel bleu, cordage noir fin en diagonale douce.
document::Project rope_on_sky_project() {
    document::Project project;
    image::Image img;
    img.width = 120;
    img.height = 80;
    img.rgba.assign(static_cast<std::size_t>(img.width * img.height * 4), 255);
    const auto paint = [&](int x0, int y0, int x1, int y1, std::uint8_t r, std::uint8_t g,
                           std::uint8_t b) {
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const auto i = static_cast<std::size_t>((y * img.width + x) * 4);
                img.rgba[i] = r;
                img.rgba[i + 1] = g;
                img.rgba[i + 2] = b;
            }
        }
    };
    paint(0, 0, 120, 80, 230, 220, 200);
    paint(10, 10, 110, 70, 90, 150, 220);
    for (int x = 12; x < 108; ++x) {
        const int y = 20 + (x - 12) * 40 / 96;
        paint(x, y, x + 1, y + 3, 5, 5, 15);
    }
    project.imageSource().original = std::move(img);
    project.imageSource().mm_per_px = Millimeters{0.25};
    return project;
}

std::size_t count_linework(const std::vector<document::VectorObject>& shapes) {
    std::size_t n = 0;
    for (const auto& s : shapes) {
        n += s.linework ? 1 : 0;
    }
    return n;
}

} // namespace

TEST_CASE("revectorize: preserve linework off keeps the historical plan",
          "[project_ops][steps][linework]") {
    auto project = rope_on_sky_project();
    REQUIRE(run(project, R"({"op":"segment_image","max_colors":3,"min_region_px":4})").ok);
    const auto plan = project_ops::revectorize(project, {});
    CHECK(count_linework(plan.add) == 0);
}

TEST_CASE("revectorize: preserve linework adds open strokes over completed fills",
          "[project_ops][steps][linework]") {
    auto project = rope_on_sky_project();
    REQUIRE(run(project, R"({"op":"segment_image","max_colors":3,"min_region_px":4})").ok);
    const auto off = project_ops::revectorize(project, {});
    const auto sourceLabels = project.imageSource().segmentation->labels;
    project.imageSource().preserve_linework = true;
    const auto on = project_ops::revectorize(project, {});
    CHECK(project.imageSource().segmentation->labels == sourceLabels); // source intacte
    REQUIRE(count_linework(on.add) >= 1);
    for (const auto& shape : on.add) {
        if (!shape.linework) {
            continue;
        }
        CHECK_FALSE(shape.source_region.has_value());
        REQUIRE_FALSE(shape.paths.empty());
        CHECK_FALSE(shape.paths.front().outer.closed);
    }
    // Les surfaces ne contiennent plus la région du cordage : moins de surfaces qu'avec OFF.
    CHECK(on.add.size() - count_linework(on.add) < off.add.size());
    // Deuxième passage sur le projet mis à jour : tout est déjà à jour.
    REQUIRE(run(project, R"({"op":"vectorize_segmentation"})").ok);
    const auto again = project_ops::revectorize(project, {});
    CHECK(again.add.empty());
    CHECK(again.replace.empty());
}

TEST_CASE("revectorize: completed fills continue under the strokes",
          "[project_ops][steps][linework]") {
    // Trait noir de 3 px posé sur la frontière ciel / sable (cas de la rambarde sur la lanterne) :
    // les deux aplats doivent se rejoindre SOUS le trait, sans liseré de tissu nu.
    document::Project project;
    image::Image img;
    img.width = 120;
    img.height = 80;
    img.rgba.assign(static_cast<std::size_t>(img.width * img.height * 4), 255);
    for (int y = 0; y < img.height; ++y) {
        for (int x = 0; x < img.width; ++x) {
            const auto i = static_cast<std::size_t>((y * img.width + x) * 4);
            const bool line = y >= 39 && y < 42 && x >= 10 && x < 110;
            const bool sky = y < 40;
            img.rgba[i] = line ? 5 : (sky ? 90 : 230);
            img.rgba[i + 1] = line ? 5 : (sky ? 150 : 210);
            img.rgba[i + 2] = line ? 15 : (sky ? 220 : 160);
        }
    }
    project.imageSource().original = std::move(img);
    project.imageSource().mm_per_px = Millimeters{0.25};
    REQUIRE(run(project, R"({"op":"segment_image","max_colors":3,"min_region_px":4})").ok);
    project.imageSource().preserve_linework = true;
    const auto plan = project_ops::revectorize(project, {});
    REQUIRE(count_linework(plan.add) >= 1);
    std::vector<geometry::PathSet> fills;
    for (const auto& shape : plan.add) {
        if (!shape.linework) {
            fills.insert(fills.end(), shape.paths.begin(), shape.paths.end());
        }
    }
    // Bande de 1 mm centrée sur le trait (y = 40,5 px), loin de ses extrémités. Repère : origine
    // au centre, Y vers le haut, 250 um/px.
    geometry::PathSet probe;
    for (const auto& [px, py] : {std::pair{30.0, 38.5}, {90.0, 38.5}, {90.0, 42.5}, {30.0, 42.5}}) {
        geometry::PathNode n;
        n.pos = Vec2um{Micrometers{static_cast<std::int32_t>((px - 60.0) * 250.0)},
                       Micrometers{static_cast<std::int32_t>((40.0 - py) * 250.0)}};
        probe.outer.nodes.push_back(n);
    }
    const auto bare = geometry::difference_polygons({probe}, fills);
    REQUIRE(bare.has_value());
    CHECK(bare->empty());
}

TEST_CASE("linework end to end: strokes sewn as running stitch after the fills",
          "[project_ops][steps][linework]") {
    auto project = rope_on_sky_project();
    REQUIRE(run(project, R"({"op":"segment_image","max_colors":3,"min_region_px":4})").ok);
    project.imageSource().preserve_linework = true;
    REQUIRE(run(project, R"({"op":"vectorize_segmentation"})").ok);
    REQUIRE(run(project, R"({"op":"convert_vectors"})").ok);
    std::vector<ObjectId> strokeIds;
    for (const auto& v : project.vector_objects) {
        if (v.linework) {
            strokeIds.push_back(v.id);
        }
    }
    REQUIRE_FALSE(strokeIds.empty());
    std::size_t lastFill = 0;
    std::size_t firstStroke = project.embroidery_objects.size();
    for (std::size_t i = 0; i < project.embroidery_objects.size(); ++i) {
        const auto& e = project.embroidery_objects[i];
        const bool stroke =
            std::find(strokeIds.begin(), strokeIds.end(), e.source_vector) != strokeIds.end();
        if (stroke) {
            CHECK(std::holds_alternative<document::RunningStitchParams>(e.params));
            firstStroke = std::min(firstStroke, i);
        } else {
            lastFill = i;
        }
    }
    CHECK(firstStroke > lastFill); // traits cousus par-dessus les aplats
    const auto sequence = stitch_generation::effective_sequence(project);
    REQUIRE(sequence.has_value());
}

TEST_CASE("revectorize: preserve linework removes shapes absorbed by completed fills",
          "[project_ops][steps][linework]") {
    auto project = rope_on_sky_project();
    REQUIRE(run(project, R"({"op":"segment_image","max_colors":3,"min_region_px":4})").ok);
    REQUIRE(run(project, R"({"op":"vectorize_segmentation"})").ok); // OFF : formes du cordage
    project.imageSource().preserve_linework = true;
    const auto plan = project_ops::revectorize(project, {});
    REQUIRE_FALSE(plan.absorbed.empty());
    for (const ObjectId id : plan.absorbed) {
        const auto* shape = project.findObject(id);
        REQUIRE(shape != nullptr);
        CHECK(shape->source_region.has_value()); // un morceau de région, jamais un trait
    }
    REQUIRE(run(project, R"({"op":"vectorize_segmentation"})").ok);
    for (const ObjectId id : plan.absorbed) {
        CHECK(project.findObject(id) == nullptr);
    }
    CHECK(count_linework(project.vector_objects) >= 1);
    // Une seconde mise à jour n'a plus rien à retirer.
    CHECK(project_ops::revectorize(project, {}).absorbed.empty());
}
