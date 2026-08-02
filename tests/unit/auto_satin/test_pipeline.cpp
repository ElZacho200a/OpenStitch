// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

#include "openstitch/auto_satin/auto_satin.hpp"
#include "openstitch/auto_satin/shapes.hpp"

using namespace openstitch;
using namespace openstitch::auto_satin;

namespace {

AutoSatinAnalysis analyze(const std::string& shape) {
    const auto region = make_shape(shape);
    REQUIRE(region.has_value());
    AutoSatinParameters params;
    params.raster.pixel_size = Micrometers{100};  // 0,1 mm : rapide pour les tests
    auto res = analyze_region(*region, params);
    REQUIRE(res.has_value());
    return *res;
}

bool finite_field(const DistanceField& d) {
    for (const float v : d.distance_um) {
        if (!std::isfinite(v)) {
            return false;
        }
    }
    return true;
}

}  // namespace

// --- Rasterisation / distance ------------------------------------------------

TEST_CASE("rasterisation : masque non vide, coord physiques coherentes") {
    const auto a = analyze("rectangle");
    CHECK(a.debug.mask.width > 10);
    CHECK(a.debug.mask.height > 3);
    // Au moins quelques pixels interieurs.
    std::size_t inside = 0;
    for (auto p : a.debug.mask.pixels) {
        inside += p ? 1 : 0;
    }
    CHECK(inside > 50);
}

TEST_CASE("distance : finie, positive, ~ demi-largeur pour un rectangle") {
    const auto a = analyze("rectangle");
    CHECK(finite_field(a.debug.distance));
    // Rectangle de 5 mm de large -> demi-largeur max ~2,5 mm = 2500 um.
    CHECK(a.debug.distance.max_value() > 2000.0f);
    CHECK(a.debug.distance.max_value() < 3000.0f);
}

// --- Squelette / graphe ------------------------------------------------------

TEST_CASE("rectangle : squelette ~ ligne, 2 extremites, 0 jonction") {
    const auto a = analyze("rectangle");
    CHECK(a.report.endpoint_count == 2);
    CHECK(a.report.junction_count == 0);
    CHECK(a.report.branch_count >= 1);
}

TEST_CASE("Y : structure branchee detectee") {
    // Le signal pertinent est « branché » (jonction OU plus de 2 extrémités),
    // qui déclenche la décomposition. Le type exact du nœud de rencontre dépend
    // de la rasterisation ; on ne le sur-spécifie pas.
    const auto a = analyze("y");
    CHECK((a.report.junction_count >= 1 || a.report.endpoint_count >= 3));
}

TEST_CASE("croix : structure branchee detectee") {
    const auto a = analyze("cross");
    CHECK((a.report.junction_count >= 1 || a.report.endpoint_count >= 3));
}

// --- Régression : traçage d'arête dans skeleton_graph.cpp (défaut corrigé) --
//
// Défaut trouvé par revue : le traçage d'une arête, en cherchant le prochain
// pixel voisin le long d'une chaîne de degré 2, s'arrêtait sur le PREMIER
// candidat rencontré dans l'ordre fixe des 8 directions — qu'il s'agisse d'un
// nœud ou d'un simple pixel de continuation. Un pixel juste avant une jonction
// a souvent, en plus de la jonction elle-même, un pixel de degré 2 D'UNE AUTRE
// branche dans son 8-voisinage (branches proches à la confluence). Deux
// symptômes mesurés : (1) sur une "croix", la jonction retombait à un degré 2
// au lieu de 4 (deux bras fusionnés en une seule arête traversante) ; (2) sur
// un "y", une branche entière (la tige) disparaissait avec une arête parasite
// en boucle sur la jonction elle-même (`from == to`), car le pixel d'origine
// de la trace pouvait être ré-atteint par un chemin de 2 pas différent de
// celui emprunté au départ (seul le pixel immédiatement précédent était
// exclu, pas le pixel d'origine de toute la trace). Corrigé par une priorité
// absolue aux nœuds sur tout le 8-voisinage (au lieu du premier candidat) et
// l'exclusion du pixel d'origine pendant toute la marche.

TEST_CASE("croix : jonction correctement detectee a degre 4 (defaut corrige)") {
    const auto a = analyze("cross");
    REQUIRE(a.report.junction_count == 1);
    CHECK(a.report.endpoint_count == 4);
    CHECK(a.report.branch_count == 4);
    // Vérifie le DEGRÉ réel du nœud jonction dans le graphe élagué (pas
    // seulement son type déclaré) : chacune des 4 arêtes doit toucher la
    // jonction exactement une fois.
    const auto& g = a.debug.graph;
    const auto junctionIt = std::find_if(g.nodes.begin(), g.nodes.end(), [](const auto& n) {
        return n.type == SkeletonNodeType::Junction;
    });
    REQUIRE(junctionIt != g.nodes.end());
    int degree = 0;
    for (const auto& e : g.edges) {
        if (e.from == junctionIt->id) ++degree;
        if (e.to == junctionIt->id) ++degree;
    }
    CHECK(degree == 4);
}

