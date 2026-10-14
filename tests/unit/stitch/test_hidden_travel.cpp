// SPDX-License-Identifier: Apache-2.0
// HP-SEQ-006.a / .c -- ponts cachés : une coupe entre deux tracés est remplacée par un saut ou un
// trajet cousu quand le trajet est prouvé recouvert par des points de couche supérieure cousus
// plus tard. Séquences construites à la main : la passe ne regarde que la séquence.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

#include "openstitch/stitch_generation/finish.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;
using CmdType = stitch::CommandType;
using Pass = stitch::StitchPass;
using Cmd = stitch::StitchCommand;

namespace {

Vec2um pt(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

ObjectId obj(std::uint64_t v) {
    ObjectId id;
    id.value = v;
    return id;
}

void stitch_to(stitch::StitchSequence& s, Vec2um p, ObjectId src, Pass pass = Pass::TopStitch) {
    s.commands.push_back({p, CmdType::Stitch, src, pass});
}

void jump_to(stitch::StitchSequence& s, Vec2um p, ObjectId src) {
    s.commands.push_back({p, CmdType::Jump, src, Pass::Travel});
}

// Remplissage en rangées alternées (aller-retour) sur [x0,x1] x [y0,y1], une piqûre à chaque bout
// de rangée. `fromRight` : la première rangée part de x1. La première piqûre est ajoutée
// (le tracé commence là), sans saut.
void fill_rows(stitch::StitchSequence& s, std::int32_t x0, std::int32_t x1, std::int32_t y0,
               std::int32_t y1, std::int32_t spacing, ObjectId src, bool fromRight = false) {
    bool rightward = !fromRight;
    for (std::int32_t y = y0; y <= y1; y += spacing) {
        const auto a = pt(rightward ? x0 : x1, y);
        const auto b = pt(rightward ? x1 : x0, y);
        stitch_to(s, a, src);
        stitch_to(s, b, src);
        rightward = !rightward;
    }
}

document::Project project_with(bool bridges) {
    document::Project project;
    project.finishing.hidden_travel.enabled = bridges;
    // Les rangées de test se rejoignent par des points de 0,4 mm : sans cela le filtre des points
    // courts (Lot F) en retirerait la moitié et fausserait la scène.
    project.finishing.filter_short_stitches = false;
    return project;
}

std::size_t count_type(const stitch::StitchSequence& s, CmdType t) {
    return static_cast<std::size_t>(std::count_if(s.commands.begin(), s.commands.end(),
                                                  [t](const Cmd& c) { return c.type == t; }));
}

std::size_t count_pass(const stitch::StitchSequence& s, Pass p) {
    return static_cast<std::size_t>(std::count_if(s.commands.begin(), s.commands.end(),
                                                  [p](const Cmd& c) { return c.pass == p; }));
}

// A : une ligne (objet 1) qui finit en (3000, 0). B : remplissage (objet 2) de
// [2000,10000] x [-2000,2000], rangées de 400 µm, qui commence à droite en (10000, -2000).
// L'extrémité de A est donc recouverte par B, cousu après.
stitch::StitchSequence overlap_scene() {
    stitch::StitchSequence s;
    stitch_to(s, pt(0, 0), obj(1));
    stitch_to(s, pt(3000, 0), obj(1));
    jump_to(s, pt(10000, -2000), obj(2));
    fill_rows(s, 2000, 10000, -2000, 2000, 400, obj(2), true);
    s.commands.push_back({pt(2000, 2000), CmdType::End, obj(0), Pass::TopStitch});
    return s;
}

bool inside(const Vec2um& p, std::int32_t x0, std::int32_t x1, std::int32_t y0, std::int32_t y1) {
    return p.x.value >= x0 && p.x.value <= x1 && p.y.value >= y0 && p.y.value <= y1;
}

} // namespace

TEST_CASE("hidden travel : desactive par defaut, la sequence est inchangee") {
    const auto scene = overlap_scene();
    const document::Project defaults;
    CHECK_FALSE(defaults.finishing.hidden_travel.enabled);
    document::Project
        untouched; // réglages par défaut, filtre des points courts coupé comme project_with
    untouched.finishing.filter_short_stitches = false;
    const auto off = finish_sequence(scene, untouched);
    const auto offExplicit = finish_sequence(scene, project_with(false));
    CHECK(off.commands == offExplicit.commands);
    // Une coupe entre les deux objets plus la coupe finale.
    CHECK(count_type(off, CmdType::Trim) == 2);
    CHECK(count_pass(off, Pass::Lock) > 0);
}

TEST_CASE("hidden travel : un pont cousu remplace la coupe quand B recouvre la fin de A") {
    const auto scene = overlap_scene();
    const auto project = project_with(true);
    const auto res = finish_sequence_report(scene, project);
    const auto& done = res.sequence;

    CHECK(res.report.candidate_gaps == 1);
    CHECK(res.report.bridges == 1);
    CHECK(res.report.trims_removed == 1);
    CHECK(res.report.refused_no_cover == 0);
    // Seule la coupe finale reste ; aucun saut entre A et B.
    CHECK(count_type(done, CmdType::Trim) == 1);
    CHECK(count_type(done, CmdType::Jump) == 0);
    const auto travelStitches = static_cast<std::size_t>(
        std::count_if(done.commands.begin(), done.commands.end(), [](const Cmd& c) {
            return c.pass == Pass::Travel && c.type == CmdType::Stitch;
        }));
    CHECK(res.report.bridge_stitches == travelStitches + 1); // + la piqûre d'arrivée de B
    // Tous les points du pont sont dans la zone recouverte, avec marge sur le bord.
    for (const Cmd& c : done.commands) {
        if (c.pass == Pass::Travel && c.type == CmdType::Stitch) {
            CHECK(inside(c.pos, 2050, 9950, -1950, 1950));
        }
    }
    // Moins de verrous qu'avec la coupe : ceux de la coupe disparaissent.
    const auto off = finish_sequence(scene, project_with(false));
    CHECK(count_pass(done, Pass::Lock) < count_pass(off, Pass::Lock));
    // Le fil reste continu : A, pont et B se suivent sans coupe.
    const auto lastA = std::find_if(done.commands.begin(), done.commands.end(),
                                    [](const Cmd& c) { return c.pos == pt(3000, 0); });
    REQUIRE(lastA != done.commands.end());
}

TEST_CASE("hidden travel : deterministe et sans effet sur un rejeu") {
    const auto scene = overlap_scene();
    const auto project = project_with(true);
    const auto a = finish_sequence_report(scene, project);
    const auto b = finish_sequence_report(scene, project);
    CHECK(a.sequence.commands == b.sequence.commands);
    CHECK(a.report.bridge_length_um == b.report.bridge_length_um);
}

TEST_CASE("hidden travel : saut recouvert -> un Jump reste, pas de point Travel") {
    stitch::StitchSequence s;
    stitch_to(s, pt(0, 0), obj(1));
    stitch_to(s, pt(3000, 0), obj(1));
    jump_to(s, pt(3000, 1500), obj(2));
    stitch_to(s, pt(3000, 1500), obj(2));
    // B recouvre largement la zone : rangées autour de l'écart.
    for (std::int32_t y = -1500; y <= 3000; y += 400) {
        stitch_to(s, pt(1000, y), obj(2));
        stitch_to(s, pt(5000, y), obj(2));
    }
    const auto res = finish_sequence_report(s, project_with(true));
    CHECK(res.report.hidden_jumps == 1);
    CHECK(res.report.bridges == 0);
    CHECK(count_pass(res.sequence, Pass::Travel) ==
          count_type(res.sequence, CmdType::Jump) + count_type(res.sequence, CmdType::Trim));
    CHECK(count_type(res.sequence, CmdType::Trim) == 1); // la coupe finale seulement
}

TEST_CASE("hidden travel : fin de A non recouverte -> la coupe est conservee") {
    stitch::StitchSequence s;
    stitch_to(s, pt(0, 0), obj(1));
    stitch_to(s, pt(3000, 0), obj(1));
    jump_to(s, pt(30000, 0), obj(2)); // B loin de A
    fill_rows(s, 28000, 40000, -2000, 2000, 400, obj(2));
    const auto res = finish_sequence_report(s, project_with(true));
    CHECK(res.report.bridges == 0);
    CHECK(res.report.refused_no_cover + res.report.refused_too_long == 1);
    CHECK(count_type(res.sequence, CmdType::Trim) == 2);
}

TEST_CASE("hidden travel : un remplissage cousu AVANT l'ecart ne cache rien") {
    stitch::StitchSequence s;
    fill_rows(s, 0, 10000, 0, 4000, 400, obj(1)); // grand remplissage d'abord
    stitch_to(s, pt(5000, 2000), obj(2));         // ligne qui finit sur lui
    stitch_to(s, pt(6000, 2000), obj(2));
    jump_to(s, pt(8000, 2000), obj(3));
    stitch_to(s, pt(8000, 2000), obj(3)); // ligne mince qui commence aussi sur lui
    stitch_to(s, pt(9000, 2000), obj(3));
    const auto res = finish_sequence_report(s, project_with(true));
    CHECK(res.report.bridges + res.report.hidden_jumps == 0);
    CHECK(res.report.candidate_gaps >= 1);
}

TEST_CASE("hidden travel : couverture trop lache -> refusee") {
    stitch::StitchSequence s;
    stitch_to(s, pt(0, 0), obj(1));
    stitch_to(s, pt(3000, 0), obj(1));
    jump_to(s, pt(10000, -2000), obj(2));
    fill_rows(s, 2000, 10000, -2000, 2000, 1500, obj(2), true); // rangées espacées de 1,5 mm
    const auto res = finish_sequence_report(s, project_with(true));
    CHECK(res.report.bridges + res.report.hidden_jumps == 0);
    CHECK(count_type(res.sequence, CmdType::Trim) == 2);
}

TEST_CASE("hidden travel : un changement de couleur garde sa coupe") {
    auto s = overlap_scene();
    // Insère un changement de couleur dans l'écart.
    const auto it = std::find_if(s.commands.begin(), s.commands.end(),
                                 [](const Cmd& c) { return c.type == CmdType::Jump; });
    s.commands.insert(it, Cmd{pt(3000, 0), CmdType::ColorChange, obj(2), Pass::Travel});
    const auto res = finish_sequence_report(s, project_with(true));
    CHECK(res.report.bridges == 0);
    CHECK(res.report.kept_color_change == 1);
    CHECK(count_type(res.sequence, CmdType::ColorChange) == 1);
    CHECK(count_type(res.sequence, CmdType::Trim) >= 2);
}

TEST_CASE("hidden travel : une coupe posee a la main n'est jamais supprimee") {
    auto s = overlap_scene();
    const auto it = std::find_if(s.commands.begin(), s.commands.end(),
                                 [](const Cmd& c) { return c.type == CmdType::Jump; });
    s.commands.insert(it, Cmd{pt(3000, 0), CmdType::Trim, obj(1), Pass::Travel});
    const auto res = finish_sequence_report(s, project_with(true));
    CHECK(res.report.bridges == 0);
    CHECK(res.report.kept_manual_trim == 1);
    CHECK(count_type(res.sequence, CmdType::Trim) >= 2);
}

TEST_CASE("hidden travel : le chemin contourne ce qui n'est pas recouvert (zone en L)") {
    // B couvre un L : R1 = [0,10000] x [0,2000], puis R2 = [8000,10000] x [0,10000]. A finit en
    // haut de R2 ; B commence en (0, 0). La droite A -> B sortirait du L.
    stitch::StitchSequence s;
    stitch_to(s, pt(9000, 9000), obj(1));
    stitch_to(s, pt(9000, 9500), obj(1));
    jump_to(s, pt(0, 200), obj(2));
    bool rightward = true;
    for (std::int32_t y = 200; y <= 1800; y += 400) {
        stitch_to(s, pt(rightward ? 0 : 10000, y), obj(2));
        stitch_to(s, pt(rightward ? 10000 : 0, y), obj(2));
        rightward = !rightward;
    }
    for (std::int32_t x = 8200; x <= 9800; x += 400) {
        stitch_to(s, pt(x, 0), obj(2));
        stitch_to(s, pt(x, 10000), obj(2));
    }
    auto project = project_with(true);
    project.finishing.hidden_travel.max_bridge_length = Micrometers{40'000};
    const auto res = finish_sequence_report(s, project);
    REQUIRE(res.report.bridges == 1);
    const auto inL = [](const Vec2um& p) {
        return inside(p, 0, 10000, 0, 2000) || inside(p, 8000, 10000, 0, 10000);
    };
    std::size_t viaCount = 0;
    for (const Cmd& c : res.sequence.commands) {
        if (c.pass == Pass::Travel && c.type == CmdType::Stitch) {
            CHECK(inL(c.pos));
            ++viaCount;
        }
    }
    CHECK(viaCount >= 3);
    CHECK(res.report.bridge_length_um > 15'000.0); // plus long que la droite (12,8 mm)
}

TEST_CASE("hidden travel : trajet trop long -> refuse, coupe conservee") {
    auto project = project_with(true);
    project.finishing.hidden_travel.max_bridge_length = Micrometers{5'000};
    const auto res = finish_sequence_report(overlap_scene(), project);
    CHECK(res.report.bridges == 0);
    CHECK(res.report.refused_too_long == 1);
    CHECK(count_type(res.sequence, CmdType::Trim) == 2);
}

namespace {

geometry::Path square_path(std::int32_t x0, std::int32_t y0, std::int32_t side) {
    geometry::Path p;
    p.closed = true;
    const auto node = [](std::int32_t x, std::int32_t y) {
        return geometry::PathNode{Vec2um{Micrometers{x}, Micrometers{y}},
                                  geometry::NodeType::Corner, std::nullopt, std::nullopt};
    };
    p.nodes = {node(x0, y0), node(x0 + side, y0), node(x0 + side, y0 + side), node(x0, y0 + side)};
    return p;
}

// Deux tatamis de la même couleur qui se chevauchent de 4 mm.
document::Project overlapping_tatami_squares() {
    document::Project project;
    for (int i = 0; i < 2; ++i) {
        document::VectorObject vec;
        vec.id = project.object_ids.next();
        vec.paths.push_back(geometry::PathSet{square_path(i * 6'000, 0, 10'000), {}});
        project.vector_objects.push_back(vec);
        document::EmbroideryObject emb;
        emb.id = project.object_ids.next();
        emb.source_vector = vec.id;
        emb.rgb = {200, 30, 30};
        emb.params = document::TatamiParams{};
        project.embroidery_objects.push_back(emb);
    }
    return project;
}

} // namespace

TEST_CASE("hidden travel : effective_sequence_report suit exactement effective_sequence") {
    auto project = overlapping_tatami_squares();
    project.finishing.hidden_travel.enabled = true;
    const auto plain = effective_sequence(project);
    const auto withReport = effective_sequence_report(project);
    REQUIRE(plain.has_value());
    REQUIRE(withReport.has_value());
    CHECK(plain->commands == withReport->sequence.commands);

    auto off = project;
    off.finishing.hidden_travel.enabled = false;
    const auto before = effective_sequence(off);
    REQUIRE(before.has_value());
    // Jamais plus de coupes qu'avec la passe désactivée ; chaque coupe supprimée est comptée.
    CHECK(count_type(*plain, CmdType::Trim) + withReport->report.trims_removed ==
          count_type(*before, CmdType::Trim));
    // Les deux exécutions donnent la même séquence.
    const auto again = effective_sequence_report(project);
    REQUIRE(again.has_value());
    CHECK(again->sequence.commands == withReport->sequence.commands);
}

namespace {

// Projet minimal portant les objets 1 et 2 de `overlap_scene`, avec le connecteur de l'objet 1.
document::Project connector_project(document::ConnectorMode mode, bool bridges) {
    auto project = project_with(bridges);
    for (std::uint64_t id = 1; id <= 2; ++id) {
        document::EmbroideryObject emb;
        emb.id = obj(id);
        emb.connector = id == 1 ? mode : document::ConnectorMode::Inherit;
        project.embroidery_objects.push_back(emb);
    }
    return project;
}

} // namespace

TEST_CASE("connecteur d'objet : jamais couper -> ni coupe ni points d'arret, le saut reste") {
    const auto scene = overlap_scene();
    const auto base =
        finish_sequence_report(scene, connector_project(document::ConnectorMode::Inherit, false));
    const auto never =
        finish_sequence_report(scene, connector_project(document::ConnectorMode::NeverTrim, false));
    CHECK(base.report.forced_no_trim == 0);
    CHECK(never.report.forced_no_trim == 1);
    CHECK(count_type(base.sequence, CmdType::Trim) == 2);
    CHECK(count_type(never.sequence, CmdType::Trim) == 1); // la coupe finale seulement
    CHECK(count_type(never.sequence, CmdType::Jump) == 1); // le fil reste tendu
    CHECK(count_pass(never.sequence, Pass::Lock) < count_pass(base.sequence, Pass::Lock));
}

TEST_CASE("connecteur d'objet : toujours couper l'emporte sur un pont cache") {
    const auto scene = overlap_scene();
    const auto bridged =
        finish_sequence_report(scene, connector_project(document::ConnectorMode::Inherit, true));
    REQUIRE(bridged.report.bridges == 1); // sans connecteur forcé, le pont est accepté
    const auto always =
        finish_sequence_report(scene, connector_project(document::ConnectorMode::AlwaysTrim, true));
    CHECK(always.report.forced_trims == 1);
    CHECK(always.report.bridges == 0);
    CHECK(always.report.candidate_gaps == 0); // le planificateur n'est même pas consulté
    CHECK(count_type(always.sequence, CmdType::Trim) == 2);
}

TEST_CASE("connecteur d'objet : sans effet avant un changement de fil ni sur une coupe posee") {
    auto scene = overlap_scene();
    const auto it = std::find_if(scene.commands.begin(), scene.commands.end(),
                                 [](const Cmd& c) { return c.type == CmdType::Jump; });
    scene.commands.insert(it, Cmd{pt(3000, 0), CmdType::ColorChange, obj(2), Pass::Travel});
    const auto never =
        finish_sequence_report(scene, connector_project(document::ConnectorMode::NeverTrim, false));
    CHECK(never.report.forced_no_trim == 0);
    CHECK(count_type(never.sequence, CmdType::Trim) >=
          2); // la coupe avant le changement de fil reste

    auto withTrim = overlap_scene();
    const auto jt = std::find_if(withTrim.commands.begin(), withTrim.commands.end(),
                                 [](const Cmd& c) { return c.type == CmdType::Jump; });
    withTrim.commands.insert(jt, Cmd{pt(3000, 0), CmdType::Trim, obj(1), Pass::Travel});
    const auto kept = finish_sequence_report(
        withTrim, connector_project(document::ConnectorMode::NeverTrim, false));
    CHECK(kept.report.forced_no_trim == 0);
    CHECK(count_type(kept.sequence, CmdType::Trim) >= 2);
}
