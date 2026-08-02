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

RouteColumn network_col(ObjectId id, Vec2um s, Vec2um e,
                        std::optional<std::uint32_t> startJunction,
                        std::optional<std::uint32_t> endJunction) {
    return RouteColumn{id, s, e, startJunction, endJunction};
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

// --- Trajet caché non justifié par une jonction (défaut trouvé par revue) ---
//
// Avant correction, seule `underpath_max` (8 mm) décidait d'un trajet caché,
// sans jamais regarder si la liaison était justifiée par une jonction
// commune. Deux colonnes SANS AUCUN lien topologique mais distantes de 5 à
// 8 mm (deux lettres rapprochées, une forme en C dont les deux bouts se
// frôlent sans être reliés) étaient donc cousues en ligne droite à travers un
// espace dont rien ne garantit qu'il est couvert de tissu.

TEST_CASE("routage : proche mais sans jonction commune -> desormais un saut") {
    std::vector<RouteColumn> cols = {col(ObjectId{1}, P(0, 0), P(10'000, 0)),
                                     col(ObjectId{2}, P(15'000, 0), P(25'000, 0))};
    const auto plan = route_columns(cols, P(0, 0), RoutingConfig{});
    REQUIRE(plan.steps.size() == 2);
    // Gap = 5 mm : sous l'ancien seuil unique (8 mm), sous l'ancien code un
    // trajet caché aurait été accepté ; au-dessus du nouveau seuil sans
    // jonction (1,5 mm) -> saut.
    CHECK(plan.steps[1].connector == ConnectorKind::Jump);
    CHECK(plan.jumps == 1);
    CHECK(plan.underpaths == 0);
}

TEST_CASE("routage : quasi en contact sans jonction -> trajet cache tolere") {
    std::vector<RouteColumn> cols = {col(ObjectId{1}, P(0, 0), P(10'000, 0)),
                                     col(ObjectId{2}, P(11'000, 0), P(20'000, 0))};
    const auto plan = route_columns(cols, P(0, 0), RoutingConfig{});
    REQUIRE(plan.steps.size() == 2);
    // Gap = 1 mm < underpath_max_without_junction (1,5 mm) : un quasi-contact
    // reste toléré, la borne stricte ne pénalise pas les cas légitimes.
    CHECK(plan.steps[1].connector == ConnectorKind::Underpath);
    CHECK(plan.underpaths == 1);
}

TEST_CASE("routage : jonction commune valide reste toleree au-dela du seuil strict") {
    // Même écart (5 mm) que le premier test ci-dessus, mais justifié cette
    // fois par une jonction partagée : le trajet caché reste autorisé jusqu'à
    // underpath_max (8 mm), la jonction n'est pas pénalisée par le nouveau
    // seuil strict réservé aux liaisons NON justifiées.
    std::vector<RouteColumn> cols = {
        network_col(ObjectId{1}, P(0, 0), P(10'000, 0), std::nullopt, 7),
        network_col(ObjectId{2}, P(15'000, 0), P(25'000, 0), 7, std::nullopt)};
    const auto plan = route_columns(cols, P(0, 0), RoutingConfig{});
    REQUIRE(plan.steps.size() == 2);
    CHECK(plan.steps[1].junction == std::optional<std::uint32_t>{7});
    CHECK(plan.steps[1].connector == ConnectorKind::Underpath);
    CHECK(plan.underpaths == 1);
}

TEST_CASE("routage : une jonction commune admissible prime sur une proximite fortuite") {
    // Après A, C est plus proche géométriquement mais seule B partage la
    // jonction 7. La topologie doit donc conserver A -> B.
    std::vector<RouteColumn> cols = {
        network_col(ObjectId{1}, P(0, 0), P(10'000, 0), std::nullopt, 7),
        network_col(ObjectId{2}, P(12'000, 0), P(30'000, 0), 7, std::nullopt),
        network_col(ObjectId{3}, P(10'500, 0), P(40'000, 0), 9, std::nullopt)};
    const auto plan = route_columns(cols, P(0, 0), RoutingConfig{});
    REQUIRE(plan.steps.size() == 3);
    CHECK(plan.steps[0].column_index == 0);
    CHECK(plan.steps[1].column_index == 1);
    CHECK(plan.steps[1].junction == std::optional<std::uint32_t>{7});
    CHECK(plan.junction_links == 1);
}

TEST_CASE("routage : une jonction incoherente et trop distante ne force pas un underpath") {
    std::vector<RouteColumn> cols = {
        network_col(ObjectId{1}, P(0, 0), P(10'000, 0), std::nullopt, 7),
        network_col(ObjectId{2}, P(100'000, 0), P(120'000, 0), 7, std::nullopt)};
    const auto plan = route_columns(cols, P(0, 0), RoutingConfig{});
    REQUIRE(plan.steps.size() == 2);
    CHECK_FALSE(plan.steps[1].junction.has_value());
    CHECK(plan.steps[1].connector == ConnectorKind::Jump);
    CHECK(plan.junction_links == 0);
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

TEST_CASE("generation : la topologie SatinParams atteint le routage du groupe") {
    auto a = straight_column(0, 10'000);
    auto b = straight_column(12'000, 30'000);
    auto c = straight_column(10'500, 40'000);  // plus proche de A mais sans jonction commune
    a.topology = document::SatinSectionTopology{0, 3, std::nullopt, 7};
    b.topology = document::SatinSectionTopology{1, 3, 7, std::nullopt};
    c.topology = document::SatinSectionTopology{2, 3, 9, std::nullopt};
    auto project = group_project({a, b, c});

    const auto seq = generate_sequence(project);
    REQUIRE(seq.has_value());
    std::vector<ObjectId> topStitchOrder;
    for (const auto& command : seq->commands) {
        if (command.type != stitch::CommandType::Stitch ||
            command.pass != stitch::StitchPass::TopStitch) {
            continue;
        }
        if (topStitchOrder.empty() || topStitchOrder.back() != command.source) {
            topStitchOrder.push_back(command.source);
        }
    }
    REQUIRE(topStitchOrder.size() == 3);
    CHECK(topStitchOrder[0] == project.embroidery_objects[0].id);
    CHECK(topStitchOrder[1] == project.embroidery_objects[1].id);
    CHECK(topStitchOrder[2] == project.embroidery_objects[2].id);
}

TEST_CASE("generation : deux sections sans jonction a 5mm ne sont plus cousues a travers le vide") {
    // Même écart (5 mm) que "un groupe satin adjacent" (1 mm, toléré), mais
    // sans aucune information de jonction/topologie : avant correction, le
    // seuil unique (8 mm) aurait accepté un trajet caché ici aussi. Ces deux
    // colonnes ne partagent aucun réseau connu -- la liaison doit rester un
    // saut, jamais un fil cousu à travers un espace non garanti couvert.
    auto project = group_project({straight_column(0, 20'000), straight_column(25'000, 45'000)});
    const auto seq = generate_sequence(project);
    REQUIRE(seq.has_value());
    int jumps = 0, travel = 0;
    for (const auto& c : seq->commands) {
        if (c.type == stitch::CommandType::Jump) ++jumps;
        if (c.type == stitch::CommandType::Stitch && c.pass == stitch::StitchPass::Travel) ++travel;
    }
    CHECK(jumps == 2);   // pose initiale + saut de liaison (non justifiee)
    CHECK(travel == 0);  // aucun trajet cache sans jonction validee
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