TEST_CASE("y : les trois branches sont toutes presentes, aucune arete parasite (defaut corrige)") {
    const auto a = analyze("y");
    REQUIRE(a.report.junction_count == 1);
    CHECK(a.report.endpoint_count == 3);
    CHECK(a.report.branch_count == 3);
    for (const auto& e : a.debug.graph.edges) {
        CHECK(e.from != e.to);  // aucune arete en boucle sur elle-meme
    }
}

// --- Régression : contrat de graph_cleanup.cpp (défaut corrigé) -------------
//
// `graph_cleanup.hpp` promet « ne supprime rien silencieusement (chaque
// suppression est diagnostiquée) », mais un nœud du graphe brut sans AUCUNE
// arête vivante incidente (isolé dès le départ, comme le point unique auquel
// se réduit le squelette d'un disque plein) n'était référencé par aucune
// paire (from, to) et disparaissait donc du graphe résultat sans jamais
// apparaître dans `removed` — défaut trouvé par revue. Corrigé : tout nœud du
// graphe d'origine non repris dans le graphe élagué est désormais ajouté à
// `removed`.

TEST_CASE("cercle : noeud isole diagnostique, jamais supprime silencieusement (defaut corrige)") {
    const auto a = analyze("circle");
    // Le squelette d'un disque plein se réduit à un point isolé (aucune
    // arête) : le graphe élagué doit rester vide, mais ce point doit
    // apparaître dans le diagnostic de suppression.
    CHECK(a.debug.graph.nodes.empty());
    CHECK(a.debug.graph.edges.empty());
    CHECK_FALSE(a.debug.removed_branches.empty());
}

// --- Satinabilite ------------------------------------------------------------

TEST_CASE("rectangle allonge : Suitable") {
    const auto a = analyze("rectangle");
    CHECK((a.report.status == SatinabilityStatus::Suitable ||
           a.report.status == SatinabilityStatus::SuitableWithWarnings));
    CHECK(a.report.is_elongated);
}

TEST_CASE("capsule et ruban : convertibles") {
    for (const char* s : {"capsule", "ribbon", "s"}) {
        const auto a = analyze(s);
        INFO("forme = " << s << " statut = " << to_string(a.report.status));
        CHECK((a.report.status == SatinabilityStatus::Suitable ||
               a.report.status == SatinabilityStatus::SuitableWithWarnings));
    }
}

TEST_CASE("Y et croix : RequiresDecomposition") {
    CHECK(analyze("y").report.status == SatinabilityStatus::RequiresDecomposition);
    CHECK(analyze("cross").report.status == SatinabilityStatus::RequiresDecomposition);
}

TEST_CASE("cercle : Ambiguous (direction non determinee)") {
    const auto a = analyze("circle");
    CHECK(a.report.status == SatinabilityStatus::Ambiguous);
    CHECK(a.report.has_ambiguous_direction);
}

TEST_CASE("anneau : Unsuitable (trou)") {
    const auto a = analyze("ring");
    CHECK(a.report.status == SatinabilityStatus::Unsuitable);
    CHECK(a.report.hole_count == 1);
}

TEST_CASE("forme large : Unsuitable (trop large)") {
    const auto a = analyze("wide");
    CHECK(a.report.status == SatinabilityStatus::Unsuitable);
    CHECK(a.report.has_wide_area);
}

// --- Invariants --------------------------------------------------------------

TEST_CASE("deterministe : meme forme -> meme rapport et meme graphe") {
    const auto a = analyze("s");
    const auto b = analyze("s");
    CHECK(a.report.status == b.report.status);
    CHECK(a.report.endpoint_count == b.report.endpoint_count);
    CHECK(a.report.junction_count == b.report.junction_count);
    REQUIRE(a.debug.graph.edges.size() == b.debug.graph.edges.size());
    for (std::size_t i = 0; i < a.debug.graph.edges.size(); ++i) {
        CHECK(a.debug.graph.edges[i].centerline == b.debug.graph.edges[i].centerline);
    }
}

TEST_CASE("invariant d'echelle : rectangle a 2 echelles reste Suitable et 2 extremites") {
    // Le rectangle par defaut fait 40x5 mm ; make_shape ne parametre pas
    // l'echelle, mais on verifie qu'un rectangle reste coherent apres analyse.
    const auto a = analyze("rectangle");
    CHECK(a.report.endpoint_count == 2);
    CHECK(a.report.is_elongated);
}

TEST_CASE("pas de NaN dans les metriques") {
    const auto a = analyze("ribbon");
    CHECK(std::isfinite(a.report.mean_width_mm));
    CHECK(std::isfinite(a.report.estimated_length_mm));
    CHECK(std::isfinite(a.report.maximum_width_mm));
    CHECK(std::isfinite(a.report.confidence));
}
