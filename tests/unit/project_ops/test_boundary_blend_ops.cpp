// SPDX-License-Identifier: Apache-2.0
// Fondu à la frontière, lot L2 : lien du document, commandes, génération, .osp, opérations
// (docs/studio-fondu-frontiere-spec.md §4, §7).
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "openstitch/commands/insert_design_command.hpp"
#include "openstitch/commands/undo_stack.hpp"
#include "openstitch/commands/vector_ops.hpp"
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
    REQUIRE_FALSE(a->boundary_blends.empty());
    REQUIRE_FALSE(b->boundary_blends.empty());
    CHECK(a->boundary_blends.front().partner == ObjectId{4});
    CHECK(a->boundary_blends.front().width_self == Micrometers{2'000});
    CHECK(b->boundary_blends.front().width_self == Micrometers{3'000});
    CHECK(document::find_boundary_blend(project, ObjectId{2}, ObjectId{4}).has_value());
    CHECK(a->params == before.findEmbroidery(ObjectId{2})->params);

    const auto info = project_ops::boundary_blend_info(project, ObjectId{4});
    REQUIRE(info.has_value());
    CHECK(info->active);
    CHECK(info->options.width_a == Micrometers{3'000});
    CHECK(info->options.width_b == Micrometers{2'000});
    CHECK_FALSE(info->frontier.empty());

    stack.undo(project);
    CHECK(project.findEmbroidery(ObjectId{2})->boundary_blends.empty());
    CHECK(project.findEmbroidery(ObjectId{4})->boundary_blends.empty());
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
    CHECK(project.findEmbroidery(ObjectId{2})->boundary_blends.front().rows ==
          document::BoundaryBlendRows::Rotating);
    CHECK(project.findEmbroidery(ObjectId{4})->boundary_blends.front().rows ==
          document::BoundaryBlendRows::Rotating);
    const auto info = project_ops::boundary_blend_info(project, ObjectId{4});
    REQUIRE(info.has_value());
    CHECK(info->options.rows == document::BoundaryBlendRows::Rotating);
    // Liens incohérents (un seul côté modifié) : fondu sans effet.
    auto broken = project;
    broken.findEmbroidery(ObjectId{4})->boundary_blends.front().rows =
        document::BoundaryBlendRows::Shared;
    CHECK_FALSE(document::find_boundary_blend(broken, ObjectId{2}, ObjectId{4}).has_value());
    // Aller-retour .osp.
    const auto path = std::filesystem::temp_directory_path() / "os_blend_rows.osp";
    REQUIRE(project_io::save_project(path, project).has_value());
    const auto loaded = project_io::load_project(path);
    REQUIRE(loaded.has_value());
    CHECK(loaded->findEmbroidery(ObjectId{2})->boundary_blends.front().rows ==
          document::BoundaryBlendRows::Rotating);
    std::filesystem::remove(path);
    // Entrelacé : aller-retour .osp aussi (clé `rows` = « interlaced »).
    auto interlaced = project;
    for (const auto id : {ObjectId{2}, ObjectId{4}}) {
        interlaced.findEmbroidery(id)->boundary_blends.front().rows =
            document::BoundaryBlendRows::Interlaced;
    }
    REQUIRE(project_io::save_project(path, interlaced).has_value());
    const auto reloaded = project_io::load_project(path);
    REQUIRE(reloaded.has_value());
    CHECK(reloaded->findEmbroidery(ObjectId{4})->boundary_blends.front().rows ==
          document::BoundaryBlendRows::Interlaced);
    std::filesystem::remove(path);
    // Opération : argument `rows`, inspect renvoie la valeur.
    auto viaOp = make_project();
    const auto r = project_ops::apply_operation(
        viaOp, R"({"op":"blend_boundary","a":2,"b":4,"rows":"shared"})");
    INFO(r.to_json());
    REQUIRE(r.ok);
    CHECK(viaOp.findEmbroidery(ObjectId{2})->boundary_blends.front().rows ==
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
    CHECK(project.findEmbroidery(ObjectId{2})->boundary_blends.empty());
    CHECK(project.findEmbroidery(ObjectId{4})->boundary_blends.empty());
    stack.undo(project);
    CHECK(document::find_boundary_blend(project, ObjectId{2}, ObjectId{4}).has_value());
    // Zone convertie en point droit : lien orphelin, sans effet.
    project.findEmbroidery(ObjectId{4})->params = document::RunningStitchParams{};
    CHECK_FALSE(document::find_boundary_blend(project, ObjectId{2}, ObjectId{4}).has_value());
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
    REQUIRE_FALSE(a->boundary_blends.empty());
    CHECK(a->boundary_blends.front() ==
          project.findEmbroidery(ObjectId{2})->boundary_blends.front());
    CHECK(loaded->findEmbroidery(ObjectId{4})->boundary_blends.front() ==
          project.findEmbroidery(ObjectId{4})->boundary_blends.front());
}

TEST_CASE("blend_boundary and unblend_boundary operations", "[project_ops][blend][ops]") {
    auto project = make_project();
    const auto ok = project_ops::apply_operation(
        project, R"({"op":"blend_boundary","a":2,"b":4,"width_a":2,"width_b":3,"curve":"smooth"})");
    INFO(ok.to_json());
    REQUIRE(ok.ok);
    CHECK(project.findEmbroidery(ObjectId{4})->boundary_blends.front().width_self ==
          Micrometers{3'000});
    CHECK(project.findEmbroidery(ObjectId{2})->boundary_blends.front().curve ==
          document::DensityCurve::Smooth);

    const auto far =
        project_ops::apply_operation(project, R"({"op":"blend_boundary","a":2,"b":6})");
    CHECK_FALSE(far.ok);
    CHECK(far.to_json().find("E_BLEND_") != std::string::npos);

    const auto off =
        project_ops::apply_operation(project, R"({"op":"unblend_boundary","object":4})");
    REQUIRE(off.ok);
    CHECK(project.findEmbroidery(ObjectId{2})->boundary_blends.empty());
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
    REQUIRE(document::find_boundary_blend(project, ObjectId{2}, ObjectId{4}).has_value());
    CHECK(project.findEmbroidery(ObjectId{4})->boundary_blends.front().manual_frontier ==
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
    CHECK(loaded->findEmbroidery(ObjectId{2})->boundary_blends.front().manual_frontier ==
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
    CHECK(project.findEmbroidery(ObjectId{2})->boundary_blends.front().manual_frontier.size() == 3);
    CHECK(drawn.to_json().find("manual_frontier") != std::string::npos);

    const auto cleared = project_ops::apply_operation(
        project, R"({"op":"blend_boundary","a":2,"b":4,"frontier":[]})");
    REQUIRE(cleared.ok);
    CHECK(project.findEmbroidery(ObjectId{2})->boundary_blends.front().manual_frontier.empty());
    CHECK(document::find_boundary_blend(project, ObjectId{2}, ObjectId{4}).has_value());
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
    const auto interlaced = generate(document::BoundaryBlendRows::Interlaced);
    CHECK(shared != perZone);
    CHECK(rotating != perZone);
    CHECK(rotating != shared);
    CHECK(interlaced != perZone);
    CHECK(interlaced != shared);
    CHECK(interlaced != rotating);
    CHECK(generate(document::BoundaryBlendRows::Shared) == shared);
    CHECK(generate(document::BoundaryBlendRows::Rotating) == rotating);
    CHECK(generate(document::BoundaryBlendRows::Interlaced) == interlaced);
}

// --- Plusieurs fondus par zone : un lien par voisine ------------------------------------------

namespace {

// Ids : A = tatami 2 (vecteur 1, 0..20 x 0..20 mm), B = tatami 4 (vecteur 3, à droite de A),
// C = tatami 6 (vecteur 5, au-dessus de A). A touche B et C ; B et C ne se touchent qu'en un coin.
document::Project make_three_zones() {
    document::Project p;
    const auto add = [&](int x_mm, int y_mm, std::array<std::uint8_t, 3> rgb) {
        const ObjectId v = p.object_ids.next();
        p.vector_objects.push_back(rect_shape(v, x_mm, y_mm, 20, 20));
        document::EmbroideryObject e;
        e.id = p.object_ids.next();
        e.name = "Zone";
        e.source_vector = v;
        e.rgb = rgb;
        e.params = document::TatamiParams{};
        p.embroidery_objects.push_back(std::move(e));
    };
    add(0, 0, {0, 0, 255});
    add(20, 0, {255, 0, 0});
    add(0, 20, {0, 255, 0});
    return p;
}

std::vector<document::BoundaryBlendLink> links_of(const document::Project& p, std::uint64_t id) {
    return p.findEmbroidery(ObjectId{id})->boundary_blends;
}

std::vector<std::uint64_t> partners_of(const document::Project& p, std::uint64_t id) {
    std::vector<std::uint64_t> out;
    for (const auto& l : links_of(p, id)) {
        out.push_back(l.partner.value);
    }
    return out;
}

void blend(document::Project& p, commands::UndoStack& stack, std::uint64_t a, std::uint64_t b,
           std::int32_t width_a = 2'500) {
    auto cmd = project_ops::make_boundary_blend_command(p, ObjectId{a}, ObjectId{b},
                                                        {Micrometers{width_a}, Micrometers{2'500}});
    INFO((cmd ? std::string{} : cmd.error().message));
    REQUIRE(cmd.has_value());
    stack.execute(std::move(*cmd), p);
}

const document::EmbroideryObject* copy_of(const document::Project& p,
                                          std::array<std::uint8_t, 3> rgb, std::uint64_t original) {
    for (const auto& e : p.embroidery_objects) {
        if (e.rgb == rgb && e.id.value != original) {
            return &e;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("a zone blends with two neighbours, each couple edited and undone alone",
          "[project_ops][blend][multi]") {
    auto project = make_three_zones();
    commands::UndoStack stack;
    blend(project, stack, 2, 4);
    const auto afterAB = project;
    blend(project, stack, 2, 6); // plus de refus « déjà un fondu avec une autre zone »
    CHECK(partners_of(project, 2) == std::vector<std::uint64_t>{4, 6});
    CHECK(partners_of(project, 4) == std::vector<std::uint64_t>{2});
    CHECK(partners_of(project, 6) == std::vector<std::uint64_t>{2});
    CHECK(links_of(project, 4) == links_of(afterAB, 4));
    REQUIRE(document::find_boundary_blends(project, ObjectId{2}).size() == 2);
    const auto infos = project_ops::boundary_blend_infos(project, ObjectId{2});
    REQUIRE(infos.size() == 2);
    CHECK(infos[0].b == ObjectId{4});
    CHECK(infos[1].b == ObjectId{6});
    CHECK(infos[0].active);
    CHECK(infos[1].active);
    CHECK(project_ops::boundary_blend_info(project, ObjectId{2})->b == ObjectId{4});
    CHECK(project_ops::boundary_blend_info(project, ObjectId{2}, ObjectId{6})->b == ObjectId{6});
    CHECK_FALSE(project_ops::boundary_blend_info(project, ObjectId{4}, ObjectId{6}).has_value());
    const auto afterAC = project;

    // Réédition de A-B : le lien A-C est intact.
    blend(project, stack, 2, 4, 4'000);
    CHECK(document::boundary_blend_link(*project.findEmbroidery(ObjectId{2}), ObjectId{4})
              ->width_self == Micrometers{4'000});
    CHECK(*document::boundary_blend_link(*project.findEmbroidery(ObjectId{2}), ObjectId{6}) ==
          *document::boundary_blend_link(*afterAC.findEmbroidery(ObjectId{2}), ObjectId{6}));
    CHECK(links_of(project, 6) == links_of(afterAC, 6));
    stack.undo(project);
    for (const std::uint64_t id : {2, 4, 6}) {
        CHECK(links_of(project, id) == links_of(afterAC, id));
    }

    // Retrait de A-C seulement.
    auto rm = project_ops::make_remove_boundary_blend_command(project, ObjectId{2}, ObjectId{6});
    REQUIRE(rm.has_value());
    stack.execute(std::move(*rm), project);
    CHECK(partners_of(project, 2) == std::vector<std::uint64_t>{4});
    CHECK(links_of(project, 6).empty());
    CHECK(links_of(project, 4) == links_of(afterAC, 4));
    CHECK(document::find_boundary_blend(project, ObjectId{2}, ObjectId{4}).has_value());
    stack.undo(project);
    for (const std::uint64_t id : {2, 4, 6}) {
        CHECK(links_of(project, id) == links_of(afterAC, id));
    }

    // Annuler la pose de A-C rend exactement l'état d'après A-B.
    stack.undo(project);
    for (const std::uint64_t id : {2, 4, 6}) {
        CHECK(links_of(project, id) == links_of(afterAB, id));
    }
}

TEST_CASE("removing a blend from a zone with several blends needs the partner",
          "[project_ops][blend][multi]") {
    auto project = make_three_zones();
    commands::UndoStack stack;
    blend(project, stack, 2, 4);
    blend(project, stack, 2, 6);
    const auto ambiguous = project_ops::make_remove_boundary_blend_command(project, ObjectId{2});
    REQUIRE_FALSE(ambiguous.has_value());
    CHECK(ambiguous.error().code == "E_BLEND_AMBIGUOUS");
    CHECK_FALSE(ambiguous.error().hint.empty());
    // Une zone à un seul fondu se retire toujours sans partenaire.
    CHECK(project_ops::make_remove_boundary_blend_command(project, ObjectId{4}).has_value());
    CHECK(project_ops::make_remove_boundary_blend_command(project, ObjectId{4}, ObjectId{6})
              .error()
              .code == "E_BLEND_NONE");

    const auto refused =
        project_ops::apply_operation(project, R"({"op":"unblend_boundary","object":2})");
    CHECK_FALSE(refused.ok);
    CHECK(refused.to_json().find("E_BLEND_AMBIGUOUS") != std::string::npos);
    const auto done = project_ops::apply_operation(
        project, R"({"op":"unblend_boundary","object":2,"partner":6})");
    INFO(done.to_json());
    REQUIRE(done.ok);
    CHECK(partners_of(project, 2) == std::vector<std::uint64_t>{4});
    CHECK(links_of(project, 6).empty());

    // L'opération blend_boundary pose un deuxième couple sans toucher au premier.
    const auto again =
        project_ops::apply_operation(project, R"({"op":"blend_boundary","a":6,"b":2,"width_a":3})");
    INFO(again.to_json());
    REQUIRE(again.ok);
    CHECK(partners_of(project, 2) == std::vector<std::uint64_t>{4, 6});
    CHECK(document::boundary_blend_link(*project.findEmbroidery(ObjectId{6}), ObjectId{2})
              ->width_self == Micrometers{3'000});
}

TEST_CASE("inspect lists every boundary blend of a zone", "[project_ops][blend][multi]") {
    auto project = make_three_zones();
    commands::UndoStack stack;
    blend(project, stack, 2, 4, 2'000);
    blend(project, stack, 2, 6, 3'000);
    const auto text = project_ops::inspect_project_json(project);
    REQUIRE(text.has_value());
    const json doc = json::parse(*text);
    bool found = false;
    for (const auto& object : doc["embroidery_objects"]) {
        if (object["id"] == 2) {
            REQUIRE(object.contains("boundary_blends"));
            REQUIRE(object["boundary_blends"].size() == 2);
            CHECK(object["boundary_blends"][0]["partner"] == 4);
            CHECK(object["boundary_blends"][0]["width_mm"] == 2.0);
            CHECK(object["boundary_blends"][1]["partner"] == 6);
            CHECK(object["boundary_blends"][1]["width_mm"] == 3.0);
            CHECK(object["boundary_blends"][1]["active"] == true);
            CHECK(object["boundary_blend"] == object["boundary_blends"][0]);
            found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("copy and design insertion remap every boundary blend link",
          "[project_ops][blend][multi]") {
    auto project = make_three_zones();
    commands::UndoStack stack;
    blend(project, stack, 2, 4);
    blend(project, stack, 2, 6);

    SECTION("paste of the three zones keeps both links between the copies") {
        const auto fragment =
            commands::collect_fragment(project, {ObjectId{1}, ObjectId{3}, ObjectId{5}});
        auto paste = commands::make_paste_command(project, fragment, Vec2um{});
        REQUIRE(paste.command);
        stack.execute(std::move(paste.command), project);
        const auto* a = copy_of(project, {0, 0, 255}, 2);
        const auto* b = copy_of(project, {255, 0, 0}, 4);
        const auto* c = copy_of(project, {0, 255, 0}, 6);
        REQUIRE((a != nullptr && b != nullptr && c != nullptr));
        REQUIRE(a->boundary_blends.size() == 2);
        CHECK(a->boundary_blends[0].partner == b->id);
        CHECK(a->boundary_blends[1].partner == c->id);
        CHECK(document::find_boundary_blends(project, a->id).size() == 2);
        CHECK(partners_of(project, 2) == std::vector<std::uint64_t>{4, 6}); // originaux intacts
    }
    SECTION("paste without C drops only the link to C") {
        const auto fragment = commands::collect_fragment(project, {ObjectId{1}, ObjectId{3}});
        auto paste = commands::make_paste_command(project, fragment, Vec2um{});
        REQUIRE(paste.command);
        stack.execute(std::move(paste.command), project);
        const auto* a = copy_of(project, {0, 0, 255}, 2);
        const auto* b = copy_of(project, {255, 0, 0}, 4);
        REQUIRE((a != nullptr && b != nullptr));
        REQUIRE(a->boundary_blends.size() == 1);
        CHECK(a->boundary_blends[0].partner == b->id);
    }
    SECTION("design insertion remaps both links") {
        auto target = make_three_zones();
        commands::UndoStack targetStack;
        targetStack.execute(std::make_unique<commands::InsertDesignCommand>(project, Vec2um{}),
                            target);
        const auto* a = copy_of(target, {0, 0, 255}, 2);
        const auto* b = copy_of(target, {255, 0, 0}, 4);
        const auto* c = copy_of(target, {0, 255, 0}, 6);
        REQUIRE((a != nullptr && b != nullptr && c != nullptr));
        REQUIRE(a->boundary_blends.size() == 2);
        CHECK(a->boundary_blends[0].partner == b->id);
        CHECK(a->boundary_blends[1].partner == c->id);
        CHECK(links_of(target, 2).empty()); // zones déjà présentes : intactes
    }
}

namespace {

std::vector<stitch::StitchCommand> commands_of(const stitch::StitchSequence& s, ObjectId id) {
    std::vector<stitch::StitchCommand> out;
    for (const auto& c : s.commands) {
        if (c.source == id) {
            out.push_back(c);
        }
    }
    return out;
}

} // namespace

TEST_CASE("a zone with two blends sews both and follows every neighbour shape",
          "[project_ops][blend][multi]") {
    auto project = make_three_zones();
    commands::UndoStack stack;
    blend(project, stack, 2, 4);
    const auto onlyAB = stitch_generation::generate_sequence(project); // raw-sequence-ok: test
    REQUIRE(onlyAB.has_value());
    blend(project, stack, 2, 6);
    const auto both = stitch_generation::generate_sequence(project); // raw-sequence-ok: test
    REQUIRE(both.has_value());
    // Le second fondu de A a un effet : A coud maintenant dans la demi-bande de C (au-dessus de
    // y = 20 mm), et sa couche change.
    const auto aboveA = [](const stitch::StitchSequence& s) {
        std::size_t n = 0;
        for (const auto& c : s.commands) {
            if (c.source == ObjectId{2} && c.type == stitch::CommandType::Stitch &&
                c.pos.y.value > 20'300 && c.pos.y.value <= 22'500 && c.pos.x.value < 17'000) {
                ++n;
            }
        }
        return n;
    };
    CHECK(aboveA(*onlyAB) == 0);
    CHECK(aboveA(*both) > 0);
    CHECK(commands_of(*both, ObjectId{2}) != commands_of(*onlyAB, ObjectId{2}));
    // Déterminisme.
    const auto again = stitch_generation::generate_sequence(project); // raw-sequence-ok: test
    REQUIRE(again.has_value());
    CHECK(again->commands == both->commands);

    // La forme de C change (moins haute, toujours collée à A) : la couche de A suit, bien que ni
    // A ni ses paramètres n'aient changé (aucune couche de fondu servie d'un cache périmé).
    project.vector_objects[2] = rect_shape(project.vector_objects[2].id, 0, 20, 12, 20);
    REQUIRE(document::find_boundary_blends(project, ObjectId{2}).size() == 2);
    const auto reshaped = stitch_generation::generate_sequence(project); // raw-sequence-ok: test
    REQUIRE(reshaped.has_value());
    CHECK(commands_of(*reshaped, ObjectId{2}) != commands_of(*both, ObjectId{2}));
    // Retour à la forme initiale : couche identique à celle d'avant.
    project.vector_objects[2] = rect_shape(project.vector_objects[2].id, 0, 20, 20, 20);
    const auto restored = stitch_generation::generate_sequence(project); // raw-sequence-ok: test
    REQUIRE(restored.has_value());
    CHECK(restored->commands == both->commands);
}
