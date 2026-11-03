// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "openstitch/commands/color_blend_commands.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/commands/undo_stack.hpp"
#include "openstitch/commands/vector_ops.hpp"
#include "openstitch/document/color_blend.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/geometry/primitives.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/project_ops/gradient_fill.hpp"
#include "openstitch/stitch_generation/color_blend.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

using namespace openstitch;
using project_ops::GradientFillSpec;

namespace {

Vec2um pt(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

struct Fixture {
    document::Project project;
    ObjectId fill;
};

Fixture rectangle_fill(std::array<std::uint8_t, 3> rgb = {200, 30, 30}) {
    Fixture f;
    document::VectorObject vec;
    vec.id = f.project.object_ids.next();
    vec.name = "forme";
    vec.paths.push_back(
        geometry::PathSet{geometry::rectangle_path(pt(0, 0), pt(30'000, 20'000)), {}});
    document::EmbroideryObject emb;
    emb.id = f.project.object_ids.next();
    emb.name = "remplissage";
    emb.source_vector = vec.id;
    emb.rgb = rgb;
    emb.params = document::TatamiParams{};
    f.fill = emb.id;
    f.project.vector_objects.push_back(std::move(vec));
    f.project.embroidery_objects.push_back(std::move(emb));
    return f;
}

ObjectId make_gradient(Fixture& f, commands::UndoStack& stack, GradientFillSpec* out = nullptr) {
    const auto spec = project_ops::default_gradient_fill(f.project, f.fill);
    REQUIRE(spec.has_value());
    if (out != nullptr) {
        *out = *spec;
    }
    const auto& shape = f.project.vector_objects[0].paths;
    auto command = project_ops::make_create_gradient_fill_command(f.project, f.fill, *spec, shape);
    REQUIRE(command.has_value());
    stack.execute(std::move(*command), f.project);
    REQUIRE(f.project.embroidery_objects.size() == 2);
    return f.project.embroidery_objects[1].id;
}

} // namespace

TEST_CASE("gradient fill: uniform to gradient and back is one undo step each",
          "[project_ops][gradient]") {
    auto f = rectangle_fill();
    const auto before = f.project.embroidery_objects;
    commands::UndoStack stack;
    const ObjectId top = make_gradient(f, stack);

    CHECK(document::find_color_blend(f.project, f.fill));
    CHECK(document::find_color_blend(f.project, top));
    CHECK(f.project.embroidery_objects[0].id == f.fill); // fond d'abord
    CHECK(f.project.embroidery_objects[1].id == top);    // dessus juste après
    CHECK(f.project.embroidery_objects[0].rgb != f.project.embroidery_objects[1].rgb);

    REQUIRE(stack.undo(f.project));
    REQUIRE(f.project.embroidery_objects.size() == 1);
    CHECK(f.project.embroidery_objects[0].params == before[0].params);
    CHECK_FALSE(f.project.embroidery_objects[0].color_blend.has_value());
    REQUIRE(stack.redo(f.project));
    CHECK(document::find_color_blend(f.project, top));

    auto removal = project_ops::make_remove_gradient_fill_command(f.project, top);
    REQUIRE(removal.has_value());
    stack.execute(std::move(*removal), f.project);
    REQUIRE(f.project.embroidery_objects.size() == 1);
    CHECK_FALSE(f.project.embroidery_objects[0].color_blend.has_value());
    REQUIRE(stack.undo(f.project));
    CHECK(f.project.embroidery_objects.size() == 2);
    CHECK(document::find_color_blend(f.project, f.fill));
}

TEST_CASE("gradient fill: describe round-trips and re-edit creates no layer",
          "[project_ops][gradient]") {
    auto f = rectangle_fill();
    commands::UndoStack stack;
    GradientFillSpec created;
    const ObjectId top = make_gradient(f, stack, &created);

    const auto described = project_ops::describe_gradient_fill(f.project, f.fill);
    REQUIRE(described.has_value());
    CHECK(described->background == created.background);
    CHECK(described->foreground == created.foreground);
    CHECK(described->complementary);
    CHECK(described->gradient.from == created.gradient.from);
    CHECK(project_ops::describe_gradient_fill(f.project, top) == described);

    // Réécrire la description telle quelle ne change aucun paramètre.
    const auto topBefore = f.project.embroidery_objects[1];
    auto same = project_ops::make_edit_gradient_fill_command(f.project, top, *described);
    REQUIRE(same.has_value());
    stack.execute(std::move(*same), f.project);
    CHECK(f.project.embroidery_objects.size() == 2);
    CHECK(f.project.embroidery_objects[1].params == topBefore.params);

    auto edited = *described;
    edited.background = {1, 2, 3};
    edited.foreground = {250, 250, 10};
    edited.gradient.curve = document::DensityCurve::Smooth;
    edited.complementary = false;
    auto command = project_ops::make_edit_gradient_fill_command(f.project, f.fill, edited);
    REQUIRE(command.has_value());
    stack.execute(std::move(*command), f.project);

    CHECK(f.project.embroidery_objects.size() == 2);
    CHECK(f.project.embroidery_objects[0].id == f.fill);
    CHECK(f.project.embroidery_objects[1].id == top);
    CHECK(f.project.embroidery_objects[0].rgb == edited.background);
    CHECK(f.project.embroidery_objects[1].rgb == edited.foreground);
    const auto now = project_ops::describe_gradient_fill(f.project, top);
    REQUIRE(now.has_value());
    CHECK(now->gradient.curve == document::DensityCurve::Smooth);
    CHECK_FALSE(now->complementary); // fond uniforme

    REQUIRE(stack.undo(f.project)); // une seule annulation
    CHECK(project_ops::describe_gradient_fill(f.project, top)->complementary);
    CHECK(f.project.embroidery_objects[0].rgb == created.background);
}

TEST_CASE("gradient fill: zone moves the axis ends along their line only",
          "[project_ops][gradient]") {
    auto f = rectangle_fill();
    commands::UndoStack stack;
    GradientFillSpec spec;
    make_gradient(f, stack, &spec);
    const auto& shape = f.project.vector_objects[0].paths;

    const auto full = project_ops::gradient_zone(shape, spec.gradient);
    REQUIRE(full.has_value());
    CHECK(full->start == Catch::Approx(0.0).margin(1e-3));
    CHECK(full->end == Catch::Approx(1.0).margin(1e-3));

    const auto narrowed = project_ops::gradient_with_zone(shape, spec.gradient, {0.25, 0.75});
    REQUIRE(narrowed.has_value());
    CHECK(narrowed->from.y == spec.gradient.from.y);
    CHECK(narrowed->from.x.value == 7'500);
    CHECK(narrowed->to.x.value == 22'500);
    const auto back = project_ops::gradient_zone(shape, *narrowed);
    REQUIRE(back.has_value());
    CHECK(back->start == Catch::Approx(0.25).margin(1e-3));
    CHECK(back->end == Catch::Approx(0.75).margin(1e-3));

    CHECK_FALSE(project_ops::gradient_with_zone(shape, spec.gradient, {0.6, 0.4}));
    CHECK_FALSE(project_ops::gradient_with_zone(shape, spec.gradient, {-0.1, 0.5}));

    spec.gradient = *narrowed;
    auto command = project_ops::make_edit_gradient_fill_command(f.project, f.fill, spec);
    REQUIRE(command.has_value());
    stack.execute(std::move(*command), f.project);
    const auto stored = project_ops::describe_gradient_fill(f.project, f.fill);
    REQUIRE(stored.has_value());
    CHECK(stored->gradient.from == narrowed->from);
    CHECK(stored->gradient.to == narrowed->to);
}

TEST_CASE("gradient fill: moving, duplicating and pasting keep both layers coherent",
          "[project_ops][gradient]") {
    auto f = rectangle_fill();
    commands::UndoStack stack;
    const ObjectId top = make_gradient(f, stack);
    const auto original = *project_ops::describe_gradient_fill(f.project, f.fill);
    const ObjectId vector = f.project.vector_objects[0].id;

    stack.execute(
        std::make_unique<commands::TranslateVectorObjectCommand>(vector, pt(5'000, 2'000)),
        f.project);
    const auto moved = *project_ops::describe_gradient_fill(f.project, top);
    CHECK(moved.gradient.from == original.gradient.from + pt(5'000, 2'000));
    CHECK(moved.gradient.to == original.gradient.to + pt(5'000, 2'000));
    CHECK(project_ops::gradient_zone(f.project.vector_objects[0].paths, moved.gradient)->start ==
          Catch::Approx(0.0).margin(1e-3));

    const auto fragment = commands::collect_fragment(f.project, {vector});
    auto pasted = commands::make_paste_command(f.project, fragment, pt(40'000, 0));
    REQUIRE(pasted.command != nullptr);
    stack.execute(std::move(pasted.command), f.project);
    REQUIRE(f.project.embroidery_objects.size() == 4);
    const auto copyBase = f.project.embroidery_objects[2].id;
    const auto copyTop = f.project.embroidery_objects[3].id;
    const auto layers = document::find_color_blend(f.project, copyTop);
    REQUIRE(layers.has_value());
    CHECK(layers->background == copyBase);
    CHECK(layers->foreground == copyTop);
    CHECK(document::find_color_blend(f.project, top)->background ==
          f.fill); // l'original est intact
    // Le dégradé de la copie est indépendant : l'éditer ne touche pas l'original.
    auto spec = *project_ops::describe_gradient_fill(f.project, copyTop);
    spec.foreground = {9, 9, 9};
    auto edit = project_ops::make_edit_gradient_fill_command(f.project, copyBase, spec);
    REQUIRE(edit.has_value());
    stack.execute(std::move(*edit), f.project);
    CHECK(f.project.findEmbroidery(top)->rgb != spec.foreground);
}

TEST_CASE("gradient fill: file round trip and invalid links", "[project_ops][gradient]") {
    auto f = rectangle_fill();
    commands::UndoStack stack;
    const ObjectId top = make_gradient(f, stack);
    const auto path = std::filesystem::temp_directory_path() / "openstitch_gradient_fill.osp";
    REQUIRE(project_io::save_project(path, f.project).has_value());
    auto loaded = project_io::load_project(path);
    REQUIRE(loaded.has_value());
    std::filesystem::remove(path);
    CHECK(project_ops::describe_gradient_fill(*loaded, f.fill) ==
          project_ops::describe_gradient_fill(f.project, f.fill));

    // Lien orphelin : plus de dégradé, aucune exception, aucun autre objet touché.
    loaded->embroidery_objects.pop_back();
    CHECK_FALSE(project_ops::describe_gradient_fill(*loaded, f.fill).has_value());
    CHECK_FALSE(project_ops::make_edit_gradient_fill_command(
                    *loaded, f.fill, *project_ops::describe_gradient_fill(f.project, top))
                    .has_value());
}

TEST_CASE("gradient fill: legacy pair without recorded spec is re-editable",
          "[project_ops][gradient]") {
    auto f = rectangle_fill();
    commands::UndoStack stack;
    const ObjectId top = make_gradient(f, stack);
    // Ancien projet : couples liés mais couche de dessus reposée à la main, couleurs libres.
    f.project.embroidery_objects[0].thread.reset();
    f.project.embroidery_objects[1].thread.reset();
    const auto spec = project_ops::describe_gradient_fill(f.project, top);
    REQUIRE(spec.has_value());
    auto command = project_ops::make_edit_gradient_fill_command(f.project, top, *spec);
    CHECK(command.has_value());
}

TEST_CASE("gradient fill: incompatibilities are explained", "[project_ops][gradient]") {
    auto f = rectangle_fill();
    CHECK(project_ops::gradient_incompatibility(f.project, f.fill).empty());
    f.project.embroidery_objects[0].params = document::RunningStitchParams{};
    CHECK_FALSE(project_ops::gradient_incompatibility(f.project, f.fill).empty());
    CHECK_FALSE(project_ops::gradient_incompatibility(f.project, ObjectId{99}).empty());
}

TEST_CASE("gradient fill: effective sequence is deterministic and has both colors",
          "[project_ops][gradient]") {
    auto f = rectangle_fill();
    commands::UndoStack stack;
    make_gradient(f, stack);
    const auto a = stitch_generation::effective_sequence(f.project);
    const auto b = stitch_generation::effective_sequence(f.project);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(a->commands.size() == b->commands.size());
    CHECK(a->commands.size() > 100);
}

TEST_CASE("gradient fill: handle drags edit the axis without leaving the shape",
          "[project_ops][gradient]") {
    auto f = rectangle_fill();
    commands::UndoStack stack;
    GradientFillSpec spec;
    make_gradient(f, stack, &spec);
    const auto& shape = f.project.vector_objects[0].paths;
    using project_ops::GradientHandle;

    const auto end = project_ops::drag_gradient_handle(shape, spec.gradient, GradientHandle::End,
                                                       pt(21'000, 3'000));
    REQUIRE(end.has_value());
    CHECK(end->to.x.value == 21'000);
    CHECK(end->from == spec.gradient.from);
    CHECK(end->to.y == spec.gradient.to.y); // glissé le long de l'axe seulement

    // Trop loin : borné au bord de la forme ; croisé : la transition garde une largeur.
    CHECK(
        project_ops::drag_gradient_handle(shape, spec.gradient, GradientHandle::End, pt(900'000, 0))
            ->to.x.value == 30'000);
    const auto crossed =
        project_ops::drag_gradient_handle(shape, spec.gradient, GradientHandle::End, pt(-5'000, 0));
    REQUIRE(crossed.has_value());
    CHECK(crossed->to.x.value > crossed->from.x.value);

    const auto sym = project_ops::drag_gradient_handle(shape, spec.gradient, GradientHandle::Start,
                                                       pt(6'000, 0), true);
    REQUIRE(sym.has_value());
    CHECK(sym->from.x.value == 6'000);
    CHECK(sym->to.x.value == 24'000); // symétrique autour du centre

    auto narrow = *project_ops::gradient_with_zone(shape, spec.gradient, {0.2, 0.5});
    const auto moved =
        project_ops::drag_gradient_handle(shape, narrow, GradientHandle::Move, pt(900'000, 2'000));
    REQUIRE(moved.has_value());
    const auto zone = *project_ops::gradient_zone(shape, *moved);
    CHECK(zone.end == Catch::Approx(1.0).margin(1e-3)); // butée au bord, étendue conservée
    CHECK(zone.end - zone.start == Catch::Approx(0.3).margin(1e-3));

    const auto turned = project_ops::drag_gradient_handle(
        shape, spec.gradient, GradientHandle::Rotate,
        pt(45'000, 10'000)); // poignée à droite du centre : axe vertical
    REQUIRE(turned.has_value());
    CHECK(turned->from.y != turned->to.y); // l'axe n'est plus horizontal
    CHECK(project_ops::gradient_zone(shape, *turned).has_value());
    CHECK_FALSE(project_ops::drag_gradient_handle(shape, document::DensityGradient{},
                                                  GradientHandle::Start, pt(0, 0)));
}

TEST_CASE("gradient fill: the edit command applies exactly what the engine builds",
          "[project_ops][gradient]") {
    // Aperçu et validation partagent make_color_blend_layers : mêmes paramètres de points.
    auto f = rectangle_fill();
    commands::UndoStack stack;
    GradientFillSpec spec;
    const ObjectId top = make_gradient(f, stack, &spec);
    spec.gradient.curve = document::DensityCurve::Steep;
    spec.foreground = {5, 150, 90};
    const auto built = stitch_generation::make_color_blend_layers(
        f.project.embroidery_objects[0], f.project.embroidery_objects[1],
        f.project.vector_objects[0].paths, spec.gradient, spec.complementary);
    REQUIRE(built.has_value());
    auto command = project_ops::make_edit_gradient_fill_command(f.project, top, spec);
    REQUIRE(command.has_value());
    stack.execute(std::move(*command), f.project);
    CHECK(f.project.embroidery_objects[0].params == built->first.params);
    CHECK(f.project.embroidery_objects[1].params == built->second.params);
}
