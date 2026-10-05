// SPDX-License-Identifier: Apache-2.0
//
// Etape 2 de la refonte "decomposition topologique" (plan approuve,
// docs/source/satin.md) : enumerate_decomposition_candidates generalise
// decompose_into_paths (qui ne produit que l'argmin) sans changer son
// comportement par defaut -- mode shadow-log, aucune decision de coupe n'en
// depend encore.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>

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

std::uint32_t single_junction_id(const SkeletonGraph& graph) {
    const auto it = std::find_if(graph.nodes.begin(), graph.nodes.end(), [](const SkeletonNode& n) {
        return n.type == SkeletonNodeType::Junction;
    });
    REQUIRE(it != graph.nodes.end());
    return it->id;
}

} // namespace

TEST_CASE(
    "decompose_into_paths (JunctionOverride) : sans override, comportement historique inchange") {
    // Non-regression explicite : la nouvelle surcharge a parametre par
    // defaut ne doit RIEN changer pour tout appelant existant.
    const auto analysis = analyze("t");
    const auto& graph = analysis.debug.graph;
    const DecompositionReport withoutParam = decompose_into_paths(graph);
    const DecompositionReport withEmptyOverrides = decompose_into_paths(graph, {}, {});
    REQUIRE(withoutParam.paths.size() == withEmptyOverrides.paths.size());
    REQUIRE(withoutParam.junctions.size() == withEmptyOverrides.junctions.size());
    CHECK(withoutParam.junctions.front().selected_pair ==
          withEmptyOverrides.junctions.front().selected_pair);
}

TEST_CASE("decompose_into_paths (JunctionOverride) : force une paire non-argmin sur T") {
    const auto analysis = analyze("t");
    const auto& graph = analysis.debug.graph;
    const std::uint32_t junctionId = single_junction_id(graph);
    const DecompositionReport natural = decompose_into_paths(graph);
    REQUIRE(natural.junctions.front().selected_pair.size() == 2);
    const std::uint32_t naturalA = natural.junctions.front().selected_pair[0];
    const std::uint32_t naturalB = natural.junctions.front().selected_pair[1];
    const std::uint32_t detachedEdge = natural.junctions.front().detached.front();

    // Force une paire DIFFERENTE de l'argmin naturel (l'arete detachee
    // devient trunk avec l'une des deux aretes du bar).
    const DecompositionReport forced =
        decompose_into_paths(graph, {}, {JunctionOverride{junctionId, {detachedEdge, naturalA}}});
    REQUIRE(forced.junctions.size() == 1);
    CHECK(forced.junctions.front().selected_pair ==
          std::vector<std::uint32_t>{detachedEdge, naturalA});
    CHECK(std::find(forced.junctions.front().detached.begin(),
                    forced.junctions.front().detached.end(),
                    naturalB) != forced.junctions.front().detached.end());
    // `candidates` (le cout de chaque paire) reste inchange par l'override --
    // seule la selection differe, cf. doc de JunctionOverride.
    CHECK(forced.junctions.front().candidates.size() ==
          natural.junctions.front().candidates.size());
}

TEST_CASE(
    "decompose_into_paths (JunctionOverride) : forced_pair vide -- toutes les branches detachees") {
    const auto analysis = analyze("t");
    const auto& graph = analysis.debug.graph;
    const std::uint32_t junctionId = single_junction_id(graph);
    const DecompositionReport forced =
        decompose_into_paths(graph, {}, {JunctionOverride{junctionId, {}}});
    REQUIRE(forced.junctions.size() == 1);
    CHECK(forced.junctions.front().selected_pair.empty());
    CHECK(forced.junctions.front().detached.size() == 3); // T a degre 3 : les 3 aretes incidentes
    // 3 aretes toutes detachees -> 3 chemins independants, un par arete.
    CHECK(forced.paths.size() == 3);
}

