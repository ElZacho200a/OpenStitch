// SPDX-License-Identifier: Apache-2.0
//
// Etape 2 de la refonte "decomposition topologique" (plan approuve,
// docs/source/satin.md) : classification RegionClass/JunctionType, lecture
// seule sur le squelette existant -- aucune decision de coupe/appariement
// n'en depend encore (mode shadow-log, cf. plan).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

#include "openstitch/auto_satin/auto_satin.hpp"
#include "openstitch/auto_satin/shapes.hpp"
#include "openstitch/satin_planning/topology.hpp"

using namespace openstitch;
using namespace openstitch::satin_planning;

namespace {

auto_satin::AutoSatinAnalysis analyze(const std::string& shapeName) {
    const auto shape = auto_satin::make_shape(shapeName);
    REQUIRE(shape.has_value());
    auto analysis = auto_satin::analyze_region(*shape, {});
    REQUIRE(analysis.has_value());
    return std::move(*analysis);
}

// Jonction T synthetique (memes valeurs que test_branch_pairing.cpp) : deux
// arcs colineaires (E0/E1) + un troisieme perpendiculaire (E2) -- une seule
// paire domine nettement, cas T canonique.
SkeletonGraph make_synthetic_t() {
    SkeletonGraph graph;
    graph.nodes = {
        SkeletonNode{0, Vec2um{Micrometers{0}, Micrometers{0}}, SkeletonNodeType::Endpoint, 1000.0},
        SkeletonNode{1, Vec2um{Micrometers{10'000}, Micrometers{0}}, SkeletonNodeType::Junction,
                     1000.0},
        SkeletonNode{2, Vec2um{Micrometers{20'000}, Micrometers{0}}, SkeletonNodeType::Endpoint,
                     1000.0},
        SkeletonNode{3, Vec2um{Micrometers{10'000}, Micrometers{10'000}},
                     SkeletonNodeType::Endpoint, 500.0},
    };
    graph.edges = {
        SkeletonEdge{
            0,
            0,
            1,
            {Vec2um{Micrometers{0}, Micrometers{0}}, Vec2um{Micrometers{10'000}, Micrometers{0}}},
            {1000.0, 1000.0},
            10'000.0},
        SkeletonEdge{1,
                     1,
                     2,
                     {Vec2um{Micrometers{10'000}, Micrometers{0}},
                      Vec2um{Micrometers{20'000}, Micrometers{0}}},
                     {1000.0, 1000.0},
                     10'000.0},
        SkeletonEdge{2,
                     1,
                     3,
                     {Vec2um{Micrometers{10'000}, Micrometers{0}},
                      Vec2um{Micrometers{10'000}, Micrometers{10'000}}},
                     {1000.0, 500.0},
                     10'000.0},
    };
    return graph;
}

// Jonction Y synthetique : trois arcs a 120 degres les uns des autres,
// meme largeur -- aucune paire ne devrait dominer les deux autres.
SkeletonGraph make_synthetic_y() {
    SkeletonGraph graph;
    const double r = 10'000.0;
    const auto arm = [&](double angleDeg) {
        const double rad = angleDeg * std::numbers::pi / 180.0;
        return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(r * std::cos(rad)))},
                      Micrometers{static_cast<std::int32_t>(std::lround(r * std::sin(rad)))}};
    };
    graph.nodes = {
        SkeletonNode{0, arm(90.0), SkeletonNodeType::Endpoint, 1000.0},
        SkeletonNode{1, Vec2um{Micrometers{0}, Micrometers{0}}, SkeletonNodeType::Junction, 1000.0},
        SkeletonNode{2, arm(210.0), SkeletonNodeType::Endpoint, 1000.0},
        SkeletonNode{3, arm(330.0), SkeletonNodeType::Endpoint, 1000.0},
    };
    graph.edges = {
        SkeletonEdge{
            0, 1, 0, {Vec2um{Micrometers{0}, Micrometers{0}}, arm(90.0)}, {1000.0, 1000.0}, r},
        SkeletonEdge{
            1, 1, 2, {Vec2um{Micrometers{0}, Micrometers{0}}, arm(210.0)}, {1000.0, 1000.0}, r},
        SkeletonEdge{
            2, 1, 3, {Vec2um{Micrometers{0}, Micrometers{0}}, arm(330.0)}, {1000.0, 1000.0}, r},
    };
    return graph;
}

std::uint32_t single_junction_id(const SkeletonGraph& graph) {
    const auto it = std::find_if(graph.nodes.begin(), graph.nodes.end(), [](const SkeletonNode& n) {
        return n.type == SkeletonNodeType::Junction;
    });
    REQUIRE(it != graph.nodes.end());
    return it->id;
}

} // namespace

TEST_CASE("classify_junction : T synthetique -- une paire colineaire domine nettement") {
    const SkeletonGraph graph = make_synthetic_t();
    const SatinJunction j = classify_junction(graph, 1);
    CHECK(j.node_id == 1);
    CHECK(j.incident_branches == std::vector<std::uint32_t>{0, 1, 2});
    CHECK(j.type == JunctionType::T);
}

TEST_CASE(
    "classify_junction : Y synthetique -- trois branches a 120 degres, aucune paire dominante") {
    const SkeletonGraph graph = make_synthetic_y();
    const SatinJunction j = classify_junction(graph, 1);
    CHECK(j.type == JunctionType::Y);
}

TEST_CASE("classify_junction : forme reelle t -- classee T") {
    const auto analysis = analyze("t");
    const auto& graph = analysis.debug.graph;
    REQUIRE(graph.junction_count() == 1);
    const std::uint32_t junctionId = single_junction_id(graph);
    const SatinJunction j = classify_junction(graph, junctionId);
    CHECK(j.type == JunctionType::T);
}

TEST_CASE("classify_junction : forme reelle cross -- classee X") {
    const auto analysis = analyze("cross");
    const auto& graph = analysis.debug.graph;
    REQUIRE(graph.junction_count() == 1);
    const std::uint32_t junctionId = single_junction_id(graph);
    const SatinJunction j = classify_junction(graph, junctionId);
    CHECK(j.incident_branches.size() == 4);
    CHECK(j.type == JunctionType::X);
}

TEST_CASE("classify_edge : rectangle -- aucune jonction, largeur constante -> Regular") {
    const auto analysis = analyze("rectangle");
    const auto& graph = analysis.debug.graph;
    REQUIRE(graph.junction_count() == 0);
    REQUIRE_FALSE(graph.edges.empty());
    const RegionClassification c = classify_edge(graph, graph.edges.front());
    CHECK(c.region_class == RegionClass::Regular);
    CHECK(c.reasons.empty());
}

TEST_CASE("classify_edge : T -- l'arete touchant la jonction est Singular (JunctionDegreeGE3)") {
    const auto analysis = analyze("t");
    const auto& graph = analysis.debug.graph;
    REQUIRE(graph.junction_count() == 1);
    bool foundSingularNearJunction = false;
    for (const auto& edge : graph.edges) {
        const RegionClassification c = classify_edge(graph, edge);
        const bool touchesJunction =
            std::any_of(graph.nodes.begin(), graph.nodes.end(), [&](const SkeletonNode& n) {
                return (n.id == edge.from || n.id == edge.to) &&
                       n.type == SkeletonNodeType::Junction;
            });
        if (touchesJunction) {
            CHECK(c.region_class == RegionClass::Singular);
            CHECK(std::find(c.reasons.begin(), c.reasons.end(),
                            SingularityReason::JunctionDegreeGE3) != c.reasons.end());
            foundSingularNearJunction = true;
        }
    }
    CHECK(foundSingularNearJunction);
}

TEST_CASE("classify_edge : changement de largeur abrupt -> Singular (AbruptWidthChange)") {
    // Arete synthetique dont le rayon local double sur sa longueur --
    // aucune jonction impliquee, seul le ratio de largeur doit declencher la
    // classification.
    SkeletonGraph graph;
    graph.nodes = {
        SkeletonNode{0, Vec2um{Micrometers{0}, Micrometers{0}}, SkeletonNodeType::Endpoint, 1000.0},
        SkeletonNode{1, Vec2um{Micrometers{10'000}, Micrometers{0}}, SkeletonNodeType::Endpoint,
                     3000.0},
    };
    graph.edges = {
        SkeletonEdge{
            0,
            0,
            1,
            {Vec2um{Micrometers{0}, Micrometers{0}}, Vec2um{Micrometers{10'000}, Micrometers{0}}},
            {1000.0, 3000.0},
            10'000.0},
    };
    const RegionClassification c = classify_edge(graph, graph.edges.front());
    CHECK(c.region_class == RegionClass::Singular);
    CHECK(std::find(c.reasons.begin(), c.reasons.end(), SingularityReason::AbruptWidthChange) !=
          c.reasons.end());
}
