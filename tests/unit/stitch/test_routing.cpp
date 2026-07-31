// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "openstitch/stitch_generation/generate.hpp"
#include "openstitch/stitch_generation/routing.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

namespace {

Vec2um P(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

RouteColumn col(ObjectId id, Vec2um s, Vec2um e) {
    return RouteColumn{id, s, e};
}

// Colonne satin droite horizontale [x0..x1], hauteur 5 mm, deux barreaux.
document::SatinParams straight_column(std::int32_t x0, std::int32_t x1) {
    document::SatinParams sp;
    sp.rail_a.closed = false;
    sp.rail_a.nodes = {{P(x0, 0), geometry::NodeType::Corner, {}, {}},
                       {P(x1, 0), geometry::NodeType::Corner, {}, {}}};
    sp.rail_b.closed = false;
    sp.rail_b.nodes = {{P(x0, 5'000), geometry::NodeType::Corner, {}, {}},
                       {P(x1, 5'000), geometry::NodeType::Corner, {}, {}}};
    sp.rungs = {{P(x0, 0), P(x0, 5'000)}, {P(x1, 0), P(x1, 5'000)}};
    sp.center_underlay = false;  // isole le routage (une seule passe par colonne)
    return sp;
}

document::Project group_project(const std::vector<document::SatinParams>& cols) {
    document::Project project;
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    project.vector_objects.push_back(vec);
    for (const auto& sp : cols) {
        document::EmbroideryObject emb;
        emb.id = project.object_ids.next();
        emb.source_vector = vec.id;  // même source -> même groupe auto-satin
        emb.rgb = {10, 20, 30};      // même couleur
        emb.params = sp;
        project.embroidery_objects.push_back(emb);
    }
    return project;
}

}  // namespace

TEST_CASE("routage : liste vide -> plan vide") {
    const auto plan = route_columns({}, P(0, 0), RoutingConfig{});
    CHECK(plan.steps.empty());
    CHECK(plan.jumps == 0);
    CHECK(plan.underpaths == 0);
}

TEST_CASE("routage : une colonne -> une etape, liaison de depart") {
    const auto plan = route_columns({col(ObjectId{1}, P(0, 0), P(10'000, 0))}, P(0, 0),
                                    RoutingConfig{});
    REQUIRE(plan.steps.size() == 1);
    CHECK(plan.steps[0].column_index == 0);
    CHECK(plan.steps[0].connector == ConnectorKind::Start);
    CHECK(plan.jumps == 0);
    CHECK(plan.underpaths == 0);
}

TEST_CASE("routage : reordonne pour minimiser le deplacement") {
    // Ordre document A(x0), C(x40k), B(x42k) : le glouton visite A, B, C dans
    // l'ordre spatial (colonnes adjacentes de 1 mm).
    std::vector<RouteColumn> cols = {col(ObjectId{1}, P(0, 0), P(20'000, 0)),
                                     col(ObjectId{3}, P(42'000, 0), P(62'000, 0)),
                                     col(ObjectId{2}, P(21'000, 0), P(41'000, 0))};
    const auto plan = route_columns(cols, P(0, 0), RoutingConfig{});
    REQUIRE(plan.steps.size() == 3);
    CHECK(plan.steps[0].column_index == 0);  // A
    CHECK(plan.steps[1].column_index == 2);  // B (index 2 dans l'entrée)
    CHECK(plan.steps[2].column_index == 1);  // C
    // Colonnes adjacentes (gaps ~1 mm < 8 mm) -> liaisons cachées, aucun saut.
    CHECK(plan.underpaths == 2);
    CHECK(plan.jumps == 0);
}

TEST_CASE("routage : oriente chaque colonne pour entrer par l'extremite proche") {
    // B a son extrémité « fin » près de la sortie de A -> B doit être inversée.
    std::vector<RouteColumn> cols = {col(ObjectId{1}, P(0, 0), P(10'000, 0)),
                                     col(ObjectId{2}, P(30'000, 0), P(11'000, 0))};
    const auto plan = route_columns(cols, P(0, 0), RoutingConfig{});
    REQUIRE(plan.steps.size() == 2);
    CHECK(plan.steps[0].column_index == 0);
    CHECK_FALSE(plan.steps[0].reversed);
    CHECK(plan.steps[1].column_index == 1);
    CHECK(plan.steps[1].reversed);  // entre par end(11000) proche de A.end(10000)
    CHECK(plan.underpaths == 1);    // gap 1 mm
}

TEST_CASE("routage : liaison longue -> saut (coupe), pas de trajet cache") {
    std::vector<RouteColumn> cols = {col(ObjectId{1}, P(0, 0), P(10'000, 0)),
                                     col(ObjectId{2}, P(100'000, 0), P(110'000, 0))};
    const auto plan = route_columns(cols, P(0, 0), RoutingConfig{});
    REQUIRE(plan.steps.size() == 2);
    CHECK(plan.steps[1].connector == ConnectorKind::Jump);
    CHECK(plan.jumps == 1);
    CHECK(plan.underpaths == 0);
}

TEST_CASE("generation : un groupe satin adjacent enchaine par trajets caches") {
    // Trois colonnes adjacentes (1 mm) : un seul saut initial, le reste cousu.
    auto project = group_project({straight_column(0, 20'000), straight_column(21'000, 41'000),
                                  straight_column(42'000, 62'000)});
    const auto seq = generate_sequence(project);
    REQUIRE(seq.has_value());

    int jumps = 0, travel = 0;
    for (const auto& c : seq->commands) {
        if (c.type == stitch::CommandType::Jump) ++jumps;
        if (c.type == stitch::CommandType::Stitch && c.pass == stitch::StitchPass::Travel) ++travel;
    }
    CHECK(jumps == 1);    // seule la pose initiale saute
    CHECK(travel > 0);    // liaisons cousues (passe Travel)
}

TEST_CASE("generation : un groupe eloigne conserve des sauts") {
    auto project = group_project({straight_column(0, 20'000), straight_column(200'000, 220'000)});
    const auto seq = generate_sequence(project);
    REQUIRE(seq.has_value());
    int jumps = 0;
    for (const auto& c : seq->commands) {
        if (c.type == stitch::CommandType::Jump) ++jumps;
    }
    CHECK(jumps == 2);  // pose initiale + saut de liaison (trop long pour cacher)
}
