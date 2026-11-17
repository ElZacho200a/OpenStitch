// SPDX-License-Identifier: Apache-2.0
// Fondus dont les parts des fils sont lues dans une grille (image), 2026-11-17.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>

#include "openstitch/autodigitize/image_blend.hpp"
#include "openstitch/commands/undo_stack.hpp"
#include "openstitch/document/multi_blend.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/geometry/primitives.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/project_ops/gradient_fill.hpp"
#include "openstitch/stitch_generation/generate.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

using namespace openstitch;

namespace {

Vec2um pt(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

struct Fixture {
    document::Project project;
    ObjectId fill;
};

// Rectangle de 30 × 20 mm centré sur l'origine (repère de l'image de travail).
Fixture rectangle_fill() {
    Fixture f;
    document::VectorObject vec;
    vec.id = f.project.object_ids.next();
    vec.name = "forme";
    vec.paths.push_back(
        geometry::PathSet{geometry::rectangle_path(pt(-15'000, -10'000), pt(15'000, 10'000)), {}});
    document::EmbroideryObject emb;
    emb.id = f.project.object_ids.next();
    emb.name = "remplissage";
    emb.source_vector = vec.id;
    emb.rgb = {200, 30, 30};
    emb.params = document::TatamiParams{};
    f.fill = emb.id;
    f.project.vector_objects.push_back(std::move(vec));
    f.project.embroidery_objects.push_back(std::move(emb));
    return f;
}

// Trois fils dont les parts varient dans les DEUX directions : fil 0 à gauche, fil 2 à
// droite, fil 1 en haut (aucun axe unique ne décrit ce mélange). Somme 255 par case.
document::WeightGrid three_thread_grid() {
    document::WeightGrid g;
    g.cell = Micrometers{1000};
    g.origin = pt(-14'500, -9'500);
    g.columns = 30;
    g.rows = 20;
    g.channels = 3;
    g.values.resize(static_cast<std::size_t>(g.columns * g.rows) * 3);
    for (int r = 0; r < g.rows; ++r) {
        for (int c = 0; c < g.columns; ++c) {
            const double x = c / double(g.columns - 1);
            const double y = r / double(g.rows - 1);
            const double top = 0.6 * y;
            const auto w1 = static_cast<int>(std::lround(255.0 * top));
            const auto w2 = static_cast<int>(std::lround((255 - w1) * x));
            const int w0 = 255 - w1 - w2;
            auto* v = &g.values[static_cast<std::size_t>(r * g.columns + c) * 3];
            v[0] = static_cast<std::uint8_t>(w0);
            v[1] = static_cast<std::uint8_t>(w1);
            v[2] = static_cast<std::uint8_t>(w2);
        }
    }
    return g;
}

project_ops::MulticolorGradientSpec grid_spec(const Fixture& f) {
    auto spec = project_ops::default_multicolor_gradient(f.project, f.fill);
    REQUIRE(spec);
    spec->palette = {document::GradientThread{{200, 30, 30}},
                     document::GradientThread{{240, 200, 40}},
                     document::GradientThread{{30, 120, 200}}};
    spec->complementary = true;
    spec->image_weights = three_thread_grid();
    return *spec;
}

// Longueur cousue (mm) par fil et par quart du rectangle : [fil][gauche/droite][bas/haut].
std::vector<std::array<std::array<double, 2>, 2>> sewn_by_quadrant(const document::Project& p) {
    const auto sequence = stitch_generation::effective_sequence(p);
    REQUIRE(sequence);
    std::vector<std::array<std::array<double, 2>, 2>> out(1);
    bool connected = false;
    Vec2um last{};
    for (const auto& command : sequence->commands) {
        if (command.type == stitch::CommandType::ColorChange) {
            out.emplace_back();
            connected = false;
            continue;
        }
        if (command.type != stitch::CommandType::Stitch) {
            connected = false;
            continue;
        }
        if (connected) {
            const double mx = 0.5 * (double(command.pos.x.value) + double(last.x.value));
            const double my = 0.5 * (double(command.pos.y.value) + double(last.y.value));
            out.back()[mx < 0.0 ? 0 : 1][my < 0.0 ? 0 : 1] +=
                std::hypot(double(command.pos.x.value) - last.x.value,
                           double(command.pos.y.value) - last.y.value) /
                1000.0;
        }
        last = command.pos;
        connected = true;
    }
    return out;
}

double total(const std::array<std::array<double, 2>, 2>& q) {
    return q[0][0] + q[0][1] + q[1][0] + q[1][1];
}

} // namespace

TEST_CASE("image weights: layers partition the shape like one plain fill",
          "[project_ops][gradient][image_blend][coverage]") {
    // Référence : la MÊME grille de rangées cousue par un seul fil (toutes les parts au fil 0).
    auto single = rectangle_fill();
    {
        commands::UndoStack refStack;
        auto spec = grid_spec(single);
        for (std::size_t i = 0; i < spec.image_weights->values.size(); i += 3) {
            spec.image_weights->values[i] = 255;
            spec.image_weights->values[i + 1] = 0;
            spec.image_weights->values[i + 2] = 0;
        }
        auto ref =
            project_ops::make_create_multicolor_gradient_command(single.project, single.fill, spec);
        REQUIRE(ref);
        refStack.execute(std::move(*ref), single.project);
    }
    const auto plainByThread = sewn_by_quadrant(single.project);
    REQUIRE(!plainByThread.empty());
    const auto& plain0 = plainByThread.front();
    std::array<std::array<double, 2>, 2> plainSum{};
    for (const auto& thread : plainByThread) {
        for (int qx = 0; qx < 2; ++qx) {
            for (int qy = 0; qy < 2; ++qy) {
                plainSum[qx][qy] += thread[qx][qy];
            }
        }
    }
    CHECK(total(plain0) == Catch::Approx(total(plainSum))); // un seul fil cousu
    auto f = rectangle_fill();
    commands::UndoStack stack;
    auto command =
        project_ops::make_create_multicolor_gradient_command(f.project, f.fill, grid_spec(f));
    REQUIRE(command);
    stack.execute(std::move(*command), f.project);
    const auto sewn = sewn_by_quadrant(f.project);
    REQUIRE(sewn.size() == 3);
    for (int qx = 0; qx < 2; ++qx) {
        for (int qy = 0; qy < 2; ++qy) {
            double sum = 0.0;
            for (const auto& thread : sewn) {
                sum += thread[qx][qy];
            }
            const double ratio = sum / plainSum[qx][qy];
            CAPTURE(qx, qy, ratio);
            // Chaque rangée cousue par exactement un fil. Mesuré (2026-11-17) : +7 % de points
            // d'arrêt aux bouts des morceaux, +5 % de liaisons, et jusqu'à +5 % de trajets
            // cachés : un fil du fond rejoint son morceau suivant en point courant le long de
            // la rangée, SOUS le morceau qu'un fil suivant y coud. Un trou ou une rangée
            // doublée ferait sortir de [0,95 ; 1,20].
            CHECK(ratio > 0.95);
            CHECK(ratio < 1.20);
        }
    }
    // Chaque fil domine là où sa part est forte : fil 0 en bas à gauche, fil 2 en bas à
    // droite, fil 1 en haut plutôt qu'en bas.
    CHECK(sewn[0][0][0] > sewn[2][0][0]);
    CHECK(sewn[2][1][0] > sewn[0][1][0]);
    CHECK(sewn[1][0][1] + sewn[1][1][1] > 2.0 * (sewn[1][0][0] + sewn[1][1][0]));
    // Les trois fils sont réellement cousus.
    for (const auto& thread : sewn) {
        CHECK(total(thread) > 20.0);
    }
}

TEST_CASE("image weights: deterministic and survive a file round trip",
          "[project_ops][gradient][image_blend][io]") {
    auto f = rectangle_fill();
    commands::UndoStack stack;
    const auto spec = grid_spec(f);
    auto command = project_ops::make_create_multicolor_gradient_command(f.project, f.fill, spec);
    REQUIRE(command);
    stack.execute(std::move(*command), f.project);
    const auto first = stitch_generation::effective_sequence(f.project);
    const auto second = stitch_generation::effective_sequence(f.project);
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first->commands == second->commands);

