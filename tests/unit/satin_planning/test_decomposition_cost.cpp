// SPDX-License-Identifier: Apache-2.0
//
// Etape 3 de la refonte "decomposition topologique" (plan approuve,
// docs/source/satin.md) : evaluate_decomposition_cost mesure REELLEMENT
// chaque candidat (split_region + evaluate_decomposition_generation, memes
// fonctions que le reste du pipeline SGSD), pas un proxy pre-construction
// seul.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>

#include "openstitch/auto_satin/auto_satin.hpp"
#include "openstitch/auto_satin/shapes.hpp"
#include "openstitch/satin_planning/decomposition_cost.hpp"

using namespace openstitch;
using namespace openstitch::satin_planning;

namespace {

std::uint32_t single_junction_id(const SkeletonGraph& graph) {
    const auto it = std::find_if(graph.nodes.begin(), graph.nodes.end(),
                                  [](const SkeletonNode& n) { return n.type == SkeletonNodeType::Junction; });
    REQUIRE(it != graph.nodes.end());
    return it->id;
}

struct ScoredCandidates {
    std::vector<std::pair<DecompositionCandidate, DecompositionCost>> scored;
    std::size_t best_index{0};
};

ScoredCandidates score_shape(const std::string& shapeName) {
    const auto shapeOpt = auto_satin::make_shape(shapeName);
    REQUIRE(shapeOpt.has_value());
    const auto& shape = *shapeOpt;
    auto_satin::SatinColumnsParameters prodParams;
    prodParams.geometry_mode = auto_satin::SatinGeometryMode::Parametric;
    const auto analysis = auto_satin::analyze_region(shape, prodParams.analysis);
    REQUIRE(analysis.has_value());
    const auto& graph = analysis->debug.graph;
    const std::uint32_t junctionId = single_junction_id(graph);

    const DecompositionCandidateSet candidates = enumerate_decomposition_candidates(graph, junctionId);
    DecompositionCostParams costParams;
    costParams.genParams = prodParams;

    ScoredCandidates out;
    double bestTotal = std::numeric_limits<double>::max();
    for (const auto& c : candidates.candidates) {
        const DecompositionCost cost = evaluate_decomposition_cost(shape, graph, c, costParams);
        if (cost.total < bestTotal) {
            bestTotal = cost.total;
            out.best_index = out.scored.size();
        }
        out.scored.emplace_back(c, cost);
    }
    return out;
}

}  // namespace

TEST_CASE("evaluate_decomposition_cost : T -- l'argmin de continuite reste gagnant (pas de surprise)") {
    // Sur un T canonique, la couverture reelle des deux candidats
    // geometriquement valides est deja quasi identique (§4 du plan de
    // refonte) : le terme de continuite, seul discriminant reel, doit donc
    // toujours designer le meme gagnant que l'ancien argmin -- aucune
    // regression de comportement attendue ici.
    const ScoredCandidates result = score_shape("t");
    REQUIRE(result.scored.size() == 3);
    CHECK(result.best_index == 0);  // candidat 0 = argmin de continuite (cf. enumerate_decomposition_candidates)
    CHECK(result.scored[0].second.continuity_cost < result.scored[1].second.continuity_cost);
}

TEST_CASE("evaluate_decomposition_cost : y_symmetric -- le candidat runner-up gagne reellement (gate etape 3)") {
    // Trois bras strictement identiques a 120 degres (cf. shapes.cpp) : les
    // 3 appariements ont un cout de continuite proche par symetrie, un cas
    // que l'ancien selecteur (argmin pre-construction) tranchait de facon
    // arbitraire (ordre d'id d'arete). Mesure reelle : le candidat 1
    // (continuite MOINS bonne que le candidat 0) couvre neanmoins
    // MESURABLEMENT mieux la region une fois reellement construit, et gagne
    // sur le cout total (couverture ponderee 2x plus que la continuite) --
    // exactement la demonstration attendue de la refonte (le solveur choisit
    // la meilleure decomposition MESUREE, pas la premiere plausible).
    const ScoredCandidates result = score_shape("y_symmetric");
    REQUIRE(result.scored.size() == 3);

    const auto& winner = result.scored[result.best_index];
    // Le vainqueur n'est PAS le candidat 0 (l'argmin de continuite pure) :
    // c'est la preuve concrete que la mesure reelle peut renverser le choix
    // pre-construction.
    CHECK(result.best_index != 0);
    // ... precisement parce que sa continuite est moins bonne que celle du
    // candidat 0, mais sa couverture reelle nettement meilleure -- jamais un
    // hasard de tri.
    CHECK(winner.second.continuity_cost > result.scored[0].second.continuity_cost);
    CHECK(winner.second.coverage_cost < result.scored[0].second.coverage_cost);
    CHECK(winner.second.total < result.scored[0].second.total);
}

TEST_CASE("evaluate_decomposition_cost : determinisme (meme cout a chaque execution)") {
    const ScoredCandidates a = score_shape("y_symmetric");
    const ScoredCandidates b = score_shape("y_symmetric");
    REQUIRE(a.scored.size() == b.scored.size());
    CHECK(a.best_index == b.best_index);
    for (std::size_t i = 0; i < a.scored.size(); ++i) {
        CHECK(a.scored[i].second.total == b.scored[i].second.total);
        CHECK(a.scored[i].second.coverage_cost == b.scored[i].second.coverage_cost);
    }
}

TEST_CASE("evaluate_decomposition_cost : cross -- deux traversees simultanees perd (limite architecturale connue, etape 4)") {
    // Defaut REEL trouve en tentant d'exploiter la variante "deux traversees
    // simultanees" d'un noeud degre 4 (§ etape 4, docs/source/satin.md) :
    // `split_region` derive ses coupes UNIQUEMENT de
    // `JunctionPairingReport::detached` (une coupe par arete detachee).
    // Le candidat "deux traversees simultanees" ne detache RIEN (les 4
    // aretes appartiennent a l'une des deux paires retenues) -- aucune
    // coupe generee, toute la region traitee comme UNE piece indivise, que
    // `try_local_satin` construit tres mal (un "+" entier comme un seul
    // rail). Ce test FIGE ce resultat (le trunk simple gagne nettement) comme
    // garde-fou : si `enumerate_decomposition_candidates`/`evaluate_
    // decomposition_cost` changeaient un jour de comportement ici sans
    // qu'une vraie methode de construction par recouvrement (§ plan de
    // refonte, etapes 5/6) n'ait ete ajoutee, ce serait un signal a
    // examiner, pas une amelioration a accepter telle quelle.
    const ScoredCandidates result = score_shape("cross");
    REQUIRE(result.scored.size() == 3);
    CHECK(result.best_index == 0);  // le trunk simple (comportement historique) reste gagnant
    // Pas un ecart marginal : la variante "deux traversees" perd nettement,
    // preuve qu'il ne s'agit pas d'un cas limite mais d'une impossibilite
    // de construction avec le mecanisme de coupe actuel.
    CHECK(result.scored[1].second.total > result.scored[0].second.total * 2.0);
}

TEST_CASE("format_decomposition_cost_report : rendu textuel exploitable pour le debug") {
    const ScoredCandidates result = score_shape("t");
    const std::string text = format_decomposition_cost_report(result.scored, result.best_index);
    CHECK_FALSE(text.empty());
    CHECK(text.find("Candidat 0") != std::string::npos);
    CHECK(text.find("[SELECTED]") != std::string::npos);
    CHECK(text.find("Total :") != std::string::npos);
}
