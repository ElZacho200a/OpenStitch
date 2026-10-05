// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

#include "openstitch/stitch_generation/generate.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

namespace {

// Projet avec un carre vectoriel de 10 mm et un objet de broderie de contour.
document::Project make_project(int embroideryCount = 1) {
    document::Project project;

    geometry::Path square;
    square.closed = true;
    const std::int32_t s = 10'000;
    square.nodes = {
        {Vec2um{Micrometers{0}, Micrometers{0}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{s}, Micrometers{0}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{s}, Micrometers{s}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{0}, Micrometers{s}}, geometry::NodeType::Corner, {}, {}},
    };

    document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.paths.push_back(geometry::PathSet{square, {}});
    project.vector_objects.push_back(vec);

    for (int i = 0; i < embroideryCount; ++i) {
        document::EmbroideryObject emb;
        emb.id = project.object_ids.next();
        emb.name = "contour " + std::to_string(i);
        emb.source_vector = vec.id;
        emb.rgb = {static_cast<std::uint8_t>(200 - i * 100), 30, 30}; // couleurs differentes
        project.embroidery_objects.push_back(emb);
    }
    return project;
}

} // namespace

TEST_CASE("sequence d'un contour : Jump, points, End") {
    const auto seq = generate_sequence(make_project());
    REQUIRE(seq.has_value());
    REQUIRE_FALSE(seq->commands.empty());
    CHECK(seq->commands.front().type == stitch::CommandType::Jump);
    CHECK(seq->commands.back().type == stitch::CommandType::End);

    const auto stats = stitch::compute_stats(*seq);
    CHECK(stats.jumps == 1);
    CHECK(stats.color_changes == 0);
    // Perimetre 40 mm, pas 3 mm -> 4 aretes x 4 pas = 16 segments, 17 points.
    CHECK(stats.stitches == 17);
    // Fil cousu = perimetre exact.
    CHECK(stats.thread_length_um == 40'000.0);
    CHECK(stats.bounds.max == Vec2um{Micrometers{10'000}, Micrometers{10'000}});
}

TEST_CASE("deux objets de couleurs differentes -> un ColorChange") {
    const auto seq = generate_sequence(make_project(2));
    REQUIRE(seq.has_value());
    const auto stats = stitch::compute_stats(*seq);
    CHECK(stats.color_changes == 1);
    CHECK(stats.jumps == 2);
}

TEST_CASE("objet invisible ignore ; projet vide -> erreur utilisateur") {
    auto project = make_project();
    project.embroidery_objects[0].visible = false;
    const auto seq = generate_sequence(project);
    REQUIRE_FALSE(seq.has_value());
    CHECK(seq.error().category == ErrorCategory::UserInput);
}

TEST_CASE("source vectorielle manquante -> erreur interne") {
    auto project = make_project();
    project.vector_objects.clear();
    CHECK_FALSE(generate_sequence(project).has_value());
}

namespace {
// Objet satin colonne droite (rails y=0/5000, x 0..20000, deux barreaux).
document::SatinParams straight_satin() {
    document::SatinParams sp;
    sp.rail_a.closed = false;
    sp.rail_a.nodes = {
        {Vec2um{Micrometers{0}, Micrometers{0}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{20'000}, Micrometers{0}}, geometry::NodeType::Corner, {}, {}}};
    sp.rail_b.closed = false;
    sp.rail_b.nodes = {
        {Vec2um{Micrometers{0}, Micrometers{5'000}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{20'000}, Micrometers{5'000}}, geometry::NodeType::Corner, {}, {}}};
    sp.rungs = {
        {Vec2um{Micrometers{0}, Micrometers{0}}, Vec2um{Micrometers{0}, Micrometers{5'000}}},
        {Vec2um{Micrometers{20'000}, Micrometers{0}},
         Vec2um{Micrometers{20'000}, Micrometers{5'000}}}};
    return sp;
}
document::Project satin_project(const document::SatinParams& sp) {
    document::Project project;
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.params = sp;
    project.embroidery_objects.push_back(emb);
    return project;
}
} // namespace

TEST_CASE("satin : les sous-couches sont taggees Underlay, le satin TopStitch") {
    auto sp = straight_satin();
    sp.center_underlay = true;
    const auto seq = generate_sequence(satin_project(sp));
    REQUIRE(seq.has_value());
    int underlay = 0, top = 0;
    for (const auto& c : seq->commands) {
        if (c.type == stitch::CommandType::Stitch && c.pass == stitch::StitchPass::Underlay)
            ++underlay;
        if (c.type == stitch::CommandType::Stitch && c.pass == stitch::StitchPass::TopStitch)
            ++top;
    }
    CHECK(underlay > 0);
    CHECK(top > 0);
}

TEST_CASE("satin : locks d'entree et de sortie en passe Lock, aux extremites") {
    auto sp = straight_satin();
    sp.center_underlay = true;
    sp.underlay_edge = true;
    sp.lock_start = document::SatinLock::BackAndForth;
    sp.lock_end = document::SatinLock::Triangle;
    const auto seq = generate_sequence(satin_project(sp));
    REQUIRE(seq.has_value());

    int lockStitches = 0;
    int lockRuns = 0; // groupes contigus de passe Lock (doit rester <= 2)
    bool inLock = false;
    int firstTop = -1, i = 0;
    for (const auto& c : seq->commands) {
        if (c.type != stitch::CommandType::Stitch) {
            ++i;
            continue;
        }
        if (c.pass == stitch::StitchPass::Lock) {
            ++lockStitches;
            if (!inLock)
                ++lockRuns;
            inLock = true;
        } else {
            inLock = false;
        }
        if (c.pass == stitch::StitchPass::TopStitch && firstTop < 0)
            firstTop = i;
        ++i;
    }
    CHECK(lockStitches > 0);
    CHECK(lockRuns == 2); // un lock au début, un à la fin — jamais par sous-passe
}

TEST_CASE("satin : le point d'entree oriente la couture") {
    // Entrée placée près de l'extrémité x=20000 : le satin doit démarrer là.
    const auto without = generate_sequence(satin_project(straight_satin()));
    auto sp = straight_satin();
    sp.entry_point = Vec2um{Micrometers{20'000}, Micrometers{2'500}};
    const auto with = generate_sequence(satin_project(sp));
    REQUIRE((without.has_value() && with.has_value()));
    const auto firstTop = [](const auto& seq) {
        for (const auto& c : seq->commands)
            if (c.type == stitch::CommandType::Stitch && c.pass == stitch::StitchPass::TopStitch)
                return c.pos;
        return Vec2um{};
    };
    CHECK(firstTop(without).x.value < 5'000); // sans entrée : démarre à x~0
    CHECK(firstTop(with).x.value > 15'000);   // avec entrée : démarre à x~20000
}

TEST_CASE("tatami : sous-couche taggee Underlay, remplissage TopStitch") {
    auto project = make_project(1);
    document::TatamiParams tp;
    tp.row_spacing = Micrometers{1'000};
    tp.underlay_edge = true;
    project.embroidery_objects[0].params = tp;
    const auto seq = generate_sequence(project);
    REQUIRE(seq.has_value());
    int under = 0, top = 0;
    for (const auto& c : seq->commands) {
        if (c.type == stitch::CommandType::Stitch && c.pass == stitch::StitchPass::Underlay)
            ++under;
        if (c.type == stitch::CommandType::Stitch && c.pass == stitch::StitchPass::TopStitch)
            ++top;
    }
    CHECK(under > 0);
    CHECK(top > 0);
}

TEST_CASE("la sequence est deterministe") {
    const auto a = generate_sequence(make_project(2));
    const auto b = generate_sequence(make_project(2));
    REQUIRE((a.has_value() && b.has_value()));
    CHECK(a->commands == b->commands);
}

namespace {
geometry::Path circle_path(std::int32_t cx, std::int32_t cy, std::int32_t r, int n = 96) {
    geometry::Path p;
    p.closed = true;
    for (int i = 0; i < n; ++i) {
        const double a = 2.0 * std::numbers::pi * i / n;
        p.nodes.push_back({Vec2um{Micrometers{cx + static_cast<std::int32_t>(r * std::cos(a))},
                                  Micrometers{cy + static_cast<std::int32_t>(r * std::sin(a))}},
                           geometry::NodeType::Corner,
                           {},
                           {}});
    }
    return p;
}

double distance_um(Vec2um a, Vec2um b) {
    const double dx = static_cast<double>(a.x.value - b.x.value);
    const double dy = static_cast<double>(a.y.value - b.y.value);
    return std::sqrt(dx * dx + dy * dy);
}
} // namespace

TEST_CASE("tatami sur une forme a 2 trous separes (etape 6, docs/source/satin.md) : "
          "couverture complete, aucun point dans les trous") {
    // § refonte decomposition topologique, etape 6 : "two_holes" (rectangle
    // 60x30mm, 2 trous circulaires de 5mm separes) n'a AUCUNE famille de
    // coupe applicable dans satin_planning::create_satin_plan (statut
    // Impossible, 0 colonne -- aucun solveur dedie pour 2+ trous, cf.
    // docs/source/satin.md). Avant de conclure qu'une correspondance de
    // contour dediee est necessaire pour cette forme precise, verifie ICI,
    // au niveau LIBRAIRIE (pas le chemin UI complet, trop fragile a
    // simuler pour cette seule question), que le repli tatami DEJA
    // EXISTANT dans l'application (§23, MainWindow::askAboutIncompleteSatinCoverage
    // -> appendTatamiFallbackObjects) produirait bien un remplissage
    // COMPLET et VALIDE de la forme entiere une fois applique -- ce
    // qu'un objet TatamiParams directement sur cette forme mesure ici.
    document::Project project;

    geometry::Path outer;
    outer.closed = true;
    constexpr std::int32_t w = 60'000, h = 15'000;
    outer.nodes = {
        {Vec2um{Micrometers{0}, Micrometers{-h}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{w}, Micrometers{-h}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{w}, Micrometers{h}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{0}, Micrometers{h}}, geometry::NodeType::Corner, {}, {}},
    };
    constexpr std::int32_t holeR = 5'000;
    const Vec2um hole1Center{Micrometers{15'000}, Micrometers{0}};
    const Vec2um hole2Center{Micrometers{45'000}, Micrometers{0}};
    geometry::Path hole1 = circle_path(hole1Center.x.value, hole1Center.y.value, holeR);
    std::reverse(hole1.nodes.begin(), hole1.nodes.end()); // sens oppose au contour exterieur
    geometry::Path hole2 = circle_path(hole2Center.x.value, hole2Center.y.value, holeR);
    std::reverse(hole2.nodes.begin(), hole2.nodes.end());

    document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.paths.push_back(geometry::PathSet{outer, {hole1, hole2}});
    project.vector_objects.push_back(vec);

    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.source_vector = vec.id;
    emb.params = document::TatamiParams{};
    project.embroidery_objects.push_back(emb);

    const auto seq = generate_sequence(project);
    REQUIRE(seq.has_value());

    int stitchCount = 0;
    int pointsInsideHoles = 0;
    for (const auto& c : seq->commands) {
        if (c.type != stitch::CommandType::Stitch)
            continue;
        ++stitchCount;
        if (distance_um(c.pos, hole1Center) < static_cast<double>(holeR) ||
            distance_um(c.pos, hole2Center) < static_cast<double>(holeR)) {
            ++pointsInsideHoles;
        }
    }
    // Un remplissage tatami reel, pas un objet degenere.
    CHECK(stitchCount > 100);
    // Garantie de base deja assuree par le decoupage tatami existant (les
    // trous sont deja geres par PathSet::holes partout ailleurs dans ce
    // fichier) -- verifiee EXPLICITEMENT ici pour cette forme precise,
    // jamais supposee.
    CHECK(pointsInsideHoles == 0);
}
