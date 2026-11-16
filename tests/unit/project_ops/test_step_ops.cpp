// SPDX-License-Identifier: Apache-2.0
// Étapes du flux Studio exposées en opérations : image_prep, image_size, vectorize_segmentation,
// enchaînées avec segment_image et convert_vectors comme le fait l'agent du compagnon.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "openstitch/document/project.hpp"
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
}
