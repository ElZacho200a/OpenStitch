// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "openstitch/auto_satin/auto_satin.hpp"
#include "openstitch/auto_satin/shapes.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/satin_planning/concavity_cuts.hpp"

using namespace openstitch;
using namespace openstitch::satin_planning;

namespace {

geometry::PathSet shape(const std::string& name) {
    const auto s = auto_satin::make_shape(name);
    REQUIRE(s.has_value());
    return *s;
}

auto_satin::SatinColumnsParameters parametric_params() {
    auto_satin::SatinColumnsParameters params;
    params.geometry_mode = auto_satin::SatinGeometryMode::Parametric;
    return params;
}

geometry::PathNode node(double x, double y) {
    return geometry::PathNode{Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(x))},
                                      Micrometers{static_cast<std::int32_t>(std::lround(y))}},
                               geometry::NodeType::Corner, std::nullopt, std::nullopt};
}

// Forme "sablier" construite a la main : un ruban 100x8mm entaille d'un
// notch en V pointant vers le HAUT depuis le bord bas, et d'un second notch
// en V pointant vers le BAS depuis le bord haut, les deux pointes se faisant
// face pres du centre (x=50000) -- exactement DEUX sommets reflex reels
// (une pointe de notch = une concavite, contrairement a un notch UNIQUE
// comme `notch`/`pinch` du corpus, qui n'en a qu'une seule), le cas
// canonique de la famille concavite->concavite : couper une ligne droite
// entre les deux pointes separe proprement les deux moities. Dimensions
// choisies EMPIRIQUEMENT (elongation mesuree via `analyze_region`, pas
// seulement le ratio largeur/hauteur de la boite englobante -- deux essais
// precedents, 40x20mm puis 60x10mm, produisaient des moities dont
// l'elongation REELLE du squelette (apres elagage des extremites) tombait
// juste sous le seuil `min_elongation` (2,5), classees `Ambiguous` et donc
// jamais construisibles) pour que chaque moitie APRES coupe reste elancee
// (~45x8mm, elongation mesuree ~5,6).
geometry::PathSet hourglass_shape() {
    geometry::Path outer;
    outer.closed = true;
    outer.nodes = {
        node(0, -4'000),      node(46'000, -4'000), node(50'000, -1'000), node(54'000, -4'000),
        node(100'000, -4'000), node(100'000, 4'000), node(54'000, 4'000), node(50'000, 1'000),
        node(46'000, 4'000),  node(0, 4'000),
    };
    return geometry::PathSet{outer, {}};
}

}  // namespace

TEST_CASE("generate_concavity_cut_candidates : rectangle -- aucune concavite, aucun candidat") {
    // Un rectangle est entierement convexe : aucun sommet reflex, donc
    // aucun candidat d'aucune des deux familles -- comportement attendu,
    // pas une erreur.
    const auto candidates = generate_concavity_cut_candidates(shape("rectangle"));
    CHECK(candidates.empty());
}

TEST_CASE("generate_concavity_cut_candidates : notch -- une seule concavite reelle -> famille bord oppose seule") {
    // `notch` (un seul V entaillant le bord haut) n'a qu'UN SEUL sommet
    // reflex reel (la pointe du V -- verifie empiriquement : les deux
    // "epaules" du V sont en fait CONVEXES, pas concaves, contrairement a
    // l'intuition visuelle). La famille concavite->concavite exige une PAIRE
    // de sommets reflex ; elle ne produit donc naturellement rien ici --
    // seule concavite->bord oppose s'applique, et c'est elle qui resout le
    // cas (cf. create_satin_plan : notch/pinch, tests/unit/satin_planning/
    // test_satin_plan.cpp).
    const auto candidates = generate_concavity_cut_candidates(shape("notch"));
    INFO(format_concavity_cut_candidates(candidates));
    REQUIRE_FALSE(candidates.empty());

    const bool hasValidOppositeEdge = std::any_of(candidates.begin(), candidates.end(), [](const ConcavityCutCandidate& c) {
        return c.valid && c.family == "concavite->bord oppose";
    });
    CHECK(hasValidOppositeEdge);
    CHECK_FALSE(std::any_of(candidates.begin(), candidates.end(),
                             [](const ConcavityCutCandidate& c) { return c.family == "concavite->concavite"; }));

    // Chaque candidat VALIDE doit conserver l'aire (aucune matiere créée ni
    // perdue au-dela d'une fine bande de coupe).
    const double originalAreaMm2 = geometry::path_set_area_um2(shape("notch")) / 1e6;
    for (const auto& c : candidates) {
        if (!c.valid) continue;
        CHECK(c.first_piece_area_mm2 + c.second_piece_area_mm2 <= originalAreaMm2 + 0.5);
        CHECK(c.first_piece_area_mm2 > 0.0);
        CHECK(c.second_piece_area_mm2 > 0.0);
    }
}

TEST_CASE("generate_concavity_cut_candidates : sablier -- deux concavites reelles -> famille concavite->concavite") {
    const auto region = hourglass_shape();
    const auto candidates = generate_concavity_cut_candidates(region);
    INFO(format_concavity_cut_candidates(candidates));
    REQUIRE_FALSE(candidates.empty());

    const bool hasValidPairFamily = std::any_of(candidates.begin(), candidates.end(), [](const ConcavityCutCandidate& c) {
        return c.valid && c.family == "concavite->concavite";
    });
    CHECK(hasValidPairFamily);

    const double originalAreaMm2 = geometry::path_set_area_um2(region) / 1e6;
    for (const auto& c : candidates) {
        if (!c.valid) continue;
        CHECK(c.first_piece_area_mm2 + c.second_piece_area_mm2 <= originalAreaMm2 + 0.5);
    }
}

TEST_CASE("select_best_concavity_cut : sablier -- choisit la coupe qui separe reellement les deux moities") {
    const auto region = hourglass_shape();
    const auto candidates = generate_concavity_cut_candidates(region);
    const auto chosen = select_best_concavity_cut(candidates, parametric_params(), {}, Micrometers{400});
    REQUIRE(chosen.has_value());
    const auto& winner = candidates[*chosen];
    CHECK(winner.valid);
    // Une vraie coupe qui separe le sablier en deux moities comparables --
    // pas un fragment degenere.
    const double originalAreaMm2 = geometry::path_set_area_um2(region) / 1e6;
    CHECK(winner.first_piece_area_mm2 > 0.2 * originalAreaMm2);
    CHECK(winner.second_piece_area_mm2 > 0.2 * originalAreaMm2);
}

TEST_CASE("select_best_concavity_cut : notch -- choisit un candidat reellement construit et mesure") {
    const auto candidates = generate_concavity_cut_candidates(shape("notch"));
    const auto chosen = select_best_concavity_cut(candidates, parametric_params(), {}, Micrometers{400});
    REQUIRE(chosen.has_value());
    REQUIRE(*chosen < candidates.size());
    CHECK(candidates[*chosen].valid);
}

TEST_CASE("select_best_concavity_cut : aucun candidat -- renvoie nullopt proprement") {
    const std::vector<ConcavityCutCandidate> empty;
    const auto chosen = select_best_concavity_cut(empty, parametric_params(), {}, Micrometers{400});
    CHECK_FALSE(chosen.has_value());
}

TEST_CASE("format_concavity_cut_candidates : rendu textuel exploitable pour le debug") {
    const auto candidates = generate_concavity_cut_candidates(shape("notch"));
    const std::string text = format_concavity_cut_candidates(candidates);
    CHECK_FALSE(text.empty());
    CHECK(text.find("Total :") != std::string::npos);
}
