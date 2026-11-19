// SPDX-License-Identifier: Apache-2.0
// Étapes du flux Studio exposées en opérations : image_prep, image_size, vectorize_segmentation,
// enchaînées avec segment_image et convert_vectors comme le fait l'agent du compagnon.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "openstitch/commands/project_commands.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/project_ops/errors.hpp"
#include "openstitch/project_ops/operations.hpp"
#include "openstitch/project_ops/revectorize.hpp"

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
    CHECK_FALSE(run(project, R"({"op":"vectorize_segmentation","precision_mm":0.005})").ok);
    CHECK(run(project, R"({"op":"vectorize_segmentation","curves":true,"adaptive":true})").ok);
    CHECK_FALSE(run(project, R"({"op":"vectorize_segmentation","adaptive":"oui"})").ok);
    CHECK(
        run(project,
            R"({"op":"vectorize_segmentation","curves":true,"adaptive":true,"adaptive_local":true})")
            .ok);
    CHECK_FALSE(run(project, R"({"op":"vectorize_segmentation","adaptive_local":1})").ok);
    CHECK(project_ops::kCurveToleranceMinPx == 0.1);
    CHECK(project_ops::kPrecisionMinMm == 0.01);
}

TEST_CASE("step ops: region precision override drives revectorize", "[project_ops][steps]") {
    auto project = two_color_project();
    REQUIRE(run(project, R"({"op":"segment_image","max_colors":2,"min_region_px":4})").ok);
    REQUIRE(run(project, R"({"op":"vectorize_segmentation"})").ok);
    const auto region = *project.vector_objects.front().source_region;
    const project_ops::RevectorizeOptions opts{};
    CHECK_FALSE(project_ops::region_tolerance(project, opts, region).overridden);
    CHECK(project_ops::region_tolerance(project, opts, region).value == 0.2);

    commands::SetRegionVectorPrecisionCommand cmd(ObjectId{}, region,
                                                  document::VectorPrecision::Simplify);
    cmd.apply(project);
    CHECK(project.imageSource().vector_precision.at(region) == document::VectorPrecision::Simplify);
    const auto tol = project_ops::region_tolerance(project, opts, region);
    CHECK(tol.overridden);
    CHECK(tol.factor == 2.0);
    CHECK(tol.value == 0.4);
    CHECK_FALSE(cmd.affectsEmbroidery());
    CHECK_FALSE(cmd.affectsSegmentation());
    cmd.revert(project);
    CHECK(project.imageSource().vector_precision.empty());
    CHECK(project_ops::revectorize(project, opts).replace.empty());
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
