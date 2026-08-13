// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>
#include <vector>

#include "openstitch/auto_satin/auto_satin.hpp"
#include "openstitch/auto_satin/shapes.hpp"
#include "openstitch/satin_planning/branch_pairing.hpp"

using namespace openstitch;
using namespace openstitch::satin_planning;

namespace {

// Petit graphe synthetique fait main (independant de la rasterisation) pour
// verifier numeriquement les formules de cout : jonction N1 avec trois arcs,
// E0 (gauche) et E1 (droite) qui se prolongent en ligne droite, E2
// (perpendiculaire, plus etroit) qui ne devrait jamais etre choisi comme
// continuation naturelle.
SkeletonGraph make_synthetic_t_junction() {
    SkeletonGraph graph;
    graph.nodes = {
        SkeletonNode{0, Vec2um{Micrometers{0}, Micrometers{0}}, SkeletonNodeType::Endpoint, 1000.0},
        SkeletonNode{1, Vec2um{Micrometers{10'000}, Micrometers{0}}, SkeletonNodeType::Junction, 1000.0},
        SkeletonNode{2, Vec2um{Micrometers{20'000}, Micrometers{0}}, SkeletonNodeType::Endpoint, 1000.0},
        SkeletonNode{3, Vec2um{Micrometers{10'000}, Micrometers{10'000}}, SkeletonNodeType::Endpoint, 500.0},
    };
    graph.edges = {
        SkeletonEdge{0,
                     0,
                     1,
                     {Vec2um{Micrometers{0}, Micrometers{0}}, Vec2um{Micrometers{10'000}, Micrometers{0}}},
                     {1000.0, 1000.0},
                     10'000.0},
        SkeletonEdge{1,
                     1,
                     2,
                     {Vec2um{Micrometers{10'000}, Micrometers{0}}, Vec2um{Micrometers{20'000}, Micrometers{0}}},
                     {1000.0, 1000.0},
                     10'000.0},
        SkeletonEdge{2,
                     1,
                     3,
                     {Vec2um{Micrometers{10'000}, Micrometers{0}}, Vec2um{Micrometers{10'000}, Micrometers{10'000}}},
                     {1000.0, 500.0},
                     10'000.0},
    };
    return graph;
}

}  // namespace

TEST_CASE("continuation_cost : continuation droite = cout angle nul") {
    const SkeletonGraph graph = make_synthetic_t_junction();
    const ContinuationCost cost = continuation_cost(graph, 1, 0, 1);
    REQUIRE(cost.valid);
    CHECK(cost.angle_cost == Catch::Approx(0.0).margin(1e-9));
    CHECK(cost.width_cost == Catch::Approx(0.0).margin(1e-9));
    CHECK(cost.curvature_cost == Catch::Approx(0.0).margin(1e-9));
}

TEST_CASE("continuation_cost : branche perpendiculaire plus etroite = cout angle et largeur eleves") {
    const SkeletonGraph graph = make_synthetic_t_junction();
    const ContinuationCost straight = continuation_cost(graph, 1, 0, 1);
    const ContinuationCost perpendicular = continuation_cost(graph, 1, 0, 2);
    REQUIRE(straight.valid);
    REQUIRE(perpendicular.valid);
    CHECK(perpendicular.angle_cost == Catch::Approx(0.5).margin(1e-9));
    CHECK(perpendicular.width_cost > 0.0);
    CHECK(perpendicular.total > straight.total);
}

TEST_CASE("continuation_cost : arcs inconnus -> cout invalide") {
    const SkeletonGraph graph = make_synthetic_t_junction();
    const ContinuationCost cost = continuation_cost(graph, 1, 0, 99);
    CHECK_FALSE(cost.valid);
}

TEST_CASE("pair_branches_at_junction : jonction en T -- la continuation droite est retenue") {
    const SkeletonGraph graph = make_synthetic_t_junction();
    const JunctionPairingReport report = pair_branches_at_junction(graph, 1);
    REQUIRE(report.candidates.size() == 3);  // C(3,2)
    REQUIRE(report.selected_pair.size() == 2);
    CHECK(report.selected_pair[0] == 0);
    CHECK(report.selected_pair[1] == 1);
    REQUIRE(report.detached.size() == 1);
    CHECK(report.detached[0] == 2);
}

TEST_CASE("decompose_into_paths : jonction en T synthetique -- deux chemins, arc 2 detache") {
    const SkeletonGraph graph = make_synthetic_t_junction();
    const DecompositionReport report = decompose_into_paths(graph);
    REQUIRE(report.paths.size() == 2);

    const auto mainPath = std::find_if(report.paths.begin(), report.paths.end(),
                                        [](const SatinPath& p) { return p.edges.size() == 2; });
    REQUIRE(mainPath != report.paths.end());
    CHECK(mainPath->edges[0] == 0);
    CHECK(mainPath->edges[1] == 1);
    CHECK(mainPath->nodes == std::vector<std::uint32_t>{0, 1, 2});
    CHECK(mainPath->length_um == Catch::Approx(20'000.0));

    const auto secondaryPath = std::find_if(report.paths.begin(), report.paths.end(),
                                             [](const SatinPath& p) { return p.edges.size() == 1; });
    REQUIRE(secondaryPath != report.paths.end());
    CHECK(secondaryPath->edges[0] == 2);
    CHECK(secondaryPath->nodes == std::vector<std::uint32_t>{1, 3});

    // Partition exacte : chaque arc apparait exactement une fois au total.
    std::set<std::uint32_t> seen;
    for (const auto& p : report.paths)
        for (auto e : p.edges) CHECK(seen.insert(e).second);
    CHECK(seen.size() == graph.edges.size());
}

TEST_CASE("format_decomposition_report : rendu textuel exploitable pour le debug") {
    const SkeletonGraph graph = make_synthetic_t_junction();
    const DecompositionReport report = decompose_into_paths(graph);
    const std::string text = format_decomposition_report(graph, report);
    CHECK_FALSE(text.empty());
    CHECK(text.find("Jonction 1") != std::string::npos);
    CHECK(text.find("[SELECTED]") != std::string::npos);
    CHECK(text.find("chemin 0") != std::string::npos);
    CHECK(text.find("2 chemin(s)") != std::string::npos);
}

namespace {

auto_satin::AutoSatinAnalysis analyze(const std::string& shapeName) {
    const auto shape = auto_satin::make_shape(shapeName);
    REQUIRE(shape.has_value());
    auto analysis = auto_satin::analyze_region(*shape, {});
    REQUIRE(analysis.has_value());
    return std::move(*analysis);
}

}  // namespace

TEST_CASE("decompose_into_paths : rectangle -- aucune jonction, un seul chemin") {
    const auto analysis = analyze("rectangle");
    const auto& graph = analysis.debug.graph;
    REQUIRE(graph.junction_count() == 0);
    const DecompositionReport report = decompose_into_paths(graph);
    REQUIRE(report.paths.size() == 1);
    CHECK(report.paths.front().edges.size() == graph.edges.size());
}

TEST_CASE("decompose_into_paths : T -- une jonction, deux chemins (barre + pied)") {
    const auto analysis = analyze("t");
    const auto& graph = analysis.debug.graph;
    REQUIRE(graph.junction_count() == 1);
    const DecompositionReport report = decompose_into_paths(graph);
    REQUIRE(report.junctions.size() == 1);
    CHECK(report.junctions.front().selected_pair.size() == 2);
    CHECK(report.junctions.front().detached.size() == 1);
    CHECK(report.paths.size() == 2);
}

TEST_CASE("decompose_into_paths : croix -- jonction de degre 4, une continuation + deux branches secondaires") {
    const auto analysis = analyze("cross");
    const auto& graph = analysis.debug.graph;
    REQUIRE(graph.junction_count() == 1);
    const DecompositionReport report = decompose_into_paths(graph);
    REQUIRE(report.junctions.size() == 1);
    const auto& jr = report.junctions.front();
    REQUIRE(jr.candidates.size() == 6);  // C(4,2) : degre 4
    CHECK(jr.selected_pair.size() == 2);
    CHECK(jr.detached.size() == 2);
    // Une continuation principale (2 arcs fusionnes) + deux branches secondaires isolees.
    CHECK(report.paths.size() == 3);
}

TEST_CASE("decompose_into_paths : H -- le pont jonction-jonction reste detache aux deux bouts") {
    const auto analysis = analyze("h");
    const auto& graph = analysis.debug.graph;
    REQUIRE(graph.junction_count() == 2);
    const DecompositionReport report = decompose_into_paths(graph);
    REQUIRE(report.junctions.size() == 2);

    const auto isJunction = [&](std::uint32_t nodeId) {
        const auto it = std::find_if(graph.nodes.begin(), graph.nodes.end(),
                                      [&](const SkeletonNode& n) { return n.id == nodeId; });
        return it != graph.nodes.end() && it->type == SkeletonNodeType::Junction;
    };
    const auto bridgeIt = std::find_if(graph.edges.begin(), graph.edges.end(), [&](const SkeletonEdge& e) {
        return isJunction(e.from) && isJunction(e.to);
    });
    REQUIRE(bridgeIt != graph.edges.end());

    for (const auto& jr : report.junctions) {
        const bool bridgeDetached = std::find(jr.detached.begin(), jr.detached.end(), bridgeIt->id) != jr.detached.end();
        CHECK(bridgeDetached);
    }
    // Les deux montants + le pont, chacun son propre chemin.
    CHECK(report.paths.size() == 3);
}

TEST_CASE("decompose_into_paths : trident -- une jonction degre 3, deux chemins") {
    const auto analysis = analyze("trident");
    const auto& graph = analysis.debug.graph;
    REQUIRE(graph.junction_count() == 1);
    const DecompositionReport report = decompose_into_paths(graph);
    REQUIRE(report.junctions.size() == 1);
    CHECK(report.junctions.front().candidates.size() == 3);
    CHECK(report.paths.size() == 2);
}

TEST_CASE("decompose_into_paths : propriete de partition sur le corpus de formes historiques") {
    for (const std::string& name : {"rectangle", "capsule", "ribbon", "s", "y", "t", "cross", "h", "wide", "notch",
                                     "pinch", "trident"}) {
        INFO("forme = " << name);
        const auto analysis = analyze(name);
        const auto& graph = analysis.debug.graph;
        const DecompositionReport report = decompose_into_paths(graph);

        std::set<std::uint32_t> seen;
        for (const auto& p : report.paths)
            for (auto e : p.edges) CHECK(seen.insert(e).second);
        CHECK(seen.size() == graph.edges.size());

        for (const auto& jr : report.junctions) {
            const std::size_t c = jr.candidates.size();
            std::size_t degree = 2;  // un noeud Junction a toujours degre >= 3, donc >= 3 candidats
            while (degree * (degree - 1) / 2 < c) ++degree;
            REQUIRE(degree * (degree - 1) / 2 == c);
            CHECK(jr.selected_pair.size() + jr.detached.size() == degree);
            if (!jr.selected_pair.empty()) CHECK(jr.selected_pair.size() == 2);
        }
    }
}