    const auto file = std::filesystem::temp_directory_path() / "openstitch_image_weights.osp";
    REQUIRE(project_io::save_project(file, f.project).has_value());
    auto reopened = project_io::load_project(file);
    std::filesystem::remove(file);
    REQUIRE(reopened);
    const auto reloaded = stitch_generation::effective_sequence(*reopened);
    REQUIRE(reloaded);
    CHECK(reloaded->commands == first->commands);
    const auto described = project_ops::describe_multicolor_gradient(*reopened, f.fill);
    REQUIRE(described);
    REQUIRE(described->image_weights);
    CHECK(*described->image_weights == *spec.image_weights);
    int slices = 0;
    for (const auto& object : reopened->embroidery_objects) {
        if (const auto* p = std::get_if<document::DirectionalFillParams>(&object.params);
            p != nullptr && p->density_gradient && p->density_gradient->slice_grid) {
            ++slices;
            CHECK(p->density_gradient->shared_rows);
            CHECK(p->density_gradient->slice_grid->channels == 2);
        }
    }
    CHECK(slices == 3);
}

TEST_CASE("image weights: inconsistent grids and incompatible modes are refused",
          "[project_ops][gradient][image_blend]") {
    auto f = rectangle_fill();
    auto spec = grid_spec(f);
    SECTION("one channel per thread") {
        spec.palette.pop_back();
        CHECK_FALSE(project_ops::make_create_multicolor_gradient_command(f.project, f.fill, spec));
    }
    SECTION("each cell sums to 255") {
        spec.image_weights->values[0] =
            static_cast<std::uint8_t>(spec.image_weights->values[0] - 1);
        CHECK_FALSE(project_ops::make_create_multicolor_gradient_command(f.project, f.fill, spec));
    }
    SECTION("satin and cross hatching") {
        spec.method = document::GradientMethod::CrossHatching;
        CHECK_FALSE(project_ops::make_create_multicolor_gradient_command(f.project, f.fill, spec));
    }
    CHECK(f.project.embroidery_objects.size() == 1); // rien n'a été créé
}