TEST_CASE("enumerate_decomposition_candidates : T -- le candidat 0 reproduit l'argmin naturel "
          "(auto-coherence)") {
    // §4 du plan de refonte : sur un T canonique, le cout de continuite
    // existant favorise DEJA la barre comme trunk -- le premier candidat
    // genere doit donc etre exactement ce que decompose_into_paths produit
    // sans aucune nouvelle machinerie.
    const auto analysis = analyze("t");
    const auto& graph = analysis.debug.graph;
    const std::uint32_t junctionId = single_junction_id(graph);
    const DecompositionReport natural = decompose_into_paths(graph);

    const DecompositionCandidateSet candidates =
        enumerate_decomposition_candidates(graph, junctionId);
    REQUIRE_FALSE(candidates.candidates.empty());
    const auto& first = candidates.candidates.front().topology;
    REQUIRE(first.junctions.size() == 1);
    CHECK(first.junctions.front().selected_pair == natural.junctions.front().selected_pair);
    CHECK(first.paths.size() == natural.paths.size());
}

TEST_CASE("enumerate_decomposition_candidates : T -- degre 3, borne par defaut (3) produit "
          "exactement 3 candidats") {
    const auto analysis = analyze("t");
    const auto& graph = analysis.debug.graph;
    const std::uint32_t junctionId = single_junction_id(graph);
    const DecompositionCandidateSet candidates =
        enumerate_decomposition_candidates(graph, junctionId);
    // 3 aretes incidentes -> C(3,2)=3 paires possibles ; budget par defaut 3
    // = 2 paires (les 2 moins couteuses) + la variante independante.
    CHECK(candidates.candidates.size() == 3);
    // Le dernier candidat est toujours la variante "aucun trunk".
    CHECK(candidates.candidates.back().topology.junctions.front().selected_pair.empty());
    // candidate_id strictement croissant, dans l'ordre de generation.
    for (std::size_t i = 0; i < candidates.candidates.size(); ++i) {
        CHECK(candidates.candidates[i].candidate_id == i);
    }
}

TEST_CASE("enumerate_decomposition_candidates : max_candidates_per_junction=1 -- seul l'argmin, "
          "jamais de variante independante") {
    const auto analysis = analyze("t");
    const auto& graph = analysis.debug.graph;
    const std::uint32_t junctionId = single_junction_id(graph);
    const DecompositionCandidateSet candidates =
        enumerate_decomposition_candidates(graph, junctionId, {}, 1);
    REQUIRE(candidates.candidates.size() == 1);
    CHECK_FALSE(candidates.candidates.front().topology.junctions.front().selected_pair.empty());
}

TEST_CASE(
    "enumerate_decomposition_candidates : determinisme (memes candidats a chaque execution)") {
    const auto analysis = analyze("t");
    const auto& graph = analysis.debug.graph;
    const std::uint32_t junctionId = single_junction_id(graph);
    const DecompositionCandidateSet a = enumerate_decomposition_candidates(graph, junctionId);
    const DecompositionCandidateSet b = enumerate_decomposition_candidates(graph, junctionId);
    REQUIRE(a.candidates.size() == b.candidates.size());
    for (std::size_t i = 0; i < a.candidates.size(); ++i) {
        CHECK(a.candidates[i].description == b.candidates[i].description);
        CHECK(a.candidates[i].topology.paths.size() == b.candidates[i].topology.paths.size());
    }
}

