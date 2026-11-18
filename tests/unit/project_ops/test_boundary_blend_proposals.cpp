// SPDX-License-Identifier: Apache-2.0
// Fondu à la frontière proposé depuis l'image (lot L5, docs/studio-fondu-frontiere-spec.md).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <string>

#include "openstitch/commands/undo_stack.hpp"
#include "openstitch/document/boundary_blend.hpp"
#include "openstitch/project_ops/boundary_blend_proposals.hpp"
#include "openstitch/project_ops/operations.hpp"

using namespace openstitch;

namespace {

document::VectorObject rect_shape(ObjectId id, int x_mm, int y_mm, int w_mm, int h_mm) {
    document::VectorObject v;
    v.id = id;
    geometry::PathSet set;
    const auto pt = [](int x, int y) {
        geometry::PathNode n;
        n.pos = Vec2um{Micrometers{x * 1000}, Micrometers{y * 1000}};
        return n;
    };
    set.outer.nodes = {pt(x_mm, y_mm), pt(x_mm + w_mm, y_mm), pt(x_mm + w_mm, y_mm + h_mm),
                       pt(x_mm, y_mm + h_mm)};
    set.outer.closed = true;
    v.paths.push_back(set);
    return v;
}

// Deux zones accolées en x = 0 (bleue à gauche, rouge à droite), 40 × 20 mm au total, et une
// image 0,1 mm/px centrée sur l'origine dont la couleur passe du bleu au rouge sur
// `transition_mm` autour de x = 0 (0 : bord net).
document::Project make_project(double transition_mm) {
    document::Project p;
    const auto add = [&](int x_mm, std::array<std::uint8_t, 3> rgb) {
        const ObjectId v = p.object_ids.next();
        p.vector_objects.push_back(rect_shape(v, x_mm, -10, 20, 20));
        document::EmbroideryObject e;
        e.id = p.object_ids.next();
        e.source_vector = v;
        e.rgb = rgb;
        e.params = document::TatamiParams{};
        p.embroidery_objects.push_back(std::move(e));
    };
    add(-20, {30, 60, 200});
    add(0, {200, 40, 40});
    image::Image img;
    img.width = 500;  // 50 mm
    img.height = 300; // 30 mm
    img.rgba.assign(static_cast<std::size_t>(img.width) * img.height * 4, 255);
    for (int py = 0; py < img.height; ++py) {
        for (int px = 0; px < img.width; ++px) {
            const double x = (px + 0.5 - img.width / 2.0) * 0.1; // mm
            double u = x >= 0.0 ? 1.0 : 0.0;
            if (transition_mm > 0.0) {
                u = std::clamp(x / transition_mm + 0.5, 0.0, 1.0);
            }
            const std::size_t k = (static_cast<std::size_t>(py) * img.width + px) * 4;
            img.rgba[k + 0] = static_cast<std::uint8_t>(std::lround(30 + u * 170));
            img.rgba[k + 1] = static_cast<std::uint8_t>(std::lround(60 - u * 20));
            img.rgba[k + 2] = static_cast<std::uint8_t>(std::lround(200 - u * 160));
        }
    }
    p.imageSource().original = std::move(img);
    p.imageSource().mm_per_px = Millimeters{0.1};
    return p;
}

} // namespace

TEST_CASE("a soft colour transition in the image proposes a boundary blend",
          "[project_ops][blend][proposal]") {
    const auto project = make_project(6.0);
    const auto proposals = project_ops::propose_boundary_blends(project);
    REQUIRE(proposals.has_value());
    REQUIRE(proposals->size() == 1);
    const auto& p = proposals->front();
    CHECK(p.a == ObjectId{2});
    CHECK(p.b == ObjectId{4});
    // Passage linéaire sur 6 mm : 10 %-90 % ≈ 4,8 mm, ≈ 2,4 mm de chaque côté (retrait de bord
    // de 0,2 mm et pas de lecture près).
    CHECK(p.transition_mm == Catch::Approx(4.8).margin(0.6));
    CHECK(p.options.width_a.value == Catch::Approx(2'400).margin(500));
    CHECK(p.options.width_b.value == Catch::Approx(2'400).margin(500));
    CHECK(p.profiles >= p.profiles_total / 2);
}

TEST_CASE("a sharp edge in the image proposes nothing", "[project_ops][blend][proposal]") {
    const auto project = make_project(0.0);
    const auto proposals = project_ops::propose_boundary_blends(project);
    REQUIRE(proposals.has_value());
    CHECK(proposals->empty());
}

TEST_CASE("no image source is a coded refusal", "[project_ops][blend][proposal]") {
    auto project = make_project(6.0);
    project.imageSource().original = image::Image{};
    const auto proposals = project_ops::propose_boundary_blends(project);
    REQUIRE_FALSE(proposals.has_value());
    CHECK(proposals.error().code == "E_BLEND_NO_IMAGE");
}

TEST_CASE("propose_boundary_blends operation lists then applies in one undo step",
          "[project_ops][blend][proposal][ops]") {
    auto project = make_project(6.0);
    const auto listed =
        project_ops::apply_operation(project, R"({"op":"propose_boundary_blends"})");
    INFO(listed.to_json());
    REQUIRE(listed.ok);
    CHECK_FALSE(project.findEmbroidery(ObjectId{2})->boundary_blend.has_value());
    CHECK(listed.to_json().find("transition_mm") != std::string::npos);

    const auto applied =
        project_ops::apply_operation(project, R"({"op":"propose_boundary_blends","apply":true})");
    INFO(applied.to_json());
    REQUIRE(applied.ok);
    CHECK(document::find_boundary_blend(project, ObjectId{2}).has_value());

    // Plus rien à proposer : les deux zones sont liées.
    const auto again =
        project_ops::apply_operation(project, R"({"op":"propose_boundary_blends","apply":true})");
    CHECK_FALSE(again.ok);
}
