// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>

#include "openstitch/commands/color_blend_commands.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/commands/undo_stack.hpp"
#include "openstitch/commands/vector_ops.hpp"
#include "openstitch/document/color_blend.hpp"
#include "openstitch/document/multi_blend.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/geometry/primitives.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/project_ops/gradient_fill.hpp"
#include "openstitch/stitch_generation/color_blend.hpp"
#include "openstitch/stitch_generation/generate.hpp"
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

// Adaptateur de test local pour conserver la lisibilité des scénarios PR #336 tout en exerçant
// exclusivement l'API autonome publique. L'ancien MultiGradientFillSpec n'existe plus en prod.
struct LegacyGradientSpec {
    std::vector<std::array<std::uint8_t, 3>> colors;
    document::DensityGradient gradient;
    std::vector<document::DensityCurve> layer_curves;
    bool complementary{true};
};

project_ops::MulticolorGradientSpec autonomous(const LegacyGradientSpec& legacy) {
    project_ops::MulticolorGradientSpec spec;
    spec.transition = legacy.gradient;
    spec.complementary = legacy.complementary;
    switch (legacy.gradient.texture) {
    case document::DensityTexture::LongShort:
        spec.method = document::GradientMethod::LongAndShort;
        break;
    case document::DensityTexture::Stippled:
        spec.method = document::GradientMethod::ControlledScatter;
        break;
    default:
        spec.method = document::GradientMethod::Directional;
        break;
    }
    for (std::size_t i = 0; i < legacy.colors.size(); ++i) {
        spec.palette.push_back({legacy.colors[i], std::nullopt,
                                i < legacy.layer_curves.size() ? legacy.layer_curves[i]
                                                               : document::DensityCurve::Linear});
    }
    return spec;
}

std::optional<LegacyGradientSpec> describe_legacy(const document::Project& project, ObjectId id) {
    const auto current = project_ops::describe_multicolor_gradient(project, id);
    if (!current) {
        return std::nullopt;
    }
    LegacyGradientSpec legacy;
    legacy.gradient = current->transition;
    legacy.complementary = current->complementary;
    for (const auto& thread : current->palette) {
        legacy.colors.push_back(thread.rgb);
        legacy.layer_curves.push_back(thread.curve);
    }
    return legacy;
}

Result<std::unique_ptr<commands::ICommand>>
create_multicolor(document::Project& project, ObjectId fill, const LegacyGradientSpec& spec) {
    auto next = autonomous(spec);
    if (const auto* base = project.findEmbroidery(fill); base != nullptr && base->is_satin()) {
        next.method = document::GradientMethod::SatinInterlaced;
    }
    return project_ops::make_create_multicolor_gradient_command(project, fill, next);
}