TEST_CASE("image weights: continuous rows follow each thread share",
          "[project_ops][gradient][image_blend][coverage]") {
    auto f = rectangle_fill();
    commands::UndoStack stack;
    auto spec = grid_spec(f);
    spec.complementary = false; // rangées continues par fil, écart selon la part
    auto command = project_ops::make_create_multicolor_gradient_command(f.project, f.fill, spec);
    REQUIRE(command);
    stack.execute(std::move(*command), f.project);
    int layers = 0;
    for (const auto& object : f.project.embroidery_objects) {
        if (const auto* p = std::get_if<document::DirectionalFillParams>(&object.params);
            p != nullptr && p->density_gradient && p->density_gradient->slice_grid) {
            ++layers;
            CHECK_FALSE(p->density_gradient->shared_rows);
        }
    }
    CHECK(layers == 3);
    const auto sewn = sewn_by_quadrant(f.project);
    REQUIRE(sewn.size() == 3);
    CHECK(sewn[0][0][0] > sewn[2][0][0]);
    CHECK(sewn[2][1][0] > sewn[0][1][0]);
    // Écart plafonné à 4 mm (densité plancher de 10 %) : le contraste haut/bas du fil 1 est
    // moins marqué qu'en grille partagée, mais net.
    const double top = sewn[1][0][1] + sewn[1][1][1];
    const double bottom = sewn[1][0][0] + sewn[1][1][0];
    CAPTURE(top, bottom);
    CHECK(top > 1.3 * bottom);
    // Survit à un aller-retour de fichier (mode non partagé conservé).
    const auto file = std::filesystem::temp_directory_path() / "openstitch_image_rows.osp";
    REQUIRE(project_io::save_project(file, f.project).has_value());
    auto reopened = project_io::load_project(file);
    std::filesystem::remove(file);
    REQUIRE(reopened);
    const auto a = stitch_generation::effective_sequence(f.project);
    const auto b = stitch_generation::effective_sequence(*reopened);
    REQUIRE(a);
    REQUIRE(b);
    CHECK(a->commands == b->commands);
}

