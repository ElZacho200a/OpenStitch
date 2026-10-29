// SPDX-License-Identifier: Apache-2.0
// HP-SEQ-006.a / .c -- ponts cachés : une coupe entre deux tracés est remplacée par un saut ou un
// trajet cousu quand le trajet est prouvé recouvert par des points de couche supérieure cousus
// plus tard. Séquences construites à la main : la passe ne regarde que la séquence.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
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

namespace {

// Objet 1 : trois passes de sous-couche dans un mauvais ordre (A, B loin, C près de A), puis un
// remplissage (couche supérieure) qui les recouvre.
stitch::StitchSequence underlay_scene() {
    stitch::StitchSequence s;
    const auto run = [&](std::int32_t x0, std::int32_t x1) {
        jump_to(s, pt(x0, 0), obj(1));
        stitch_to(s, pt(x0, 0), obj(1), Pass::Underlay);
        stitch_to(s, pt((x0 + x1) / 2, 0), obj(1), Pass::Underlay);
        stitch_to(s, pt(x1, 0), obj(1), Pass::Underlay);
    };
    run(0, 5'000);       // A
    run(30'000, 35'000); // B (loin)
    run(6'000, 9'000);   // C (près de A)
    jump_to(s, pt(0, 0), obj(1));
    fill_rows(s, 0, 40'000, 0, 2'000, 400, obj(1));
    return s;
}

std::vector<Cmd> top_layer(const stitch::StitchSequence& s) {
    std::vector<Cmd> out;
    for (const Cmd& c : s.commands) {
        if (c.type == CmdType::Stitch && c.pass == Pass::TopStitch) {
            out.push_back(c);
        }
    }
    return out;
}

std::vector<std::pair<std::int32_t, std::int32_t>>
underlay_points_sorted(const stitch::StitchSequence& s) {
    std::vector<std::pair<std::int32_t, std::int32_t>> out;
    for (const Cmd& c : s.commands) {
        if (c.type == CmdType::Stitch && c.pass == Pass::Underlay) {
            out.emplace_back(c.pos.x.value, c.pos.y.value);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

TEST_CASE(
    "optimiser les trajets : les passes de sous-couche sont reordonnees, le remplissage intact") {
    const auto scene = underlay_scene();
    auto off = project_with(false);
    auto on = project_with(true);
    // Sans points d'arrêt : un verrou d'entrée remplace la piqûre d'arrivée d'un tracé, ce qui
    // ferait varier le décompte sans rapport avec l'ordre des passes.
    off.finishing.lock_type = document::LockStitch::None;
    on.finishing.lock_type = document::LockStitch::None;
    // Seul le niveau 3 : pas de pont, pour isoler l'effet du réordonnancement.
    on.finishing.hidden_travel.between_objects = false;
    on.finishing.hidden_travel.within_object = false;

    const auto before = finish_sequence_report(scene, off);
    const auto after = finish_sequence_report(scene, on);
    CHECK(after.report.underlay_blocks == 1);
    CHECK(after.report.underlay_jump_saved_um > 0.0);
    // Deux coupes de moins : la dernière passe finit là où le remplissage commence, et A et C
    // sont voisines.
    CHECK(count_type(after.sequence, CmdType::Trim) + 2 ==
          count_type(before.sequence, CmdType::Trim));
    // Impératif : la couche supérieure est strictement identique (même points, même ordre) ...
    CHECK(top_layer(after.sequence) == top_layer(before.sequence));
    // ... et la sous-couche garde exactement les mêmes points, seulement enchaînés autrement.
    CHECK(underlay_points_sorted(after.sequence) == underlay_points_sorted(before.sequence));
    // Déterminisme.
    CHECK(finish_sequence_report(scene, on).sequence.commands == after.sequence.commands);
}

TEST_CASE("optimiser les trajets : niveau 3 coupe -> sequence strictement identique") {
    const auto scene = underlay_scene();
    auto base = project_with(false);
    auto offLevel = project_with(true);
    offLevel.finishing.hidden_travel.between_objects = false;
    offLevel.finishing.hidden_travel.within_object = false;
    offLevel.finishing.hidden_travel.reorder_underlay = false;
    CHECK(finish_sequence(scene, offLevel).commands == finish_sequence(scene, base).commands);
}

TEST_CASE(
    "optimiser les trajets : des passes separees par une coupe posee ne sont pas reordonnees") {
    auto scene = underlay_scene();
    // Coupe posée à la main entre A et B : le groupe est coupé en deux, rien à réordonner.
    const auto it = std::find_if(scene.commands.begin(), scene.commands.end(), [](const Cmd& c) {
        return c.pos == pt(30'000, 0) && c.type == CmdType::Jump;
    });
    REQUIRE(it != scene.commands.end());
    scene.commands.insert(it, Cmd{pt(5'000, 0), CmdType::Trim, obj(1), Pass::Travel});
    auto on = project_with(true);
    on.finishing.lock_type = document::LockStitch::None;
    on.finishing.hidden_travel.between_objects = false;
    on.finishing.hidden_travel.within_object = false;
    const auto res = finish_sequence_report(scene, on);
    // B et C restent un groupe de deux et peuvent être réordonnées, mais A n'est pas déplacée
    // au-delà de la coupe : son premier point reste le premier point cousu de la séquence.
    const auto firstStitch =
        std::find_if(res.sequence.commands.begin(), res.sequence.commands.end(), [](const Cmd& c) {
            return c.type == CmdType::Stitch && c.pass == Pass::Underlay;
        });
    REQUIRE(firstStitch != res.sequence.commands.end());
    CHECK(firstStitch->pos == pt(0, 0));
}

TEST_CASE("optimiser les trajets : portee entre objets / dans un objet") {
    // Écart entre deux objets (overlap_scene) : exclu quand « entre objets » est coupé.
    auto project = project_with(true);
    project.finishing.hidden_travel.between_objects = false;
    const auto res = finish_sequence_report(overlap_scene(), project);
    CHECK(res.report.bridges == 0);
    CHECK(res.report.skipped_by_scope == 1);
    CHECK(count_type(res.sequence, CmdType::Trim) == 2);
    project.finishing.hidden_travel.between_objects = true;
    CHECK(finish_sequence_report(overlap_scene(), project).report.bridges == 1);
}

TEST_CASE("optimiser les trajets : sur un projet genere, la couche superieure reste identique") {
    // Deux grands tatamis de la même couleur qui se chevauchent, sous-couche automatique, liaisons
    // non cachées : c'est le cas où des coupes peuvent disparaître.
    document::Project project;
    for (int i = 0; i < 2; ++i) {
        document::VectorObject vec;
        vec.id = project.object_ids.next();
        vec.paths.push_back(geometry::PathSet{square_path(i * 20'000, 0, 30'000), {}});
        project.vector_objects.push_back(vec);
        document::EmbroideryObject emb;
        emb.id = project.object_ids.next();
        emb.source_vector = vec.id;
        emb.rgb = {200, 30, 30};
        document::TatamiParams tp;
        tp.hidden_underpath = false;
        tp.underlay_mode = document::UnderlayMode::Auto;
        emb.params = tp;
        project.embroidery_objects.push_back(emb);
    }
    project.finishing.lock_type = document::LockStitch::None;
    const auto off = effective_sequence_report(project);
    REQUIRE(off.has_value());

    auto on = project;
    on.finishing.hidden_travel.enabled = true;
    on.finishing.hidden_travel.max_bridge_length = Micrometers{60'000};
    const auto opt = effective_sequence_report(on);
    REQUIRE(opt.has_value());

    // Impératif : le remplissage (couche supérieure) est strictement le même, point par point.
    CHECK(top_layer(opt->sequence) == top_layer(off->sequence));
    // Jamais plus de coupes qu'avec la passe désactivée.
    CHECK(count_type(opt->sequence, CmdType::Trim) <= count_type(off->sequence, CmdType::Trim));
}

namespace {

// Une région d'un seul trait horizontal : saut d'entrée, deux piqûres de couche supérieure.
void region(stitch::StitchSequence& s, std::uint64_t id, std::int32_t x0, std::int32_t x1,
            std::int32_t y = 0) {
    jump_to(s, pt(x0, y), obj(id));
    stitch_to(s, pt(x0, y), obj(id));
    stitch_to(s, pt(x1, y), obj(id));
}

document::Project advanced_project(bool reorderRegions) {
    auto project = project_with(true);
    project.finishing.lock_type = document::LockStitch::None;
    project.finishing.hidden_travel.between_objects = false;
    project.finishing.hidden_travel.within_object = false;
    project.finishing.hidden_travel.reorder_underlay = false;
    project.finishing.hidden_travel.reorder_regions = reorderRegions;
    for (std::uint64_t id = 1; id <= 4; ++id) {
        document::EmbroideryObject emb;
        emb.id = obj(id);
        project.embroidery_objects.push_back(emb);
    }
    return project;
}

// Ordre des objets tel qu'ils apparaissent dans la séquence (une entrée par suite).
std::vector<std::uint64_t> object_order(const stitch::StitchSequence& s) {
    std::vector<std::uint64_t> order;
    for (const Cmd& c : s.commands) {
        if (c.type == CmdType::Stitch && c.pass == Pass::TopStitch &&
            (order.empty() || order.back() != c.source.value)) {
            order.push_back(c.source.value);
        }
    }
    return order;
}

double total_jump_um(const stitch::StitchSequence& s) {
    double total = 0.0;
    Vec2um at{};
    bool have = false;
    for (const Cmd& c : s.commands) {
        if (c.type == CmdType::Jump && have) {
            total += length_um(c.pos - at);
        }
        if (c.type == CmdType::Stitch || c.type == CmdType::Jump) {
            at = c.pos;
            have = true;
        }
    }
    return total;
}

} // namespace

TEST_CASE("mode avance : les regions d'une meme couleur sont reordonnees, chacune intacte") {
    stitch::StitchSequence s;
    region(s, 1, 0, 5'000);       // A
    region(s, 2, 50'000, 55'000); // B (loin)
    region(s, 3, 6'000, 11'000);  // C (près de A)
    const auto off = finish_sequence_report(s, advanced_project(false));
    const auto on = finish_sequence_report(s, advanced_project(true));
    CHECK(object_order(off.sequence) == std::vector<std::uint64_t>{1, 2, 3});
    CHECK(object_order(on.sequence) == std::vector<std::uint64_t>{1, 3, 2});
    CHECK(on.report.region_groups == 1);
    CHECK(on.report.regions_moved >= 1);
    CHECK(on.report.region_jump_saved_um > 0.0);
    CHECK(total_jump_um(on.sequence) < total_jump_um(off.sequence));
    // Chaque région est intacte : mêmes piqûres, même sens.
    for (const std::uint64_t id : {1, 2, 3}) {
        std::vector<Vec2um> a;
        std::vector<Vec2um> b;
        for (const Cmd& c : off.sequence.commands) {
            if (c.type == CmdType::Stitch && c.source.value == id) {
                a.push_back(c.pos);
            }
        }
        for (const Cmd& c : on.sequence.commands) {
            if (c.type == CmdType::Stitch && c.source.value == id) {
                b.push_back(c.pos);
            }
        }
        CHECK(a == b);
    }
    // Déterminisme.
    CHECK(finish_sequence_report(s, advanced_project(true)).sequence.commands ==
          on.sequence.commands);
}

TEST_CASE("mode avance : deux regions dont les emprises se touchent gardent leur ordre") {
    stitch::StitchSequence s;
    region(s, 1, 0, 5'000);
    region(s, 2, 50'000, 55'000);
    region(s, 3, 5'200, 11'000); // touche A (moins de 0,7 mm) : doit rester après A
    region(s, 4, 56'000, 60'000);
    const auto on = finish_sequence_report(s, advanced_project(true));
    const auto order = object_order(on.sequence);
    const auto pos = [&](std::uint64_t id) {
        return std::find(order.begin(), order.end(), id) - order.begin();
    };
    CHECK(pos(1) < pos(3)); // A avant C : C recouvre A, cet ordre décide de ce qui est dessus
}

TEST_CASE("mode avance : desactive par defaut, region verrouillee et changement de fil respectes") {
    stitch::StitchSequence s;
    region(s, 1, 0, 5'000);
    region(s, 2, 50'000, 55'000);
    region(s, 3, 6'000, 11'000);
    // Désactivé : séquence strictement identique.
    CHECK(finish_sequence(s, advanced_project(false)).commands ==
          finish_sequence(s, advanced_project(false)).commands);
    auto off = advanced_project(true);
    off.finishing.hidden_travel.reorder_regions = false;
    CHECK(object_order(finish_sequence(s, off)) == std::vector<std::uint64_t>{1, 2, 3});

    // Région B verrouillée : elle reste à sa place, C ne peut pas la dépasser.
    auto locked = advanced_project(true);
    locked.embroidery_objects[1].locked = true;
    CHECK(object_order(finish_sequence(s, locked)) == std::vector<std::uint64_t>{1, 2, 3});

    // Un changement de fil sépare les groupes : C ne remonte pas avant le changement.
    stitch::StitchSequence withColor;
    region(withColor, 1, 0, 5'000);
    region(withColor, 2, 50'000, 55'000);
    withColor.commands.push_back({pt(55'000, 0), CmdType::ColorChange, obj(2), Pass::Travel});
    region(withColor, 3, 6'000, 11'000);
    const auto res = finish_sequence_report(withColor, advanced_project(true));
    CHECK(object_order(res.sequence) == std::vector<std::uint64_t>{1, 2, 3});
    CHECK(count_type(res.sequence, CmdType::ColorChange) == 1);
}

TEST_CASE("mode avance : privilegie une coupe evitee sur un petit detour") {
    stitch::StitchSequence s;
    region(s, 1, 0, 1'500);
    region(s, 2, 2'700, 3'200);
    region(s, 1, 4'400, 5'400); // meme objet, saute 2,9 mm depuis A
    region(s, 3, 6'600, 7'600);

    const auto off = finish_sequence_report(s, advanced_project(false));
    const auto on = finish_sequence_report(s, advanced_project(true));
    // L'ordre naturel est geometriquement plus court, mais multiplie les
    // transitions entre objets et donc les coupes (y compris sous 3 mm).
    CHECK(count_type(off.sequence, CmdType::Trim) == 4);
    CHECK(count_type(on.sequence, CmdType::Trim) == 3);
    CHECK(on.report.regions_moved > 0);
    CHECK(on.report.region_jump_saved_um < 0.0); // detour accepte pour eviter une coupe
    CHECK(object_order(on.sequence).size() == 3);
    CHECK(finish_sequence_report(s, advanced_project(true)).sequence.commands ==
          on.sequence.commands);
}

TEST_CASE("sous-couches : recherche locale bornee pour sept passes et plus") {
    stitch::StitchSequence s;
    stitch_to(s, pt(0, 0), obj(8)); // ancre precedente
    const std::array<std::array<std::int32_t, 4>, 7> spans{{
        {{28'000, 20'000, 30'000, 20'000}},
        {{40'000, 29'000, 44'000, 29'000}},
        {{33'000, 34'000, 37'000, 34'000}},
        {{35'000, 9'000, 37'000, 9'000}},
        {{12'000, 10'000, 15'000, 10'000}},
        {{3'000, 26'000, 5'000, 26'000}},
        {{29'000, 20'000, 31'000, 20'000}},
    }};
    for (const auto& b : spans) {
        jump_to(s, pt(b[0], b[1]), obj(1));
        stitch_to(s, pt(b[0], b[1]), obj(1), Pass::Underlay);
        stitch_to(s, pt(b[2], b[3]), obj(1), Pass::Underlay);
    }
    jump_to(s, pt(30'000, 30'000), obj(9));
    stitch_to(s, pt(30'000, 30'000), obj(9));

    auto off = project_with(false);
    auto on = project_with(true);
    off.finishing.lock_type = document::LockStitch::None;
    on.finishing.lock_type = document::LockStitch::None;
    on.finishing.hidden_travel.between_objects = false;
    on.finishing.hidden_travel.within_object = false;
    const auto baseline = finish_sequence_report(s, off);
    const auto optimized = finish_sequence_report(s, on);

    CHECK(optimized.report.underlay_blocks == 1);
    CHECK(optimized.report.underlay_jump_saved_um > 0.0);
    CHECK(count_type(optimized.sequence, CmdType::Trim) <
          count_type(baseline.sequence, CmdType::Trim));
    CHECK(top_layer(optimized.sequence) == top_layer(baseline.sequence));
    CHECK(underlay_points_sorted(optimized.sequence) == underlay_points_sorted(baseline.sequence));
    CHECK(finish_sequence_report(s, on).sequence.commands == optimized.sequence.commands);
}
