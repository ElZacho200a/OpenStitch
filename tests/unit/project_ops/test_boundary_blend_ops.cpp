// SPDX-License-Identifier: Apache-2.0
// Fondu à la frontière, lot L2 : lien du document, commandes, génération, .osp, opérations
// (docs/studio-fondu-frontiere-spec.md §4, §7).
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

#include "openstitch/commands/undo_stack.hpp"
#include "openstitch/document/boundary_blend.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/project_ops/boundary_blend.hpp"
#include "openstitch/project_ops/inspect.hpp"
#include "openstitch/project_ops/operations.hpp"
#include "openstitch/stitch_generation/generate.hpp"

using namespace openstitch;
using json = nlohmann::json;

namespace {

document::VectorObject rect_shape(ObjectId id, int x_mm, int y_mm, int w_mm, int h_mm) {
    document::VectorObject v;
    v.id = id;
    v.name = "Zone " + std::to_string(id.value);
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

// Ids : 1 vecteur gauche, 2 tatami bleu ; 3 vecteur droit, 4 tatami rouge ; 5 vecteur lointain,
// 6 tatami vert ; 7 vecteur satin, 8 satin (non pris en charge).
document::Project make_project() {
    document::Project p;
    const auto add = [&](int x_mm, int w_mm, std::array<std::uint8_t, 3> rgb,
                         document::StitchParams params) {
        const ObjectId v = p.object_ids.next();
        p.vector_objects.push_back(rect_shape(v, x_mm, 0, w_mm, 20));
        document::EmbroideryObject e;
        e.id = p.object_ids.next();
        e.name = "Zone";
        e.source_vector = v;
        e.rgb = rgb;
        e.params = std::move(params);
        p.embroidery_objects.push_back(std::move(e));
    };
    add(0, 20, {0, 0, 255}, document::TatamiParams{});
    add(20, 20, {255, 0, 0}, document::TatamiParams{});
    add(60, 20, {0, 255, 0}, document::TatamiParams{});
    add(80, 20, {9, 9, 9}, document::RunningStitchParams{});
    return p;
}

std::size_t stitches_of(const stitch::StitchSequence& s, ObjectId id, double xMin, double xMax) {
    std::size_t n = 0;
    for (const auto& c : s.commands) {
        if (c.source == id && c.type == stitch::CommandType::Stitch && c.pos.x.value >= xMin &&
            c.pos.x.value <= xMax) {
            ++n;
        }
    }
    return n;
}

} // namespace

TEST_CASE("boundary blend command links both zones in one undo step", "[project_ops][blend]") {
    auto project = make_project();
    const auto before = project;
    commands::UndoStack stack;
    auto cmd = project_ops::make_boundary_blend_command(project, ObjectId{2}, ObjectId{4},
                                                        {Micrometers{2'000}, Micrometers{3'000}});
    REQUIRE(cmd.has_value());
    stack.execute(std::move(*cmd), project);
    const auto* a = project.findEmbroidery(ObjectId{2});
    const auto* b = project.findEmbroidery(ObjectId{4});
    REQUIRE(a->boundary_blend.has_value());
    REQUIRE(b->boundary_blend.has_value());
    CHECK(a->boundary_blend->partner == ObjectId{4});
    CHECK(a->boundary_blend->width_self == Micrometers{2'000});
    CHECK(b->boundary_blend->width_self == Micrometers{3'000});
    CHECK(document::find_boundary_blend(project, ObjectId{2}).has_value());
    CHECK(a->params == before.findEmbroidery(ObjectId{2})->params);

    const auto info = project_ops::boundary_blend_info(project, ObjectId{4});
    REQUIRE(info.has_value());
    CHECK(info->active);
    CHECK(info->options.width_a == Micrometers{3'000});
    CHECK(info->options.width_b == Micrometers{2'000});
    CHECK_FALSE(info->frontier.empty());

    stack.undo(project);
    CHECK_FALSE(project.findEmbroidery(ObjectId{2})->boundary_blend.has_value());
    CHECK_FALSE(project.findEmbroidery(ObjectId{4})->boundary_blend.has_value());
}

TEST_CASE("boundary blend refusals carry stable codes", "[project_ops][blend]") {
    auto project = make_project();
    const auto code = [&](ObjectId a, ObjectId b, project_ops::BoundaryBlendOptions o = {}) {
        const auto r = project_ops::boundary_blend_refusal(project, a, b, o);
        return r ? r->code : std::string{};
    };
    CHECK(code(ObjectId{2}, ObjectId{4}).empty());
    CHECK(code(ObjectId{2}, ObjectId{2}) == "E_BLEND_SAME_OBJECT");
    CHECK(code(ObjectId{2}, ObjectId{6}) == "E_BLEND_NO_FRONTIER");
    CHECK(code(ObjectId{6}, ObjectId{8}) == "E_BLEND_UNSUPPORTED_FILL");
    CHECK(code(ObjectId{2}, ObjectId{4}, {Micrometers{100}, Micrometers{2'500}}) ==
          "E_BLEND_WIDTH_RANGE");
    project.findEmbroidery(ObjectId{4})->rgb = project.findEmbroidery(ObjectId{2})->rgb;
    CHECK(code(ObjectId{2}, ObjectId{4}) == "E_BLEND_SAME_COLOR");
}

TEST_CASE("boundary blend changes generation near the frontier only", "[project_ops][blend]") {
    auto project = make_project();
    const auto plain = stitch_generation::generate_sequence(project); // raw-sequence-ok: test
    REQUIRE(plain.has_value());
    commands::UndoStack stack;
    auto cmd = project_ops::make_boundary_blend_command(project, ObjectId{2}, ObjectId{4}, {});
    REQUIRE(cmd.has_value());
    stack.execute(std::move(*cmd), project);
    const auto blended = stitch_generation::generate_sequence(project); // raw-sequence-ok: test
    REQUIRE(blended.has_value());
    // A (bleu, x ∈ [0 ; 20] mm) coud maintenant dans la demi-bande de B (x ∈ ]20 ; 22,5] mm).
    CHECK(stitches_of(*plain, ObjectId{2}, 20'300.0, 22'500.0) == 0);
    CHECK(stitches_of(*blended, ObjectId{2}, 20'300.0, 22'500.0) > 0);
    CHECK(stitches_of(*blended, ObjectId{4}, 17'500.0, 19'700.0) > 0);
    // Loin de la frontière, la zone verte (sans lien) est identique.
    CHECK(stitches_of(*plain, ObjectId{6}, 0.0, 1e9) ==
          stitches_of(*blended, ObjectId{6}, 0.0, 1e9));
    // Déterminisme.
    const auto again = stitch_generation::generate_sequence(project); // raw-sequence-ok: test
    REQUIRE(again.has_value());
    CHECK(again->commands == blended->commands);
}

TEST_CASE("blended zone keeps its underlay out of its half band", "[project_ops][blend]") {
    auto project = make_project();
    for (const ObjectId id : {ObjectId{2}, ObjectId{4}}) {
        std::get<document::TatamiParams>(project.findEmbroidery(id)->params).underlay_edge = true;
    }
    // Sous-couche cousue par l'objet `id` avec x dans [xMin ; xMax].
    const auto underlay = [](const stitch::StitchSequence& s, ObjectId id, double xMin,
                             double xMax) {
        std::size_t n = 0;
        for (const auto& c : s.commands) {
            if (c.source == id && c.type == stitch::CommandType::Stitch &&
                c.pass == stitch::StitchPass::Underlay && c.pos.x.value >= xMin &&
                c.pos.x.value <= xMax) {
                ++n;
            }
        }
        return n;
    };
    const auto plain = stitch_generation::generate_sequence(project); // raw-sequence-ok: test
    REQUIRE(plain.has_value());
    // Sans fondu, le tour de bord de A longe la frontière (x = 20 mm).
    REQUIRE(underlay(*plain, ObjectId{2}, 18'000.0, 20'000.0) > 0);
    commands::UndoStack stack;
    auto cmd = project_ops::make_boundary_blend_command(project, ObjectId{2}, ObjectId{4}, {});
    REQUIRE(cmd.has_value());
    stack.execute(std::move(*cmd), project);
    const auto blended = stitch_generation::generate_sequence(project); // raw-sequence-ok: test
    REQUIRE(blended.has_value());
    // Fondu : plus aucune sous-couche dans les demi-bandes (2,5 mm de chaque côté), où la couche
    // supérieure clairsemée ne la couvrirait plus. Ailleurs, la sous-couche reste.
    CHECK(underlay(*blended, ObjectId{2}, 17'600.0, 1e9) == 0);
    CHECK(underlay(*blended, ObjectId{4}, -1e9, 22'400.0) == 0);
    CHECK(underlay(*blended, ObjectId{2}, 0.0, 17'500.0) > 0);
    CHECK(underlay(*blended, ObjectId{4}, 22'500.0, 40'000.0) > 0);
}

TEST_CASE("row direction setting is stored, saved and exposed", "[project_ops][blend][rows]") {
    auto project = make_project();
    commands::UndoStack stack;
    project_ops::BoundaryBlendOptions options;
    options.rows = document::BoundaryBlendRows::Rotating;
    auto cmd = project_ops::make_boundary_blend_command(project, ObjectId{2}, ObjectId{4}, options);
    REQUIRE(cmd.has_value());
    stack.execute(std::move(*cmd), project);
    CHECK(project.findEmbroidery(ObjectId{2})->boundary_blend->rows ==
          document::BoundaryBlendRows::Rotating);
    CHECK(project.findEmbroidery(ObjectId{4})->boundary_blend->rows ==
          document::BoundaryBlendRows::Rotating);
    const auto info = project_ops::boundary_blend_info(project, ObjectId{4});
    REQUIRE(info.has_value());
    CHECK(info->options.rows == document::BoundaryBlendRows::Rotating);
    // Liens incohérents (un seul côté modifié) : fondu sans effet.
    auto broken = project;
    broken.findEmbroidery(ObjectId{4})->boundary_blend->rows = document::BoundaryBlendRows::Shared;
    CHECK_FALSE(document::find_boundary_blend(broken, ObjectId{2}).has_value());
    // Aller-retour .osp.
    const auto path = std::filesystem::temp_directory_path() / "os_blend_rows.osp";
    REQUIRE(project_io::save_project(path, project).has_value());
    const auto loaded = project_io::load_project(path);
    REQUIRE(loaded.has_value());
    CHECK(loaded->findEmbroidery(ObjectId{2})->boundary_blend->rows ==
          document::BoundaryBlendRows::Rotating);
    std::filesystem::remove(path);
    // Opération : argument `rows`, inspect renvoie la valeur.
    auto viaOp = make_project();
    const auto r = project_ops::apply_operation(
        viaOp, R"({"op":"blend_boundary","a":2,"b":4,"rows":"shared"})");
    INFO(r.to_json());
    REQUIRE(r.ok);
    CHECK(viaOp.findEmbroidery(ObjectId{2})->boundary_blend->rows ==
          document::BoundaryBlendRows::Shared);
    const auto text = project_ops::inspect_project_json(viaOp);
    REQUIRE(text.has_value());
    const json doc = json::parse(*text);
    bool found = false;
    for (const auto& object : doc["embroidery_objects"]) {
        if (object["id"] == 2) {
            CHECK(object["boundary_blend"]["rows"] == "shared");
            found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("removing one link or zone deactivates the blend", "[project_ops][blend]") {
    auto project = make_project();
    commands::UndoStack stack;
    auto cmd = project_ops::make_boundary_blend_command(project, ObjectId{2}, ObjectId{4}, {});
    REQUIRE(cmd.has_value());
    stack.execute(std::move(*cmd), project);
    auto rm = project_ops::make_remove_boundary_blend_command(project, ObjectId{4});
    REQUIRE(rm.has_value());
    stack.execute(std::move(*rm), project);
    CHECK_FALSE(project.findEmbroidery(ObjectId{2})->boundary_blend.has_value());
    CHECK_FALSE(project.findEmbroidery(ObjectId{4})->boundary_blend.has_value());
    stack.undo(project);
    CHECK(document::find_boundary_blend(project, ObjectId{2}).has_value());
    // Zone convertie en point droit : lien orphelin, sans effet.
    project.findEmbroidery(ObjectId{4})->params = document::RunningStitchParams{};
    CHECK_FALSE(document::find_boundary_blend(project, ObjectId{2}).has_value());
    CHECK(project_ops::make_remove_boundary_blend_command(project, ObjectId{6}).error().code ==
          "E_BLEND_NONE");
}

TEST_CASE("boundary blend survives a .osp round trip", "[project_ops][blend]") {
    auto project = make_project();
    commands::UndoStack stack;
    auto cmd = project_ops::make_boundary_blend_command(
        project, ObjectId{2}, ObjectId{4},
        {Micrometers{1'500}, Micrometers{4'000}, document::DensityCurve::Smooth});
    REQUIRE(cmd.has_value());
    stack.execute(std::move(*cmd), project);
    static const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    static std::atomic<int> counter{0};
    const auto path =
        std::filesystem::temp_directory_path() /
        ("openstitch_blend_" + std::to_string(nonce) + "_" + std::to_string(counter++) + ".osp");
    REQUIRE(project_io::save_project(path, project).has_value());
    const auto loaded = project_io::load_project(path);
    std::filesystem::remove(path);
    REQUIRE(loaded.has_value());
    const auto* a = loaded->findEmbroidery(ObjectId{2});
    REQUIRE(a->boundary_blend.has_value());
    CHECK(*a->boundary_blend == *project.findEmbroidery(ObjectId{2})->boundary_blend);
    CHECK(*loaded->findEmbroidery(ObjectId{4})->boundary_blend ==
          *project.findEmbroidery(ObjectId{4})->boundary_blend);
}

TEST_CASE("blend_boundary and unblend_boundary operations", "[project_ops][blend][ops]") {
    auto project = make_project();
    const auto ok = project_ops::apply_operation(
        project, R"({"op":"blend_boundary","a":2,"b":4,"width_a":2,"width_b":3,"curve":"smooth"})");
    INFO(ok.to_json());
    REQUIRE(ok.ok);
    CHECK(project.findEmbroidery(ObjectId{4})->boundary_blend->width_self == Micrometers{3'000});
    CHECK(project.findEmbroidery(ObjectId{2})->boundary_blend->curve ==
          document::DensityCurve::Smooth);

    const auto far =
        project_ops::apply_operation(project, R"({"op":"blend_boundary","a":2,"b":6})");
    CHECK_FALSE(far.ok);
    CHECK(far.to_json().find("E_BLEND_") != std::string::npos);

    const auto off =
        project_ops::apply_operation(project, R"({"op":"unblend_boundary","object":4})");
    REQUIRE(off.ok);
    CHECK_FALSE(project.findEmbroidery(ObjectId{2})->boundary_blend.has_value());
}

TEST_CASE("inspect reports the boundary blend of each zone", "[project_ops][blend]") {
    auto project = make_project();
    commands::UndoStack stack;
    auto cmd = project_ops::make_boundary_blend_command(project, ObjectId{2}, ObjectId{4},
                                                        {Micrometers{2'000}, Micrometers{3'000}});
    REQUIRE(cmd.has_value());
    stack.execute(std::move(*cmd), project);
    const auto text = project_ops::inspect_project_json(project);
    REQUIRE(text.has_value());
    const json doc = json::parse(*text);
    bool found = false;
    for (const auto& object : doc["embroidery_objects"]) {
        if (object["id"] == 4) {
            REQUIRE(object.contains("boundary_blend"));
            CHECK(object["boundary_blend"]["partner"] == 2);
            CHECK(object["boundary_blend"]["width_mm"] == 3.0);
            CHECK(object["boundary_blend"]["partner_width_mm"] == 2.0);
            CHECK(object["boundary_blend"]["active"] == true);
            CHECK(object["boundary_blend"]["frontier_mm"].get<double>() > 15.0);
            found = true;
        }
    }
    CHECK(found);
}

// --- L4 : frontière tracée à la main ----------------------------------------------------------

TEST_CASE("a hand-drawn frontier blends zones that do not touch",
          "[project_ops][blend][frontier]") {
    auto project = make_project();
    // Écarte la zone rouge : jour de 3 mm entre x = 20 et x = 23 mm.
    project.vector_objects[1] = rect_shape(ObjectId{3}, 23, 0, 20, 20);
    CHECK(project_ops::boundary_blend_refusal(project, ObjectId{2}, ObjectId{4})->code ==
          "E_BLEND_NO_FRONTIER");

    project_ops::BoundaryBlendOptions options;
    options.width_a = Micrometers{4'000};
    options.width_b = Micrometers{4'000};
    options.manual_frontier = {Vec2um{Micrometers{21'500}, Micrometers{-1'000}},
                               Vec2um{Micrometers{21'500}, Micrometers{21'000}}};
    CHECK_FALSE(project_ops::boundary_blend_refusal(project, ObjectId{2}, ObjectId{4}, options));
    commands::UndoStack stack;
    auto cmd = project_ops::make_boundary_blend_command(project, ObjectId{2}, ObjectId{4}, options);
    REQUIRE(cmd.has_value());
    stack.execute(std::move(*cmd), project);
    REQUIRE(document::find_boundary_blend(project, ObjectId{2}).has_value());
    CHECK(project.findEmbroidery(ObjectId{4})->boundary_blend->manual_frontier ==
          options.manual_frontier);
    const auto info = project_ops::boundary_blend_info(project, ObjectId{2});
    REQUIRE(info.has_value());
    CHECK(info->active);
    REQUIRE(info->frontier.size() == 1);
    CHECK(info->frontier.front().points == options.manual_frontier);

    // La zone bleue coud maintenant au-delà du jour, dans la demi-bande de la rouge.
    const auto blended = stitch_generation::generate_sequence(project); // raw-sequence-ok: test
    REQUIRE(blended.has_value());
    CHECK(stitches_of(*blended, ObjectId{2}, 23'300.0, 25'500.0) > 0);
    CHECK(stitches_of(*blended, ObjectId{4}, 17'500.0, 19'700.0) > 0);

    // Aller-retour .osp : la frontière tracée est conservée.
    static const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
                      ("openstitch_blend_frontier_" + std::to_string(nonce) + ".osp");
    REQUIRE(project_io::save_project(path, project).has_value());
    const auto loaded = project_io::load_project(path);
    std::filesystem::remove(path);
    REQUIRE(loaded.has_value());
    CHECK(loaded->findEmbroidery(ObjectId{2})->boundary_blend->manual_frontier ==
          options.manual_frontier);
}

TEST_CASE("hand-drawn frontier refusals", "[project_ops][blend][frontier]") {
    auto project = make_project();
    project_ops::BoundaryBlendOptions far;
    far.manual_frontier = {Vec2um{Micrometers{48'000}, Micrometers{0}},
                           Vec2um{Micrometers{48'000}, Micrometers{20'000}}};
    // Zones 2 (x ∈ [0 ; 20]) et 6 (x ∈ [60 ; 80]) : à 28 et 12 mm de la ligne, demi-bandes 2,5 mm.
    CHECK(project_ops::boundary_blend_refusal(project, ObjectId{2}, ObjectId{6}, far)->code ==
          "E_BLEND_FRONTIER_FAR");
    project_ops::BoundaryBlendOptions single;
    single.manual_frontier = {Vec2um{Micrometers{20'000}, Micrometers{5'000}}};
    CHECK(project_ops::boundary_blend_refusal(project, ObjectId{2}, ObjectId{4}, single)->code ==
          "E_BLEND_FRONTIER_SHORT");
}

TEST_CASE("blend_boundary operation accepts and clears a drawn frontier",
          "[project_ops][blend][frontier][ops]") {
    auto project = make_project();
    const auto drawn = project_ops::apply_operation(
        project, R"({"op":"blend_boundary","a":2,"b":4,"frontier":[[20,-1],[20,10],[21,21]]})");
    INFO(drawn.to_json());
    REQUIRE(drawn.ok);
    CHECK(project.findEmbroidery(ObjectId{2})->boundary_blend->manual_frontier.size() == 3);
    CHECK(drawn.to_json().find("manual_frontier") != std::string::npos);

    const auto cleared = project_ops::apply_operation(
        project, R"({"op":"blend_boundary","a":2,"b":4,"frontier":[]})");
    REQUIRE(cleared.ok);
    CHECK(project.findEmbroidery(ObjectId{2})->boundary_blend->manual_frontier.empty());
    CHECK(document::find_boundary_blend(project, ObjectId{2}).has_value());
}

TEST_CASE("row direction setting changes generation deterministically",
          "[project_ops][blend][rows]") {
    // Angles différents : PerZone croise les trames, Shared et Rotating partagent la bande.
    const auto generate = [](document::BoundaryBlendRows rows) {
        auto project = make_project();
        std::get<document::TatamiParams>(project.findEmbroidery(ObjectId{2})->params).angle =
            Angle{0.785};
        std::get<document::TatamiParams>(project.findEmbroidery(ObjectId{4})->params).angle =
            Angle{-0.52};
        commands::UndoStack stack;
        project_ops::BoundaryBlendOptions options;
        options.rows = rows;
        auto cmd =
            project_ops::make_boundary_blend_command(project, ObjectId{2}, ObjectId{4}, options);
        REQUIRE(cmd.has_value());
        stack.execute(std::move(*cmd), project);
        auto seq = stitch_generation::generate_sequence(project); // raw-sequence-ok: test
        REQUIRE(seq.has_value());
        return seq->commands;
    };
    const auto perZone = generate(document::BoundaryBlendRows::PerZone);
    const auto shared = generate(document::BoundaryBlendRows::Shared);
    const auto rotating = generate(document::BoundaryBlendRows::Rotating);
    CHECK(shared != perZone);
    CHECK(rotating != perZone);
    CHECK(rotating != shared);
    CHECK(generate(document::BoundaryBlendRows::Shared) == shared);
    CHECK(generate(document::BoundaryBlendRows::Rotating) == rotating);
}