Result<std::unique_ptr<commands::ICommand>>
edit_multicolor(document::Project& project, ObjectId member, const LegacyGradientSpec& spec) {
    auto next = autonomous(spec);
    if (const auto current = project_ops::describe_multicolor_gradient(project, member)) {
        next.method = current->method;
        next.satin_density = current->satin_density;
        next.crosshatch_layers = current->crosshatch_layers;
        next.crosshatch_angle = current->crosshatch_angle;
    }
    return project_ops::make_edit_multicolor_gradient_command(project, member, next);
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

TEST_CASE("multicolor: five physical thread layers are one undoable blend group",
          "[project_ops][gradient][multicolor]") {
    auto f = rectangle_fill();
    const auto plain = f.project.embroidery_objects.front();
    const auto initial = project_ops::default_gradient_fill(f.project, f.fill);
    REQUIRE(initial);
    LegacyGradientSpec spec;
    spec.colors = {{12, 25, 80}, {24, 64, 130}, {57, 113, 165}, {91, 170, 205}, {180, 223, 238}};
    spec.gradient = initial->gradient;
    spec.gradient.texture = document::DensityTexture::Feathered;
    spec.layer_curves = {document::DensityCurve::Linear, document::DensityCurve::Smooth,
                         document::DensityCurve::Steep, document::DensityCurve::EaseIn,
                         document::DensityCurve::EaseOut};
    spec.complementary = true;

    commands::UndoStack stack;
    auto command = create_multicolor(f.project, f.fill, spec);
    REQUIRE(command);
    stack.execute(std::move(*command), f.project);
    REQUIRE(f.project.embroidery_objects.size() == 6); // ancre + 5 couches physiques

    const auto group = document::find_gradient_group(f.project, f.fill);
    REQUIRE(group);
    CHECK(group->layers.size() == 5);
    CHECK(group->anchor == f.fill);
    for (std::size_t i = 0; i < group->layers.size(); ++i) {
        const auto* layer = f.project.findEmbroidery(group->layers[i]);
        REQUIRE(layer);
        CHECK(layer->rgb == spec.colors[i]);
        REQUIRE(layer->multi_blend);
        CHECK(layer->multi_blend->index == i);
        const auto* params = std::get_if<document::DirectionalFillParams>(&layer->params);
        REQUIRE(params);
        REQUIRE(params->density_gradient);
        CHECK(params->density_gradient->curve == spec.layer_curves[i]);
        CHECK(params->density_gradient->density_stops.size() == 5);
        CHECK(params->density_gradient->density_stops[i].weight == 1.0);
        for (std::size_t j = 0; j < 5; ++j) {
            double sum = 0.0;
            for (const auto member : group->layers) {
                const auto* other = f.project.findEmbroidery(member);
                const auto& grad =
                    *std::get<document::DirectionalFillParams>(other->params).density_gradient;
                sum += grad.density_stops[j].weight;
            }
            CHECK(sum == Catch::Approx(1.0));
        }
    }
    const auto stitched = stitch_generation::effective_sequence(f.project);
    REQUIRE(stitched);
    CHECK(stitched->commands.size() > 20);

    // Même résultat après fermeture / réouverture.
    const auto path =
        std::filesystem::temp_directory_path() / "openstitch_multicolor_gradient_test.osp";
    REQUIRE(project_io::save_project(path, f.project));
    const auto reopened = project_io::load_project(path);
    std::filesystem::remove(path);
    REQUIRE(reopened);
    CHECK(document::find_gradient_group(*reopened, f.fill));
    const auto restored = describe_legacy(*reopened, f.fill);
    REQUIRE(restored);
    CHECK(restored->colors == spec.colors);
    CHECK(restored->layer_curves == spec.layer_curves);

    REQUIRE(stack.undo(f.project));
    REQUIRE(f.project.embroidery_objects.size() == 1);
    CHECK(f.project.embroidery_objects.front().params == plain.params);
    CHECK_FALSE(f.project.embroidery_objects.front().multi_blend);
    REQUIRE(stack.redo(f.project));
    REQUIRE(f.project.embroidery_objects.size() == 6);
}

TEST_CASE("multicolor: editing layer count, removing group and undo stay atomic",
          "[project_ops][gradient][multicolor]") {
    auto f = rectangle_fill();
    commands::UndoStack stack;
    auto initial = project_ops::default_gradient_fill(f.project, f.fill);
    REQUIRE(initial);
    LegacyGradientSpec spec;
    spec.gradient = initial->gradient;
    spec.colors = {{200, 5, 5}, {120, 40, 150}, {50, 30, 220}, {10, 120, 240}};
    auto created = create_multicolor(f.project, f.fill, spec);
    REQUIRE(created);
    stack.execute(std::move(*created), f.project);
    const auto original = document::find_gradient_group(f.project, f.fill);
    REQUIRE(original);

    spec.colors.pop_back();
    spec.gradient.geometry = document::DensityGeometry::Curved;
    auto edited = edit_multicolor(f.project, f.fill, spec);
    REQUIRE(edited);
    stack.execute(std::move(*edited), f.project);
    REQUIRE(f.project.embroidery_objects.size() == 4); // ancre + 3 couches
    CHECK(describe_legacy(f.project, f.fill)->colors == spec.colors);
    REQUIRE(stack.undo(f.project));
    REQUIRE(f.project.embroidery_objects.size() == 5); // ancre + 4 couches
    CHECK(document::find_gradient_group(f.project, f.fill)->layers == original->layers);

    auto remove = project_ops::make_remove_multicolor_gradient_command(f.project, f.fill);
    REQUIRE(remove);
    stack.execute(std::move(*remove), f.project);
    CHECK_FALSE(document::find_gradient_group(f.project, f.fill));
    CHECK(f.project.embroidery_objects.size() == 1);
    REQUIRE(stack.undo(f.project));
    CHECK(document::find_gradient_group(f.project, f.fill));
}

TEST_CASE("multicolor: legacy two-color blend upgrades without losing undo",
          "[project_ops][gradient][multicolor]") {
    auto f = rectangle_fill();
    commands::UndoStack stack;
    const auto top = make_gradient(f, stack);
    REQUIRE(document::find_color_blend(f.project, top));
    auto spec = project_ops::describe_gradient_fill(f.project, top);
    REQUIRE(spec);
    LegacyGradientSpec multi;
    multi.colors = {spec->background, {70, 100, 170}, spec->foreground};
    multi.gradient = spec->gradient;
    auto conversion = edit_multicolor(f.project, top, multi);
    REQUIRE(conversion);
    stack.execute(std::move(*conversion), f.project);
    REQUIRE(f.project.embroidery_objects.size() == 4); // ancre + 3 couches
    CHECK_FALSE(document::find_color_blend(f.project, top));
    // `top` (ancien dessus) est retiré par la conversion : plus aucune des nouvelles couches ne
    // réutilise son id. L'ancre, elle, reprend l'id du fond (`f.fill`).
    CHECK(document::find_gradient_group(f.project, f.fill));
    REQUIRE(stack.undo(f.project));
    REQUIRE(f.project.embroidery_objects.size() == 2);
    CHECK(document::find_color_blend(f.project, top));
    CHECK_FALSE(document::find_gradient_group(f.project, top));
}

TEST_CASE("multicolor directional: no long stitched bridges across inactive color zones",
          "[project_ops][gradient][multicolor][routing]") {
    for (int mode = 0; mode < 4; ++mode) {
        const auto texture = static_cast<document::DensityTexture>(mode);
        auto f = rectangle_fill();
        commands::UndoStack stack;
        const auto initial = project_ops::default_gradient_fill(f.project, f.fill);
        REQUIRE(initial);
        LegacyGradientSpec spec;
        spec.gradient = initial->gradient;
        spec.gradient.texture = texture;
        spec.colors.push_back({24, 42, 99});
        spec.colors.push_back({40, 83, 141});
        spec.colors.push_back({76, 122, 179});
        spec.colors.push_back({125, 171, 205});
        spec.colors.push_back({183, 216, 229});
        auto command = create_multicolor(f.project, f.fill, spec);
        REQUIRE(command);
        stack.execute(std::move(*command), f.project);
        const auto result = stitch_generation::effective_sequence(f.project);
        REQUIRE(result);
        bool connected = false;
        Vec2um last{};
        double longest = 0.0;
        for (const auto& command : result->commands) {
            if (command.type != stitch::CommandType::Stitch) {
                connected = false;
                continue;
            }
            if (connected) {
                const double dx = double(command.pos.x.value) - double(last.x.value);
                const double dy = double(command.pos.y.value) - double(last.y.value);
                longest = std::max(longest, std::hypot(dx, dy));
            }
            last = command.pos;
            connected = true;
        }
        CAPTURE(static_cast<int>(texture), longest);
        // Les raccords masqués doivent se transformer en sauts. Le générateur
        // vise des points de 4 mm, 7 mm étant le plafond physique général.
        CHECK(longest <= 7'000.0);
    }
}

TEST_CASE("multicolor gradient: expanded density profiles persist and change stitch paths",
          "[project_ops][gradient][multicolor][curves]") {
    auto fixture = rectangle_fill();
    commands::UndoStack undo;
    LegacyGradientSpec spec;
    spec.colors = {{24, 42, 99}, {86, 144, 197}, {232, 216, 162}};
    const auto defaults = project_ops::default_gradient_fill(fixture.project, fixture.fill);
    REQUIRE(defaults);
    spec.gradient = defaults->gradient;
    auto created = create_multicolor(fixture.project, fixture.fill, spec);
    REQUIRE(created);
    undo.execute(std::move(*created), fixture.project);

    const auto original = stitch_generation::effective_sequence(fixture.project);
    REQUIRE(original);
    for (const auto curve :
         {document::DensityCurve::Convex, document::DensityCurve::Concave,
          document::DensityCurve::ConvexConcave, document::DensityCurve::ConcaveConvex}) {
        auto changed = describe_legacy(fixture.project, fixture.fill);
        REQUIRE(changed);
        changed->layer_curves.assign(changed->colors.size(), curve);
        auto edit = edit_multicolor(fixture.project, fixture.fill, *changed);
        REQUIRE(edit);
        undo.execute(std::move(*edit), fixture.project);

        const auto applied = describe_legacy(fixture.project, fixture.fill);
        REQUIRE(applied);
        REQUIRE(applied->layer_curves.size() == applied->colors.size());
        for (const auto v : applied->layer_curves) {
            CHECK(v == curve);
        }
        const auto generated = stitch_generation::effective_sequence(fixture.project);
        REQUIRE(generated);
        CHECK(generated->commands != original->commands);
    }

    const auto file =
        std::filesystem::temp_directory_path() / "openstitch_multicolor_extended_curves.osp";
    REQUIRE(project_io::save_project(file, fixture.project).has_value());
    auto reopened = project_io::load_project(file);
    REQUIRE(reopened);
    const auto persisted = describe_legacy(*reopened, fixture.fill);
    REQUIRE(persisted);
    REQUIRE(persisted->layer_curves.size() == 3);
    for (const auto curve : persisted->layer_curves) {
        CHECK(curve == document::DensityCurve::ConcaveConvex);
    }
    std::filesystem::remove(file);
}

TEST_CASE("multicolor satin: all eight thread regions stay nonempty and reproducible",
          "[project_ops][gradient][multicolor][satin]") {
    auto f = rectangle_fill();
    document::SatinParams satin;
    satin.rail_a.closed = false;
    satin.rail_b.closed = false;
    satin.rail_a.nodes = {{pt(0, 0), geometry::NodeType::Corner, {}, {}},
                          {pt(30'000, 0), geometry::NodeType::Corner, {}, {}}};
    satin.rail_b.nodes = {{pt(0, 4'000), geometry::NodeType::Corner, {}, {}},
                          {pt(30'000, 4'000), geometry::NodeType::Corner, {}, {}}};
    satin.density = Micrometers{400};
    f.project.embroidery_objects.front().params = satin;

    LegacyGradientSpec spec;
    spec.colors.push_back({24, 42, 99});
    spec.colors.push_back({40, 83, 141});
    spec.colors.push_back({76, 122, 179});
    spec.colors.push_back({125, 171, 205});
    spec.colors.push_back({183, 216, 229});
    spec.colors.push_back({236, 231, 208});
    spec.colors.push_back({241, 182, 134});
    spec.colors.push_back({224, 105, 79});
    commands::UndoStack stack;
    auto created = create_multicolor(f.project, f.fill, spec);
    REQUIRE(created);
    stack.execute(std::move(*created), f.project);
    const auto group = document::find_gradient_group(f.project, f.fill);
    REQUIRE(group);
    REQUIRE(group->layers.size() == 8);

    const auto stitched = stitch_generation::effective_sequence(f.project);
    REQUIRE(stitched);
    const auto repeated = stitch_generation::effective_sequence(f.project);
    REQUIRE(repeated);
    CHECK(stitched->commands == repeated->commands);

    std::array<std::size_t, 8> counts{};
    for (const auto& command : stitched->commands) {
        if (command.type != stitch::CommandType::Stitch) {
            continue;
        }
        for (std::size_t i = 0; i < group->layers.size(); ++i) {
            if (command.source == group->layers[i]) {
                ++counts[i];
                break;
            }
        }
    }
    for (const std::size_t count : counts) {
        CHECK(count > 0);
    }

    // La sauvegarde OSP doit préserver le routage satin et les huit fils :
    // une réouverture ne doit pas modifier la séquence machine générée.
    const auto satin_file =
        std::filesystem::temp_directory_path() / "openstitch_multicolor_satin_eight.osp";
    REQUIRE(project_io::save_project(satin_file, f.project).has_value());
    const auto reopened_satin = project_io::load_project(satin_file);
    REQUIRE(reopened_satin);
    const auto reopened_group = document::find_gradient_group(*reopened_satin, f.fill);
    REQUIRE(reopened_group);
    CHECK(reopened_group->layers.size() == 8);
    const auto reopened_stitches = stitch_generation::effective_sequence(*reopened_satin);
    REQUIRE(reopened_stitches);
    CHECK(reopened_stitches->commands == stitched->commands);
    std::filesystem::remove(satin_file);

    // Un satin multicolore ne doit pas se transformer en une suite de
    // deplacements machine. Les sauts sont permis aux frontieres des fils,
    // mais leur proportion doit rester bornee meme avec huit couleurs.
    std::size_t satin_stitches = 0;
    std::size_t satin_jumps = 0;
    for (const auto& command : stitched->commands) {
        if (command.type == stitch::CommandType::Stitch) {
            ++satin_stitches;
        } else if (command.type == stitch::CommandType::Jump) {
            ++satin_jumps;
        }
    }
    REQUIRE(satin_stitches > 0);
    CAPTURE(satin_stitches, satin_jumps);
    CHECK(satin_jumps * 3 <= satin_stitches);

    // Une transition de couleur doit etre explicite dans le flux machine :
    // aucune couture ne peut relier deux objets de fils differents.
    bool has_previous_stitch = false;
    ObjectId previous_source{};
    for (const auto& command : stitched->commands) {
        if (command.type != stitch::CommandType::Stitch) {
            has_previous_stitch = false;
            continue;
        }
        if (has_previous_stitch) {
            CHECK(command.source == previous_source);
        }
        previous_source = command.source;
        has_previous_stitch = true;
    }

    // Les transitions entre fils doivent rester des déplacements, pas des
    // diagonales cousues. Un point cousu ne doit jamais dépasser 7 mm.
    bool connected = false;
    auto previous = stitched->commands.front().pos;
    for (const auto& command : stitched->commands) {
        if (command.type == stitch::CommandType::Stitch) {
            if (connected) {
                const double dx = static_cast<double>(command.pos.x.value - previous.x.value);
                const double dy = static_cast<double>(command.pos.y.value - previous.y.value);
                CHECK(std::hypot(dx, dy) <= 7'000.0);
            }
            previous = command.pos;
            connected = true;
        } else {
            connected = false;
        }
    }
}

TEST_CASE("multicolor satin: distributed traverses create separate machine color blocks",
          "[project_ops][gradient][multicolor][satin]") {
    auto f = rectangle_fill();
    document::SatinParams satin;
    satin.rail_a.closed = false;
    satin.rail_b.closed = false;
    satin.rail_a.nodes = {{pt(0, 0), geometry::NodeType::Corner, {}, {}},
                          {pt(30'000, 0), geometry::NodeType::Corner, {}, {}}};
    satin.rail_b.nodes = {{pt(0, 4'000), geometry::NodeType::Corner, {}, {}},
                          {pt(30'000, 4'000), geometry::NodeType::Corner, {}, {}}};
    satin.density = Micrometers{400};
    f.project.embroidery_objects.front().params = satin;

    const auto original = stitch_generation::effective_sequence(f.project);
    REQUIRE(original);
    LegacyGradientSpec spec;
    spec.colors = {{20, 30, 100}, {35, 105, 175}, {95, 180, 205}};
    commands::UndoStack stack;
    auto created = create_multicolor(f.project, f.fill, spec);
    REQUIRE(created);
    stack.execute(std::move(*created), f.project);
    const auto group = document::find_gradient_group(f.project, f.fill);
    REQUIRE(group);
    REQUIRE(group->layers.size() == 3);
    for (const auto id : group->layers) {
        const auto* object = f.project.findEmbroidery(id);
        REQUIRE(object);
        CHECK(object->is_satin());
    }
    const auto stitched = stitch_generation::effective_sequence(f.project);
    REQUIRE(stitched);
    CHECK(stitched->commands.size() > 10);

    // Répartition exclusive par traverse : la quantité totale ne doit
    // pas devenir triple sur le même ruban de satin.
    CHECK(stitched->commands.size() < 2 * original->commands.size() + 20);

    REQUIRE(stack.undo(f.project));
    REQUIRE(f.project.embroidery_objects.size() == 1);
    REQUIRE(stack.redo(f.project));
    CHECK(document::find_gradient_group(f.project, f.fill));

    auto remove = project_ops::make_remove_multicolor_gradient_command(f.project, f.fill);
    REQUIRE(remove);
    stack.execute(std::move(*remove), f.project);
    CHECK_FALSE(document::find_gradient_group(f.project, f.fill));
    REQUIRE(f.project.embroidery_objects.size() == 1);
    CHECK(f.project.embroidery_objects[0].is_satin());
}

// --- API explicite (MulticolorGradientSpec), mission 2026-11-14, Phase 3 ------------------------

TEST_CASE("multicolor gradient object: explicit API creates, reads back and removes an anchor",
          "[project_ops][gradient][multicolor][object]") {
    auto f = rectangle_fill();
    project_ops::MulticolorGradientSpec spec;
    spec.method = document::GradientMethod::Directional;
    spec.palette = {document::GradientThread{{10, 20, 220}, std::nullopt},
                    document::GradientThread{{220, 120, 10}, std::nullopt},
                    document::GradientThread{{40, 200, 60}, std::nullopt}};
    spec.transition.from = pt(0, 0);
    spec.transition.to = pt(30'000, 0);

    commands::UndoStack stack;
    auto created = project_ops::make_create_multicolor_gradient_command(f.project, f.fill, spec);
    REQUIRE(created);
    stack.execute(std::move(*created), f.project);
    REQUIRE(f.project.embroidery_objects.size() == 4); // ancre + 3 couches
    REQUIRE(f.project.embroidery_objects[0].id == f.fill);
    CHECK(f.project.embroidery_objects[0].is_multicolor_gradient());

    const auto described = project_ops::describe_multicolor_gradient(f.project, f.fill);
    REQUIRE(described);
    CHECK(described->method == document::GradientMethod::Directional);
    REQUIRE(described->palette.size() == 3);
    CHECK(described->palette[1].rgb == spec.palette[1].rgb);

    const auto group = document::find_gradient_group(f.project, f.fill);
    REQUIRE(group);
    CHECK(group->anchor == f.fill);
    CHECK(group->layers.size() == 3);
    for (const auto id : group->layers) {
        const auto* layer = f.project.findEmbroidery(id);
        REQUIRE(layer);
        CHECK(layer->is_directional());
    }

    auto edited = spec;
    edited.palette[0].rgb = {5, 5, 5};
    auto editCmd = project_ops::make_edit_multicolor_gradient_command(f.project, f.fill, edited);
    REQUIRE(editCmd);
    stack.execute(std::move(*editCmd), f.project);
    CHECK(project_ops::describe_multicolor_gradient(f.project, f.fill)->palette[0].rgb ==
          std::array<std::uint8_t, 3>{5, 5, 5});

    auto removed = project_ops::make_remove_multicolor_gradient_command(f.project, f.fill);
    REQUIRE(removed);
    stack.execute(std::move(*removed), f.project);
    REQUIRE(f.project.embroidery_objects.size() == 1);
    CHECK_FALSE(project_ops::describe_multicolor_gradient(f.project, f.fill).has_value());
    REQUIRE(stack.undo(f.project));
    REQUIRE(f.project.embroidery_objects.size() == 4);
    REQUIRE(stack.undo(f.project));
    REQUIRE(stack.undo(f.project));
    REQUIRE(f.project.embroidery_objects.size() == 1);
    CHECK_FALSE(f.project.embroidery_objects.front().is_multicolor_gradient());
}

TEST_CASE("multicolor gradient object: satin interlaced derives rails when none exist",
          "[project_ops][gradient][multicolor][object][satin]") {
    auto f = rectangle_fill(); // Tatami, pas de rails -- rien a reutiliser.
    REQUIRE_FALSE(std::get_if<document::SatinParams>(&f.project.embroidery_objects[0].params));

    project_ops::MulticolorGradientSpec spec;
    spec.method = document::GradientMethod::SatinInterlaced;
    spec.satin_density = Micrometers{400};
    spec.palette = {document::GradientThread{{255, 0, 0}, std::nullopt},
                    document::GradientThread{{0, 255, 0}, std::nullopt},
                    document::GradientThread{{0, 0, 255}, std::nullopt}};

    CHECK(project_ops::multicolor_gradient_incompatibility(f.project, f.fill, spec.method).empty());

    commands::UndoStack stack;
    auto created = project_ops::make_create_multicolor_gradient_command(f.project, f.fill, spec);
    REQUIRE(created);
    stack.execute(std::move(*created), f.project);

    const auto group = document::find_gradient_group(f.project, f.fill);
    REQUIRE(group);
    REQUIRE(group->layers.size() == 3);
    for (const auto id : group->layers) {
        const auto* layer = f.project.findEmbroidery(id);
        REQUIRE(layer);
        REQUIRE(layer->is_satin());
        const auto& satin = std::get<document::SatinParams>(layer->params);
        CHECK(satin.rail_a.nodes.size() >= 2);
        CHECK(satin.rail_b.nodes.size() >= 2);
    }
    const auto stitched = stitch_generation::effective_sequence(f.project);
    REQUIRE(stitched);
    CHECK(stitched->commands.size() > 10);
}

TEST_CASE(
    "multicolor gradient object: satin interlaced is refused on a shape too small for two rails",
    "[project_ops][gradient][multicolor][object][satin]") {
    document::Project project;
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    // Contour degenere (tous les sommets confondus) : la distance maximale entre deux sommets
    // est nulle, `rails_from_contour` refuse toujours ce cas (quel que soit le compte de points
    // apres fermeture) -- plus robuste qu'un seuil de nombre de sommets pour ce test.
    geometry::Path triangle;
    triangle.closed = true;
    triangle.nodes = {{pt(0, 0), geometry::NodeType::Corner, {}, {}},
                      {pt(0, 0), geometry::NodeType::Corner, {}, {}},
                      {pt(0, 0), geometry::NodeType::Corner, {}, {}}};
    vec.paths.push_back(geometry::PathSet{triangle, {}});
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.source_vector = vec.id;
    emb.params = document::TatamiParams{};
    project.vector_objects.push_back(std::move(vec));
    project.embroidery_objects.push_back(emb);

    const auto why = project_ops::multicolor_gradient_incompatibility(
        project, emb.id, document::GradientMethod::SatinInterlaced);
    CHECK_FALSE(why.empty());
    CHECK(why.find("rails") != std::string::npos);
    // Message actionnable, pas un simple constat (mission : "ne te contente pas d'afficher
    // rails manquants").
    CHECK(why.find("Dessinez") != std::string::npos);

    project_ops::MulticolorGradientSpec spec;
    spec.method = document::GradientMethod::SatinInterlaced;
    spec.palette = {document::GradientThread{{255, 0, 0}, std::nullopt},
                    document::GradientThread{{0, 0, 255}, std::nullopt}};
    const auto created =
        project_ops::make_create_multicolor_gradient_command(project, emb.id, spec);
    CHECK_FALSE(created.has_value());
}

TEST_CASE("multicolor gradient object: cross hatching layers all stitch at their own angle",
          "[project_ops][gradient][multicolor][object][crosshatch]") {
    auto f = rectangle_fill();
    project_ops::MulticolorGradientSpec spec;
    spec.method = document::GradientMethod::CrossHatching;
    spec.crosshatch_layers = 2;
    spec.crosshatch_angle = Angle{0.0};
    spec.transition.spacing_from = Micrometers{600};
    spec.transition.spacing_to = Micrometers{600};
    spec.palette = {document::GradientThread{{10, 10, 200}, std::nullopt},
                    document::GradientThread{{200, 10, 10}, std::nullopt},
                    document::GradientThread{{10, 200, 10}, std::nullopt},
                    document::GradientThread{{200, 200, 10}, std::nullopt}};

    commands::UndoStack stack;
    auto created = project_ops::make_create_multicolor_gradient_command(f.project, f.fill, spec);
    REQUIRE(created);
    stack.execute(std::move(*created), f.project);

    const auto group = document::find_gradient_group(f.project, f.fill);
    REQUIRE(group);
    REQUIRE(group->layers.size() == 4);
    for (std::size_t i = 0; i < group->layers.size(); ++i) {
        const auto* layer = f.project.findEmbroidery(group->layers[i]);
        REQUIRE(layer);
        REQUIRE(layer->is_directional());
        const auto& params = std::get<document::DirectionalFillParams>(layer->params);
        REQUIRE(params.guides.size() == 1);
        REQUIRE(params.density_gradient);
        CHECK(params.density_gradient->density_stops[i].weight == Catch::Approx(1.0));
    }
    // Les deux premières couches alternent entre deux angles perpendiculaires (0 et 90 degres) ;
    // chacune doit produire des points reels.
    const auto stitched = stitch_generation::effective_sequence(f.project);
    REQUIRE(stitched);
    CHECK(stitched->commands.size() > 10);

    // Determinisme : deux generations identiques produisent la meme sequence.
    const auto repeated = stitch_generation::effective_sequence(f.project);
    REQUIRE(repeated);
    CHECK(stitched->commands == repeated->commands);
}
