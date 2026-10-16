// SPDX-License-Identifier: Apache-2.0
// Rendu réaliste headless (`project render`) : PNG valide, déterministe, fond de tissu visible.
#include <catch2/catch_test_macros.hpp>

#include <array>

#include "openstitch/document/project.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/project_ops/render.hpp"

using namespace openstitch;
using namespace openstitch::project_ops;

namespace {

Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

// Carré tatami de côté 2*half (µm) centré sur l'origine, de couleur `rgb`.
void add_square(document::Project& project, std::int32_t half, std::array<std::uint8_t, 3> rgb) {
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.name = "carre";
    geometry::PathSet set;
    for (const auto& p : {um(-half, -half), um(half, -half), um(half, half), um(-half, half)}) {
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
}

} // namespace

TEST_CASE("rendu realiste : PNG valide, fond vert autour du motif", "[project_ops][render]") {
    document::Project project;
    add_square(project, 10'000, {200, 30, 30});
    RenderOptions options;
    options.px_per_mm = 8.0;
    const auto rendered = render_realistic_png(project, options);
    REQUIRE(rendered.has_value());
    CHECK(rendered->width > 100);
    CHECK(rendered->height > 100);
    // Le PNG se relit, et ses coins (hors motif) sont du fond vert.
    const auto decoded = image::decode_image(rendered->png);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->width == rendered->width);
    const auto px = [&](int x, int y) {
        const auto* p =
            decoded->rgba.data() + (static_cast<std::size_t>(y) * decoded->width + x) * 4;
        return std::array<int, 3>{p[0], p[1], p[2]};
    };
    CHECK(px(0, 0) == std::array<int, 3>{0, 255, 0});
    // Le centre est brodé : rouge dominant, plus de vert franc.
    const auto centre = px(decoded->width / 2, decoded->height / 2);
    CHECK(centre[0] > centre[1]);
    // Déterminisme : mêmes entrées, mêmes octets.
    const auto again = render_realistic_png(project, options);
    REQUIRE(again.has_value());
    CHECK(again->png == rendered->png);
}

TEST_CASE("rendu realiste : refus nomme sans point visible et zone vide", "[project_ops][render]") {
    document::Project empty;
    CHECK_FALSE(render_realistic_png(empty).has_value());

    document::Project project;
    add_square(project, 5'000, {10, 10, 10});
    RenderOptions bad;
    bad.crop = {0.6, 0.0, 0.4, 1.0}; // x1 < x0
    const auto refused = render_realistic_png(project, bad);
    REQUIRE_FALSE(refused.has_value());

    RenderOptions half;
    half.crop = {0.0, 0.0, 0.5, 1.0};
    const auto cropped = render_realistic_png(project, half);
    const auto full = render_realistic_png(project);
    REQUIRE(cropped.has_value());
    REQUIRE(full.has_value());
    CHECK(cropped->width < full->width);
}
