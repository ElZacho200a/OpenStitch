// SPDX-License-Identifier: Apache-2.0
// Finitions de la séquence (Lots E et F, audit marine plein cadre 2026-09-22) :
// coupes automatiques, points d'arrêt, points courts.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

#include "openstitch/stitch_generation/finish.hpp"
#include "openstitch/stitch_generation/generate.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;
using CmdType = stitch::CommandType;
using Pass = stitch::StitchPass;

namespace {

geometry::Path square_at(std::int32_t x0, std::int32_t y0, std::int32_t side) {
    geometry::Path p;
    p.closed = true;
    const auto node = [](std::int32_t x, std::int32_t y) {
        return geometry::PathNode{Vec2um{Micrometers{x}, Micrometers{y}},
                                  geometry::NodeType::Corner, std::nullopt, std::nullopt};
    };
    p.nodes = {node(x0, y0), node(x0 + side, y0), node(x0 + side, y0 + side), node(x0, y0 + side)};
    return p;
}

// Deux contours (point droit) de 10 mm. Un contour finit là où il commence
// (son premier sommet) : le premier carré finit en (0, 0), le second commence
// en (-gap, 0) et s'étend vers les x et y négatifs -- le déplacement entre
// les deux vaut exactement `gap` µm. Couleurs identiques ou non.
document::Project two_squares(std::int32_t gap, bool sameColor = true) {
    document::Project project;
    for (int i = 0; i < 2; ++i) {
        document::VectorObject vec;
        vec.id = project.object_ids.next();
        vec.paths.push_back(
            geometry::PathSet{i == 0 ? square_at(0, 0, 10'000) : square_at(-gap, 0, -10'000), {}});
        project.vector_objects.push_back(vec);
        document::EmbroideryObject emb;
        emb.id = project.object_ids.next();
        emb.source_vector = vec.id;
        emb.rgb = sameColor || i == 0 ? std::array<std::uint8_t, 3>{200, 30, 30}
                                      : std::array<std::uint8_t, 3>{30, 30, 200};
        project.embroidery_objects.push_back(emb);
    }
    return project;
}

std::size_t count_type(const stitch::StitchSequence& s, CmdType t) {
    return static_cast<std::size_t>(std::count_if(s.commands.begin(), s.commands.end(),
                                                  [t](const auto& c) { return c.type == t; }));
}

std::size_t count_pass(const stitch::StitchSequence& s, Pass p) {
    return static_cast<std::size_t>(std::count_if(s.commands.begin(), s.commands.end(),
                                                  [p](const auto& c) { return c.pass == p; }));
}

// Déplacements (suites de Jump entre deux points cousus) plus longs que
// `threshold` et non précédés d'une coupe.
std::size_t long_moves_without_trim(const stitch::StitchSequence& s, double threshold) {
    std::size_t n = 0;
    bool haveStitch = false;
    Vec2um last{};
    bool trimmed = false;
    bool inMove = false;
    for (const auto& c : s.commands) {
        if (c.type == CmdType::Trim || c.type == CmdType::ColorChange) {
            trimmed = trimmed || c.type == CmdType::Trim;
        } else if (c.type == CmdType::Jump) {
            inMove = true;
        } else if (c.type == CmdType::Stitch) {
            if (haveStitch && inMove && !trimmed && length_um(c.pos - last) > threshold) {
                ++n;
            }
            haveStitch = true;
            last = c.pos;
            inMove = false;
            trimmed = false;
        }
    }
    return n;
}

} // namespace