TEST_CASE("image blend: parts follow the source image", "[project_ops][gradient][image_blend]") {
    auto f = rectangle_fill();
    // Image de 60 × 40 px à 0,5 mm/px (30 × 20 mm) : rouge à gauche, bleu à droite, rampe
    // linéaire en lumière entre les deux.
    image::Image img;
    img.width = 60;
    img.height = 40;
    img.rgba.resize(static_cast<std::size_t>(img.width * img.height) * 4);
    const std::array<std::uint8_t, 3> red{220, 20, 20};
    const std::array<std::uint8_t, 3> blue{20, 40, 220};
    const auto toSrgb = [](double lin) {
        lin = std::clamp(lin, 0.0, 1.0);
        const double s = lin <= 0.0031308 ? lin * 12.92 : 1.055 * std::pow(lin, 1 / 2.4) - 0.055;
        return static_cast<std::uint8_t>(std::lround(s * 255.0));
    };
    for (int y = 0; y < img.height; ++y) {
        for (int x = 0; x < img.width; ++x) {
            const double t = (x + 0.5) / img.width;
            auto* px = &img.rgba[static_cast<std::size_t>(y * img.width + x) * 4];
            for (int k = 0; k < 3; ++k) {
                const double lin = (1 - t) * autodigitize::srgb_to_linear(red[k]) +
                                   t * autodigitize::srgb_to_linear(blue[k]);
                px[k] = toSrgb(lin);
            }
            px[3] = 255;
        }
    }
    f.project.original = img;
    f.project.mm_per_px = Millimeters{0.5};

    project_ops::ImageBlendRequest request;
    request.colors = {red, blue};
    request.locality = 0.0;
    commands::UndoStack stack;
    auto command = project_ops::make_image_blend_command(f.project, f.fill, request);
    REQUIRE(command);
    stack.execute(std::move(*command), f.project);
    const auto spec = project_ops::describe_multicolor_gradient(f.project, f.fill);
    REQUIRE(spec);
    REQUIRE(spec->image_weights);
    const auto& g = *spec->image_weights;
    REQUIRE(g.channels == 2);
    const auto red_at = [&](int c, int r) {
        return g.values[static_cast<std::size_t>(r * g.columns + c) * 2] / 255.0;
    };
    const int mid = g.rows / 2;
    CHECK(red_at(0, mid) > 0.9);
    CHECK(red_at(g.columns - 1, mid) < 0.1);
    CHECK(red_at(g.columns / 2, mid) == Catch::Approx(0.5).margin(0.1));
    // Une seule annulation défait tout.
    stack.undo(f.project);
    CHECK(f.project.embroidery_objects.size() == 1);

    // Proposition de deux couleurs : les deux extrêmes de la rampe.
    auto proposed = autodigitize::propose_blend_palette(img, Millimeters{0.5},
                                                        f.project.vector_objects[0].paths, 2);
    REQUIRE(proposed);
    REQUIRE(proposed->size() == 2);
    CAPTURE(int((*proposed)[0][0]), int((*proposed)[0][1]), int((*proposed)[0][2]),
            int((*proposed)[1][0]), int((*proposed)[1][1]), int((*proposed)[1][2]));
    CHECK(std::abs(int((*proposed)[0][2]) - int(blue[2])) < 40);
    CHECK(std::abs(int((*proposed)[1][0]) - int(red[0])) < 40);
}

