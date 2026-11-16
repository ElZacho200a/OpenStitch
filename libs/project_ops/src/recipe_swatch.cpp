// SPDX-License-Identifier: Apache-2.0
#include <string>

#include "openstitch/document/stitch_recipes.hpp"
#include "openstitch/project_ops/render.hpp"

namespace openstitch::project_ops {

namespace {

geometry::PathNode corner(std::int32_t x, std::int32_t y) {
    return geometry::PathNode{Vec2um{Micrometers{x}, Micrometers{y}}, geometry::NodeType::Corner,
                              std::nullopt, std::nullopt};
}

// Formes témoins, en µm : octogone 18 x 14 mm (remplissage), onde ouverte de 24 mm (contour).
geometry::Path swatch_path(document::RecipeTarget target) {
    geometry::Path path;
    if (target == document::RecipeTarget::Fill) {
        path.closed = true;
        path.nodes = {corner(4'000, 0),       corner(14'000, 0),      corner(18'000, 4'000),
                      corner(18'000, 10'000), corner(14'000, 14'000), corner(4'000, 14'000),
                      corner(0, 10'000),      corner(0, 4'000)};
    } else {
        path.closed = false;
        path.nodes = {corner(0, 2'000), corner(6'000, 10'000), corner(12'000, 2'000),
                      corner(18'000, 10'000), corner(24'000, 2'000)};
    }
    return path;
}

} // namespace

Result<RenderResult> render_recipe_swatch(std::string_view recipe_id, double px_per_mm) {
    const auto* recipe = document::find_stitch_recipe(recipe_id);
    if (recipe == nullptr) {
        return std::unexpected(
            Error{ErrorCategory::UserInput, "recette inconnue : " + std::string(recipe_id), {}});
    }
    document::Project project;
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.paths.push_back(geometry::PathSet{swatch_path(recipe->target), {}});
    project.vector_objects.push_back(vec);
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.source_vector = vec.id;
    emb.rgb = {38, 72, 158};
    emb.params = recipe->params;
    project.embroidery_objects.push_back(emb);
    RenderOptions options;
    options.px_per_mm = px_per_mm;
    options.fabric_rgb = {236, 230, 218};
    options.max_pixels = 400'000; // vignette : borne basse, rendu rapide
    return render_realistic_png(project, options);
}

} // namespace openstitch::project_ops
