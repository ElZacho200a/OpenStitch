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

// Rectangle 100x8mm (meme gabarit exterieur que `hourglass_shape`) entaille
// de DEUX notches pointus (~143 degres de virage) sur les bords OPPOSES --
// un en bas pres de x=54mm, un en haut pres de x=61mm -- assez espaces pour
// que la ligne DROITE entre les deux pointes ne soit plus verticale mais
// nettement diagonale, PLUS une troisieme entaille RECTANGULAIRE (coins a
// 90 degres, filtree par `min_reflex_turn_deg`) creusee depuis le bord
// haut entre les deux, assez profonde (jusqu'a y=-1500) pour physiquement
// obstruer cette diagonale (qui passe par (57500,0), a l'interieur meme de
// l'obstruction) sans toucher aux deux notches pointus. La ligne droite
// echoue donc necessairement (point median hors de la region -- litteralement
// dans le trou de l'obstruction), alors qu'un chemin en coude passant sous
// l'obstruction (matiere restante entre y=-4000 et y=-1500 sur toute sa
// largeur) reste entierement dans la matiere. Construite specifiquement pour
// exercer honnetement le mecanisme de coupe polygonale (§14, dernier
// element) -- aucune forme du corpus reel ne le declenche a ce jour ;
// dimensions et positions verifiees empiriquement (plusieurs iterations,
// cf. historique de developpement) pour que le point de coude trouve par
// `find_elbow_waypoint` reste a bonne distance de l'obstruction et des
// epaules des notches.
geometry::PathSet elbow_obstructed_hourglass_shape() {
    geometry::Path outer;
    outer.closed = true;
    outer.nodes = {
        node(0, -4'000),      node(53'000, -4'000), node(54'000, -1'000), node(55'000, -4'000),
        node(100'000, -4'000), node(100'000, 4'000), node(62'000, 4'000), node(61'000, 1'000),
        node(60'000, 4'000),  node(58'500, 4'000),   node(58'500, -1'500), node(56'500, -1'500),
        node(56'500, 4'000),  node(0, 4'000),
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

TEST_CASE(
    "generate_concavity_cut_candidates : sablier obstrue -- ligne droite hors de la region, coude reussit la "
    "separation") {
    const auto region = elbow_obstructed_hourglass_shape();
    ConcavityCutParams params;
    params.min_reflex_turn_deg = 120.0;  // filtre les coins a 90 degres de l'obstruction
    const auto candidates = generate_concavity_cut_candidates(region, params);
    INFO(format_concavity_cut_candidates(candidates));
    REQUIRE_FALSE(candidates.empty());

    const auto polygonal = std::find_if(candidates.begin(), candidates.end(), [](const ConcavityCutCandidate& c) {
        return c.family == "concavite->concavite (polygonale)";
    });
    REQUIRE(polygonal != candidates.end());
    CHECK(polygonal->valid);
    CHECK(polygonal->waypoint.has_value());

    // Coupe reellement en deux moities comparables, pas un fragment degenere
    // -- la meme regle de sante que le sablier droit.
    const double originalAreaMm2 = geometry::path_set_area_um2(region) / 1e6;
    CHECK(polygonal->first_piece_area_mm2 > 0.2 * originalAreaMm2);
    CHECK(polygonal->second_piece_area_mm2 > 0.2 * originalAreaMm2);
    CHECK(polygonal->first_piece_area_mm2 + polygonal->second_piece_area_mm2 <= originalAreaMm2 + 0.5);

    // Aucune famille "concavite->concavite" DROITE (non polygonale) ne doit
    // avoir reussi pour cette paire -- la separation VIENT du coude, pas
    // d'un hasard de tolerance geometrique. `generate_concavity_cut_candidates`
    // bascule vers la variante polygonale des qu'un point median est hors de
    // la region, donc aucune entree "concavite->concavite" pure n'apparait
    // meme dans les candidats REJETES pour cette paire.
    CHECK_FALSE(std::any_of(candidates.begin(), candidates.end(),
                             [](const ConcavityCutCandidate& c) { return c.family == "concavite->concavite"; }));
}

TEST_CASE(
    "generate_concavity_cut_candidates : sablier obstrue, try_elbow_cuts=false -- rejet propre sans tenter de "
    "coude") {
    // Meme forme, mecanisme desactive explicitement (cf. doc de
    // `ConcavityCutParams::try_elbow_cuts`) : ancre de non-regression pour
    // le comportement "lignes droites seules" -- la paire doit alors etre
    // rejetee proprement (point median hors de la region), sans qu'aucune
    // entree "(polygonale)" n'apparaisse.
    const auto region = elbow_obstructed_hourglass_shape();
    ConcavityCutParams params;
    params.min_reflex_turn_deg = 120.0;
    params.try_elbow_cuts = false;
    const auto candidates = generate_concavity_cut_candidates(region, params);
    INFO(format_concavity_cut_candidates(candidates));

    const auto straight = std::find_if(candidates.begin(), candidates.end(), [](const ConcavityCutCandidate& c) {
        return c.family == "concavite->concavite";
    });
    REQUIRE(straight != candidates.end());
    CHECK_FALSE(straight->valid);
    CHECK(straight->rejection_reason == "point median hors de la region");
    CHECK_FALSE(std::any_of(candidates.begin(), candidates.end(), [](const ConcavityCutCandidate& c) {
        return c.family == "concavite->concavite (polygonale)";
    }));
}

TEST_CASE("format_concavity_cut_candidates : rendu textuel exploitable pour le debug") {
    const auto candidates = generate_concavity_cut_candidates(shape("notch"));
    const std::string text = format_concavity_cut_candidates(candidates);
    CHECK_FALSE(text.empty());
    CHECK(text.find("Total :") != std::string::npos);
}