TEST_CASE("image blend: mixing weights reproduce a mixture of the threads",
          "[autodigitize][image_blend]") {
    const std::vector<std::array<double, 3>> colors{
        {0.8, 0.1, 0.1}, {0.1, 0.7, 0.1}, {0.1, 0.1, 0.9}, {0.9, 0.9, 0.9}};
    const std::array<double, 3> target{0.3 * 0.8 + 0.7 * 0.1, 0.3 * 0.1 + 0.7 * 0.7,
                                       0.3 * 0.1 + 0.7 * 0.1};
    double residual = 1.0;
    const auto w = autodigitize::mix_weights(target, colors, 0.0, 4, &residual);
    REQUIRE(w.size() == 4);
    CHECK(w[0] == Catch::Approx(0.3).margin(1e-6));
    CHECK(w[1] == Catch::Approx(0.7).margin(1e-6));
    CHECK(residual < 1e-12);
    // Au plus deux fils non nuls si on le demande, somme 1, parts positives.
    const auto two = autodigitize::mix_weights({0.5, 0.5, 0.5}, colors, 0.05, 2);
    int nonZero = 0;
    double sum = 0.0;
    for (const double v : two) {
        CHECK(v >= 0.0);
        nonZero += v > 1e-9 ? 1 : 0;
        sum += v;
    }
    CHECK(nonZero <= 2);
    CHECK(sum == Catch::Approx(1.0));
}

namespace {

// Îlots de 4 mm au pas de 8 mm (12 îlots) : un fil n'occupe qu'eux, l'autre le reste. Rangées
// horizontales, rangées continues par fil.
project_ops::MulticolorGradientSpec checker_spec(const Fixture& f, bool thread0_in_even) {
    auto spec = grid_spec(f);
    spec.palette.resize(2);
    spec.complementary = false;
    spec.stitch_angle = Angle{0.0};
    auto& g = *spec.image_weights;
    g.channels = 2;
    g.values.assign(static_cast<std::size_t>(g.columns * g.rows) * 2, 0);
    for (int r = 0; r < g.rows; ++r) {
        for (int c = 0; c < g.columns; ++c) {
            const bool even = c % 8 < 4 && r % 8 < 4;
            const bool zero = even == thread0_in_even;
            auto* v = &g.values[static_cast<std::size_t>(r * g.columns + c) * 2];
            v[0] = zero ? 255 : 0;
            v[1] = zero ? 0 : 255;
        }
    }
    return spec;
}

// Coupes par bloc de couleur.
std::vector<int> trims_per_color(const document::Project& p) {
    const auto sequence = stitch_generation::effective_sequence(p);
    REQUIRE(sequence);
    std::vector<int> out(1, 0);
    for (const auto& command : sequence->commands) {
        if (command.type == stitch::CommandType::ColorChange) {
            out.push_back(0);
        } else if (command.type == stitch::CommandType::Trim) {
            ++out.back();
        }
    }
    return out;
}

} // namespace

TEST_CASE("image weights: a scattered back layer travels hidden under later layers",
          "[project_ops][gradient][image_blend][trims]") {
    // Mêmes îlots épars, une fois au fil du FOND (couvert ensuite par le fil 1 : trajets
    // cachés permis), une fois au fil du DESSUS (rien par-dessus : il doit couper).
    auto back = rectangle_fill();
    auto front = rectangle_fill();
    commands::UndoStack s1;
    commands::UndoStack s2;
    {
        auto spec = checker_spec(back, true);
        auto cmd =
            project_ops::make_create_multicolor_gradient_command(back.project, back.fill, spec);
        REQUIRE(cmd);
        s1.execute(std::move(*cmd), back.project);
    }
    {
        // Motif inverse : les îlots épars appartiennent au fil 1.
        auto spec = checker_spec(front, false);
        auto cmd =
            project_ops::make_create_multicolor_gradient_command(front.project, front.fill, spec);
        REQUIRE(cmd);
        s2.execute(std::move(*cmd), front.project);
    }
    const auto backTrims = trims_per_color(back.project);
    const auto frontTrims = trims_per_color(front.project);
    REQUIRE(backTrims.size() >= 2);
    REQUIRE(frontTrims.size() >= 2);
    // 12 îlots épars : la couche du dessus coupe entre presque chacun ; celle du fond voyage
    // sous le fil suivant.
    CHECK(frontTrims[1] >= 6);
    CHECK(backTrims[0] * 3 <= frontTrims[1]);
    // Déterminisme.
    CHECK(trims_per_color(back.project) == backTrims);
}