TEST_CASE("decompose_into_paths (JunctionOverride) : forced_secondary_pair -- deux traversees "
          "simultanees (etape 4, croix)") {
    const auto analysis = analyze("cross");
    const auto& graph = analysis.debug.graph;
    const std::uint32_t junctionId = single_junction_id(graph);
    const DecompositionReport natural = decompose_into_paths(graph);
    REQUIRE(natural.junctions.front().selected_pair.size() == 2);
    const std::uint32_t trunkA = natural.junctions.front().selected_pair[0];
    const std::uint32_t trunkB = natural.junctions.front().selected_pair[1];
    REQUIRE(natural.junctions.front().detached.size() == 2);
    const std::uint32_t otherA = natural.junctions.front().detached[0];
    const std::uint32_t otherB = natural.junctions.front().detached[1];

    const DecompositionReport forced = decompose_into_paths(
        graph, {}, {JunctionOverride{junctionId, {trunkA, trunkB}, {otherA, otherB}}});
    REQUIRE(forced.junctions.size() == 1);
    CHECK(forced.junctions.front().selected_pair == std::vector<std::uint32_t>{trunkA, trunkB});
    CHECK(forced.junctions.front().secondary_pair == std::vector<std::uint32_t>{otherA, otherB});
    CHECK(forced.junctions.front()
              .detached.empty()); // les 4 aretes appartiennent a l'une des deux paires
    // Les deux paires traversent : 2 chemins (un par paire), jamais 4
    // chemins independants ni 1 seul chemin fusionnant tout.
    CHECK(forced.paths.size() == 2);
}

TEST_CASE("enumerate_decomposition_candidates : cross -- degre 4 genere la variante deux "
          "traversees simultanees") {
    const auto analysis = analyze("cross");
    const auto& graph = analysis.debug.graph;
    const std::uint32_t junctionId = single_junction_id(graph);
    const DecompositionCandidateSet candidates =
        enumerate_decomposition_candidates(graph, junctionId);
    // Degre 4, budget par defaut 3 : 1 trunk simple (le meilleur) + 1 variante
    // "deux traversees simultanees" + 1 variante "aucun trunk" -- jamais un
    // second trunk simple concurrent (§ etape 4 : la place lui est retiree
    // pour reserver le slot dual-through).
    REQUIRE(candidates.candidates.size() == 3);
    CHECK(candidates.candidates[0].description.find("trunk = aretes") != std::string::npos);
    CHECK(candidates.candidates[1].description.find("deux traversees simultanees") !=
          std::string::npos);
    CHECK(candidates.candidates[2].description.find("aucun trunk") != std::string::npos);

    const auto& dualThrough = candidates.candidates[1].topology.junctions.front();
    CHECK(dualThrough.selected_pair.size() == 2);
    CHECK(dualThrough.secondary_pair.size() == 2);
    CHECK(dualThrough.detached.empty());
    CHECK(candidates.candidates[1].topology.paths.size() == 2);
    // La paire primaire et la paire secondaire doivent etre disjointes --
    // ensemble, elles couvrent les 4 aretes incidentes exactement une fois.
    std::set<std::uint32_t> allEdges(dualThrough.selected_pair.begin(),
                                     dualThrough.selected_pair.end());
    allEdges.insert(dualThrough.secondary_pair.begin(), dualThrough.secondary_pair.end());
    CHECK(allEdges.size() == 4);

    // Le trunk simple (candidat 0), lui, garde le comportement historique :
    // 2 aretes detachees, 3 chemins.
    const auto& singleTrunk = candidates.candidates[0].topology.junctions.front();
    CHECK(singleTrunk.secondary_pair.empty());
    CHECK(singleTrunk.detached.size() == 2);
    CHECK(candidates.candidates[0].topology.paths.size() == 3);
}

TEST_CASE("format_decomposition_candidates_report : rendu textuel exploitable pour le debug") {
    const auto analysis = analyze("t");
    const auto& graph = analysis.debug.graph;
    const std::uint32_t junctionId = single_junction_id(graph);
    const DecompositionCandidateSet candidates =
        enumerate_decomposition_candidates(graph, junctionId);
    const std::string text = format_decomposition_candidates_report(candidates);
    CHECK_FALSE(text.empty());
    CHECK(text.find("Candidat 0") != std::string::npos);
    CHECK(text.find("Total :") != std::string::npos);
}
