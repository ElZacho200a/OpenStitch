// SPDX-License-Identifier: Apache-2.0
// Recettes de points (STCH-CROS-01, ADR 0007).
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <set>
#include <string>

#include "openstitch/document/stitch_recipes.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

using namespace openstitch;

namespace {

geometry::PathNode node(std::int32_t x, std::int32_t y) {
    return geometry::PathNode{Vec2um{Micrometers{x}, Micrometers{y}}, geometry::NodeType::Corner,
                              std::nullopt, std::nullopt};
}

// Un objet : carré fermé de 20 mm pour un remplissage, tracé ouvert en L pour un contour.
document::Project project_for(const document::StitchRecipe& recipe) {
    document::Project project;
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    geometry::Path path;
    if (recipe.target == document::RecipeTarget::Fill) {
        path.closed = true;
        path.nodes = {node(0, 0), node(20'000, 0), node(20'000, 20'000), node(0, 20'000)};
    } else {
        path.closed = false;
        path.nodes = {node(0, 0), node(30'000, 0), node(30'000, 15'000)};
    }
    vec.paths.push_back(geometry::PathSet{path, {}});
    project.vector_objects.push_back(vec);
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.source_vector = vec.id;
    emb.params = recipe.params;
    project.embroidery_objects.push_back(emb);
    return project;
}

std::size_t stitch_count(const stitch::StitchSequence& s) {
    std::size_t n = 0;
    for (const auto& c : s.commands) {
        n += c.type == stitch::CommandType::Stitch ? 1U : 0U;
    }
    return n;
}

} // namespace

TEST_CASE("recipes: stable unique ids, names, sources and lookup") {
    const auto& all = document::stitch_recipes();
    REQUIRE(all.size() >= 14);
    std::set<std::string> ids;
    for (const auto& r : all) {
        INFO(std::string(r.id));
        CHECK(ids.insert(std::string(r.id)).second);
        CHECK(!r.name_fr.empty());
        CHECK(!r.name_en.empty());
        CHECK(r.version >= 1);
        CHECK(r.source_url == "https://crosstela.com/fr/learn/stitches/" + std::string(r.id) + "/");
        CHECK(document::find_stitch_recipe(r.id) == &r);
    }
    CHECK(document::find_stitch_recipe("no-such-recipe") == nullptr);
    CHECK(document::recipe_target_id(document::RecipeTarget::Outline) == "outline");
    CHECK(document::recipe_fidelity_id(document::RecipeFidelity::Approximation) == "approximation");
}

TEST_CASE("recipes: every recipe stitches its target shape, deterministically") {
    for (const auto& r : document::stitch_recipes()) {
        INFO(std::string(r.id));
        const auto project = project_for(r);
        const auto a = stitch_generation::effective_sequence(project);
        const auto b = stitch_generation::effective_sequence(project);
        REQUIRE(a.has_value());
        CHECK(stitch_count(*a) > 10);
        REQUIRE(b.has_value());
        CHECK(a->commands == b->commands);
    }
}
