// SPDX-License-Identifier: Apache-2.0
//
// HP-AUTO-003 : clic-pour-broder. Une region obtenue par croissance devient un objet vectoriel et
// un objet de broderie en une seule commande annulable.
#include <catch2/catch_test_macros.hpp>

#include <variant>

#include "openstitch/commands/click_to_stitch_commands.hpp"
#include "openstitch/commands/undo_stack.hpp"
#include "openstitch/document/project.hpp"

using namespace openstitch;

namespace {

image::Image white_with_red_block(int w, int h, int x0, int y0, int x1, int y1) {
    image::Image img;
    img.width = w;
    img.height = h;
    img.rgba.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4U, 255);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            auto* p = &img.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                                 static_cast<std::size_t>(x)) *
                                4U];
            p[0] = 220;
            p[1] = 30;
            p[2] = 30;
        }
    }
    return img;
}

commands::ClickToStitchOptions options(commands::ClickToStitchKind kind) {
    commands::ClickToStitchOptions o;
    o.mm_per_px = Millimeters{0.5};
    o.kind = kind;
    return o;
}

segmentation::GrownRegion grow(const image::Image& img, int x, int y) {
    auto grown = segmentation::grow_region(img, x, y, {.tolerance = 8.0});
    REQUIRE(grown.has_value());
    return std::move(*grown);
}

} // namespace

TEST_CASE("click to stitch : auto cree un tatami annulable sur un grand aplat") {
    const auto img = white_with_red_block(40, 30, 5, 5, 34, 24); // 15 x 10 mm
    const auto region = grow(img, 10, 10);
    document::Project project;
    commands::UndoStack stack;

    auto result = commands::make_click_to_stitch_command(
        project.object_ids, region, options(commands::ClickToStitchKind::Auto));
    REQUIRE(result.command != nullptr);
    CHECK(result.vector_id.valid());
    CHECK(result.embroidery_id.valid());
    CHECK(project.vector_objects.empty()); // la fabrique ne touche pas au contenu

    stack.execute(std::move(result.command), project);
    REQUIRE(project.vector_objects.size() == 1);
    REQUIRE(project.embroidery_objects.size() == 1);
    const auto& emb = project.embroidery_objects.front();
    CHECK(emb.id == result.embroidery_id);
    CHECK(emb.source_vector == project.vector_objects.front().id);
    CHECK(emb.is_tatami());
    CHECK(emb.rgb == std::array<std::uint8_t, 3>{220, 30, 30});
    CHECK_FALSE(project.vector_objects.front().paths.empty());
    CHECK(stack.undoName() == "Clic-pour-broder");

    REQUIRE(stack.undo(project));
    CHECK(project.vector_objects.empty());
    CHECK(project.embroidery_objects.empty());
    REQUIRE(stack.redo(project));
    REQUIRE(project.embroidery_objects.size() == 1);
    CHECK(project.embroidery_objects.front().id == result.embroidery_id); // memes identifiants
}

TEST_CASE("click to stitch : les types forces donnent les parametres demandes") {
    const auto img = white_with_red_block(40, 30, 5, 5, 34, 24);
    const auto region = grow(img, 10, 10);

    {
        document::Project project;
        commands::UndoStack stack;
        auto r = commands::make_click_to_stitch_command(
            project.object_ids, region, options(commands::ClickToStitchKind::Contour));
        REQUIRE(r.command != nullptr);
        stack.execute(std::move(r.command), project);
        const auto& emb = project.embroidery_objects.front();
        CHECK(std::holds_alternative<document::RunningStitchParams>(emb.params));
        CHECK(emb.intent == document::EmbroideryIntent::ForcedUserChoice);
    }
    {
        document::Project project;
        commands::UndoStack stack;
        auto r = commands::make_click_to_stitch_command(
            project.object_ids, region, options(commands::ClickToStitchKind::Satin));
        REQUIRE(r.command != nullptr);
        stack.execute(std::move(r.command), project);
        CHECK(project.embroidery_objects.front().is_auto_satin());
    }
    {
        // Petite zone (3 x 3 px = 2,25 mm2) : Auto la ramene a un contour, Remplissage force un
        // tatami.
        const auto small = white_with_red_block(20, 20, 4, 4, 6, 6);
        const auto small_region = grow(small, 5, 5);
        document::Project project_auto;
        commands::UndoStack stack_auto;
        auto a = commands::make_click_to_stitch_command(project_auto.object_ids, small_region,
                                                        options(commands::ClickToStitchKind::Auto));
        REQUIRE(a.command != nullptr);
        stack_auto.execute(std::move(a.command), project_auto);
        CHECK(std::holds_alternative<document::RunningStitchParams>(
            project_auto.embroidery_objects.front().params));

        document::Project project_fill;
        commands::UndoStack stack_fill;
        auto f = commands::make_click_to_stitch_command(project_fill.object_ids, small_region,
                                                        options(commands::ClickToStitchKind::Fill));
        REQUIRE(f.command != nullptr);
        stack_fill.execute(std::move(f.command), project_fill);
        CHECK(project_fill.embroidery_objects.front().is_tatami());
    }
}

TEST_CASE("click to stitch : une zone minuscule ou vide est refusee avec un message") {
    const auto img = white_with_red_block(10, 10, 4, 4, 4, 4); // un seul pixel
    const auto region = grow(img, 4, 4);
    document::Project project;
    auto result = commands::make_click_to_stitch_command(
        project.object_ids, region, options(commands::ClickToStitchKind::Auto));
    CHECK(result.command == nullptr);
    CHECK_FALSE(result.error.empty());
    CHECK(project.vector_objects.empty());

    segmentation::GrownRegion empty;
    auto none = commands::make_click_to_stitch_command(project.object_ids, empty,
                                                       options(commands::ClickToStitchKind::Auto));
    CHECK(none.command == nullptr);
    CHECK_FALSE(none.error.empty());
}

TEST_CASE("click to stitch : le resultat est deterministe") {
    const auto img = white_with_red_block(40, 30, 5, 5, 34, 24);
    const auto region = grow(img, 10, 10);
    document::Project a;
    document::Project b;
    auto ra = commands::make_click_to_stitch_command(a.object_ids, region,
                                                     options(commands::ClickToStitchKind::Auto));
    auto rb = commands::make_click_to_stitch_command(b.object_ids, region,
                                                     options(commands::ClickToStitchKind::Auto));
    REQUIRE(ra.command != nullptr);
    REQUIRE(rb.command != nullptr);
    ra.command->apply(a);
    rb.command->apply(b);
    REQUIRE(a.vector_objects.size() == b.vector_objects.size());
    const auto& pa = a.vector_objects.front().paths;
    const auto& pb = b.vector_objects.front().paths;
    CHECK(pa == pb);
}