TEST_CASE("finish : un deplacement long entre deux objets -> arret, coupe, arret") {
    const auto project = two_squares(20'000);
    const auto raw = generate_sequence(project);
    REQUIRE(raw.has_value());
    CHECK(count_type(*raw, CmdType::Trim) == 0);
    CHECK(long_moves_without_trim(*raw, 3'000.0) == 1); // le défaut de l'audit

    const auto done = finish_sequence(*raw, project);
    CHECK(count_type(done, CmdType::Trim) == 1);
    CHECK(long_moves_without_trim(done, 3'000.0) == 0);
    // Un verrou d'entrée et de sortie par objet : 4 verrous au total.
    std::size_t lockRuns = 0;
    for (std::size_t i = 0; i < done.commands.size(); ++i) {
        if (done.commands[i].pass == Pass::Lock &&
            (i == 0 || done.commands[i - 1].pass != Pass::Lock)) {
            ++lockRuns;
        }
    }
    CHECK(lockRuns == 4);
    // La coupe suit immédiatement le verrou de sortie du premier objet.
    const auto trimIt = std::find_if(done.commands.begin(), done.commands.end(),
                                     [](const auto& c) { return c.type == CmdType::Trim; });
    REQUIRE(trimIt != done.commands.begin());
    CHECK(std::prev(trimIt)->pass == Pass::Lock);
    CHECK(std::prev(trimIt)->source == project.embroidery_objects[0].id);
    CHECK(done.commands.back().type == CmdType::End);
}

TEST_CASE("finish : deplacement court entre deux objets -> toujours coupe, avec verrous") {
    // Regression 2026-10-08 : un ecart sous `trim_threshold` entre deux formes
    // laissait le fil tendu (aucune coupe).
    const auto project = two_squares(1'000); // 1 mm < seuil de 3 mm
    const auto raw = generate_sequence(project);
    REQUIRE(raw.has_value());
    const auto done = finish_sequence(*raw, project);
    CHECK(count_type(done, CmdType::Trim) == 1);
    CHECK(count_type(done, CmdType::Jump) == count_type(*raw, CmdType::Jump));
    CHECK(count_pass(done, Pass::Lock) > 0);
}

TEST_CASE("finish : coupe avant un changement de fil") {
    const auto project = two_squares(1'000, false);
    const auto raw = generate_sequence(project);
    REQUIRE(raw.has_value());
    const auto done = finish_sequence(*raw, project);
    REQUIRE(count_type(done, CmdType::Trim) == 1);
    const auto trimIt = std::find_if(done.commands.begin(), done.commands.end(),
                                     [](const auto& c) { return c.type == CmdType::Trim; });
    REQUIRE(std::next(trimIt) != done.commands.end());
    CHECK(std::next(trimIt)->type == CmdType::ColorChange);

    auto noTrim = project;
    noTrim.finishing.trim_before_color_change = false;
    CHECK(count_type(finish_sequence(*raw, noTrim), CmdType::Trim) == 0);
}

TEST_CASE("finish : seuil de coupe et type de verrou configurables") {
    auto project = two_squares(5'000);
    const auto raw = generate_sequence(project);
    REQUIRE(raw.has_value());
    CHECK(count_type(finish_sequence(*raw, project), CmdType::Trim) == 1);
    // Entre deux objets, le seuil ne desactive plus la coupe.
    project.finishing.trim_threshold = Micrometers{6'000};
    CHECK(count_type(finish_sequence(*raw, project), CmdType::Trim) == 1);
    project.finishing.lock_type = document::LockStitch::None;
    CHECK(count_pass(finish_sequence(*raw, project), Pass::Lock) == 0);
}

TEST_CASE("finish : desactive (projet anterieur) -> sequence identique") {
    auto project = two_squares(20'000);
    project.finishing = document::SequenceFinishing::legacy();
    const auto raw = generate_sequence(project);
    REQUIRE(raw.has_value());
    CHECK(finish_sequence(*raw, project).commands == raw->commands);
}

TEST_CASE("finish : verrous poses sur le trace cousu, sans sortir de l'objet") {
    const auto project = two_squares(20'000);
    const auto raw = generate_sequence(project);
    REQUIRE(raw.has_value());
    const auto done = finish_sequence(*raw, project);
    for (const auto& c : done.commands) {
        if (c.pass != Pass::Lock) {
            continue;
        }
        // Contours carrés : chaque verrou reste dans le carré de son objet
        // ([0,10]² mm pour le premier, [-30,-20]x[-10,0] mm pour le second).
        const bool first = c.source == project.embroidery_objects[0].id;
        const std::int32_t x0 = first ? 0 : -30'000;
        const std::int32_t y0 = first ? 0 : -10'000;
        CHECK(c.pos.x.value >= x0);
        CHECK(c.pos.x.value <= x0 + 10'000);
        CHECK(c.pos.y.value >= y0);
        CHECK(c.pos.y.value <= y0 + 10'000);
    }
}

TEST_CASE("finish : un satin qui porte deja ses verrous n'est pas double") {
    document::Project project;
    document::SatinParams sp;
    sp.rail_a.closed = false;
    sp.rail_b.closed = false;
    const auto node = [](std::int32_t x, std::int32_t y) {
        return geometry::PathNode{Vec2um{Micrometers{x}, Micrometers{y}},
                                  geometry::NodeType::Corner, std::nullopt, std::nullopt};
    };
    sp.rail_a.nodes = {node(0, 0), node(20'000, 0)};
    sp.rail_b.nodes = {node(0, 3'000), node(20'000, 3'000)};
    sp.center_underlay = false;
    sp.lock_start = document::SatinLock::Triangle;
    sp.lock_end = document::SatinLock::Triangle;
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.params = sp;
    project.embroidery_objects.push_back(emb);
    const auto raw = generate_sequence(project);
    REQUIRE(raw.has_value());
    const auto done = finish_sequence(*raw, project);
    CHECK(count_pass(done, Pass::Lock) == count_pass(*raw, Pass::Lock));
}

TEST_CASE("finish : effective_sequence applique les finitions, deterministe") {
    const auto project = two_squares(20'000);
    const auto a = effective_sequence(project);
    const auto b = effective_sequence(project);
    REQUIRE((a.has_value() && b.has_value()));
    CHECK(a->commands == b->commands);
    CHECK(count_type(*a, CmdType::Trim) == 1);
    const auto ctx = refresh_context(project, std::nullopt);
    REQUIRE(ctx.has_value());
    CHECK(ctx->effective.commands == a->commands);
}

// --- Lot F : points trop courts ----------------------------------------------
// Avant : ~4 950 points de moins de 0,5 mm sur la marine (bouts de rangée
// tatami, point de longueur nulle à chaque arrivée de saut), aucun filtrage.

namespace {

Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

// Projet : un tatami suivant une région en L (0..10 mm, bras de 2 mm), plus
// la séquence brute fournie par le test (fabriquée à la main pour viser un cas
// précis ; la source de chaque commande est ce tatami).
struct LCase {
    document::Project project;
    ObjectId object;
};

LCase l_shape_project() {
    LCase c;
    geometry::Path l;
    l.closed = true;
    const auto node = [](std::int32_t x, std::int32_t y) {
        return geometry::PathNode{Vec2um{Micrometers{x}, Micrometers{y}},
                                  geometry::NodeType::Corner, std::nullopt, std::nullopt};
    };
    l.nodes = {node(0, 0),         node(10'000, 0),     node(10'000, 2'000),
               node(2'000, 2'000), node(2'000, 10'000), node(0, 10'000)};
    document::VectorObject vec;
    vec.id = c.project.object_ids.next();
    vec.paths.push_back(geometry::PathSet{l, {}});
    c.project.vector_objects.push_back(vec);
    document::EmbroideryObject emb;
    emb.id = c.project.object_ids.next();
    emb.source_vector = vec.id;
    emb.params = document::TatamiParams{};
    c.project.embroidery_objects.push_back(emb);
    c.object = emb.id;
    c.project.finishing.lock_type = document::LockStitch::None; // isole le filtre
    return c;
}

stitch::StitchSequence seq_of(ObjectId src, const std::vector<Vec2um>& pts,
                              std::vector<Pass> passes = {}) {
    stitch::StitchSequence s;
    s.commands.push_back({pts.front(), CmdType::Jump, src, Pass::Travel});
    for (std::size_t i = 0; i < pts.size(); ++i) {
        s.commands.push_back(
            {pts[i], CmdType::Stitch, src, i < passes.size() ? passes[i] : Pass::TopStitch});
    }
    s.commands.push_back({pts.back(), CmdType::End, ObjectId{}});
    return s;
}

std::size_t stitches_shorter_than(const stitch::StitchSequence& s, double len) {
    std::size_t n = 0;
    for (std::size_t i = 1; i < s.commands.size(); ++i) {
        if (s.commands[i].type == CmdType::Stitch && s.commands[i - 1].type == CmdType::Stitch &&
            s.commands[i].pass != Pass::Lock &&
            length_um(s.commands[i].pos - s.commands[i - 1].pos) < len) {
            ++n;
        }
    }
    return n;
}

} // namespace

TEST_CASE("finish : un point de moins de 0,5 mm est fusionne avec le suivant") {
    auto c = l_shape_project();
    // Bras horizontal : 1 -> 1,3 (0,3 mm) -> 4 -> 7 mm.
    const auto raw =
        seq_of(c.object, {um(1'000, 1'000), um(1'300, 1'000), um(4'000, 1'000), um(7'000, 1'000)});
    CHECK(stitches_shorter_than(raw, 500.0) == 1);
    const auto done = finish_sequence(raw, c.project);
    CHECK(stitches_shorter_than(done, 500.0) == 0);
    // Seul le point court disparaît ; extrémités et autres points intacts.
    CHECK(count_type(done, CmdType::Stitch) == 3);
    CHECK(done.commands[1].pos == um(1'000, 1'000));
    CHECK(done.commands[done.commands.size() - 2].pos == um(7'000, 1'000));

    // Désactivable.
    c.project.finishing.filter_short_stitches = false;
    CHECK(stitches_shorter_than(finish_sequence(raw, c.project), 500.0) == 1);
}

TEST_CASE("finish : jamais de fusion qui ferait sortir le fil de la region") {
    const auto c = l_shape_project();
    // A (bras horizontal) -> B (coin intérieur, 0,43 mm plus loin) -> C (bras
    // vertical). Sans B, la corde A->C couperait l'angle HORS du L.
    const auto raw = seq_of(c.object, {um(5'000, 1'000), um(2'300, 1'800), um(1'950, 2'050),
                                       um(1'950, 6'000), um(1'000, 9'000)});
    const auto done = finish_sequence(raw, c.project);
    CHECK(count_type(done, CmdType::Stitch) == count_type(raw, CmdType::Stitch));
}

TEST_CASE("finish : verrous, retouches manuelles et extremites jamais filtres") {
    const auto c = l_shape_project();
    const auto raw = seq_of(
        c.object,
        {um(1'000, 1'000), um(1'200, 1'000), um(1'400, 1'000), um(5'000, 1'000), um(5'100, 1'000)},
        {Pass::TopStitch, Pass::Lock, Pass::Manual, Pass::TopStitch, Pass::TopStitch});
    const auto done = finish_sequence(raw, c.project);
    // Lock (0,2 mm), Manual (0,2 mm) et le dernier point (0,1 mm) restent.
    CHECK(count_type(done, CmdType::Stitch) == 5);
}

TEST_CASE("finish : point de longueur nulle a l'arrivee d'un saut absorbe par le verrou") {
    auto c = l_shape_project();
    c.project.finishing.lock_type = document::LockStitch::BackAndForth;
    const auto raw = seq_of(c.object, {um(1'000, 1'000), um(4'000, 1'000), um(7'000, 1'000)});
    // seq_of : Jump(p0) puis Stitch(p0) -- piqûre de longueur nulle.
    CHECK(stitches_shorter_than(raw, 1.0) == 0); // (le Jump n'est pas un Stitch)
    const auto done = finish_sequence(raw, c.project);
    REQUIRE(done.commands[0].type == CmdType::Jump);
    // Le premier point cousu après le saut est le verrou, pas une piqûre nulle.
    CHECK(done.commands[1].pass == Pass::Lock);
    CHECK(done.commands[1].pos != done.commands[0].pos);
    // Le verrou revient bien piquer en p0 avant le premier vrai point.
    bool backAtStart = false;
    for (std::size_t i = 1; i < done.commands.size() && done.commands[i].pass == Pass::Lock; ++i) {
        backAtStart = backAtStart || done.commands[i].pos == um(1'000, 1'000);
    }
    CHECK(backAtStart);
}

TEST_CASE("finish : la coupe se decide sur le point d'atterrissage reel (verrou d'entree)") {
    // Écart de 2,8 mm entre les deux objets, mais la piqûre nulle d'arrivée
    // est remplacée par le verrou d'entrée, qui part 0,8 mm plus loin : le
    // fil parcourt 3,6 mm, il faut couper (défaut mesuré sur la marine : 9
    // déplacements de 3,2 à 3,6 mm restés sans coupe).
    const auto project = two_squares(2'800);
    const auto raw = generate_sequence(project);
    REQUIRE(raw.has_value());
    const auto done = finish_sequence(*raw, project);
    CHECK(long_moves_without_trim(done, 3'000.0) == 0);
    CHECK(count_type(done, CmdType::Trim) == 1);
}
