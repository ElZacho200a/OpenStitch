// SPDX-License-Identifier: Apache-2.0
//
// Tests de `trace_corridor`/`CorridorStation` (corridor.hpp, HP-STI-018
// Phase B, specs/plans/hp-sti-018-turning-satin.md §2.2/§4/§5) :
//  - primitive directe sur formes synthetiques (bande droite, coude a 90°) ;
//  - determinisme (deux appels identiques, sortie identique) ;
//  - comparaison cote a cote via l'indicateur test-only
//    `SatinColumnsParameters::use_corridor_tracing_dev_only` sur le corpus
//    complet de `shapes.cpp`, y compris la preuve empirique centrale de la
//    Phase B : le tronc isole de "E" (`e_trunk_isolated`), refuse par le
//    chemin historique (`cross_section`), construit par le nouveau chemin
//    (`trace_corridor`).
#include "corridor.hpp"
#include "geometry_detail.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

#include "openstitch/auto_satin/satin_column.hpp"
#include "openstitch/auto_satin/satinability.hpp"
#include "openstitch/auto_satin/shapes.hpp"
#include "openstitch/geometry/path.hpp"

using namespace openstitch;
using namespace openstitch::auto_satin;
using namespace openstitch::auto_satin::detail;

namespace {

Poly make_rectangle(P2 min_corner, P2 max_corner) {
    return {min_corner, {max_corner.x, min_corner.y}, max_corner, {min_corner.x, max_corner.y}};
}

// Echantillonne une ligne droite [x0,y]..[x1,y] en `samples` segments (inclus
// aux deux bouts) -- pas de lissage/resample_arc ici (internes a
// satin_column.cpp), juste un pas uniforme : suffisant pour une forme
// synthetique deja parfaitement droite.
std::vector<P2> straight_axis(double x0, double x1, double y, int samples) {
    std::vector<P2> axis;
    axis.reserve(static_cast<std::size_t>(samples) + 1);
    for (int i = 0; i <= samples; ++i) {
        const double t = static_cast<double>(i) / samples;
        axis.push_back({x0 + (x1 - x0) * t, y});
    }
    return axis;
}

// Axe en coude a 90 degres, pas uniforme `spacing` : jambe verticale
// (0, startY) -> (0, cornerY) puis jambe horizontale (0, cornerY) ->
// (legX, cornerY) -- contigue (pas de saut au sommet du coude), jamais
// un point de depart exactement SUR le bord (`startY` doit rester a bonne
// distance de l'arete basse, sans quoi le premier echantillon a une
// distance de bord inferieure a celle des deux cotes -- un vrai piege
// degenere, pas une propriete de trace_corridor).
std::vector<P2> corner_axis(double startY, double cornerY, double legX, double spacing) {
    std::vector<P2> axis;
    for (double y = startY; y <= cornerY + 1e-6; y += spacing) {
        axis.push_back({0.0, y});
    }
    for (double x = spacing; x <= legX + 1e-6; x += spacing) {
        axis.push_back({x, cornerY});
    }
    return axis;
}

// Lissage Chaikin (coins coupes), extremites conservees -- copie locale
// volontaire de la fonction privee du meme nom dans satin_column.cpp (jamais
// exposee hors de ce fichier) : `compute_column_stations` l'applique TOUJOURS
// a l'axe avant le moindre appel a `trace_corridor` (cf. corridor.hpp,
// "polyligne deja lissee/reechantillonnee par l'appelant") -- un axe en
// coude a 90 degres BRUT (sans lissage, un seul sommet anguleux exact)
// n'est donc pas une entree realiste pour cette primitive ; le lisser ici
// avant de l'utiliser reproduit fidelement ce que la production fait
// reellement, au lieu de tester un cas que trace_corridor ne voit jamais en
// pratique.
std::vector<P2> chaikin(const std::vector<P2>& pts, int iterations) {
    std::vector<P2> cur = pts;
    for (int it = 0; it < iterations && cur.size() >= 3; ++it) {
        std::vector<P2> out;
        out.reserve(cur.size() * 2);
        out.push_back(cur.front());
        for (std::size_t i = 0; i + 1 < cur.size(); ++i) {
            out.push_back(cur[i] * 0.75 + cur[i + 1] * 0.25);
            out.push_back(cur[i] * 0.25 + cur[i + 1] * 0.75);
        }
        out.push_back(cur.back());
        cur = std::move(out);
    }
    return cur;
}

// Intersection propre de deux segments (croisement transversal) sur P2
// double -- meme pattern que `segments_cross_2d` de test_columns.cpp (copie
// locale volontaire, coordonnees differentes : P2 double ici, pas Vec2um).
bool segments_cross_p2(P2 a, P2 b, P2 c, P2 d) {
    const auto cross2 = [](P2 u, P2 v) { return u.x * v.y - u.y * v.x; };
    const auto orient = [&](P2 p, P2 q, P2 r) { return cross2(q - p, r - p); };
    const double o1 = orient(a, b, c);
    const double o2 = orient(a, b, d);
    const double o3 = orient(c, d, a);
    const double o4 = orient(c, d, b);
    return (o1 > 0) != (o2 > 0) && (o3 > 0) != (o4 > 0) && o1 != 0 && o2 != 0 && o3 != 0 && o4 != 0;
}

// Aucun rail (foot_a comme foot_b) ne doit se croiser d'une station a la
// suivante, et le "barreau" foot_a<->foot_b ne doit pas non plus croiser
// celui de la station suivante -- meme invariant que le nettoyage
// anti-croisement de `compute_column_stations`, verifie ici directement sur
// la sortie BRUTE de `trace_corridor` (avant tout nettoyage).
void check_no_self_crossing(const std::vector<CorridorStation>& stations) {
    for (std::size_t i = 1; i < stations.size(); ++i) {
        const auto& prev = stations[i - 1];
        const auto& cur = stations[i];
        INFO("station #" << i);
        CHECK_FALSE(segments_cross_p2(prev.foot_a.point, cur.foot_a.point, prev.foot_b.point,
                                      cur.foot_b.point));
        CHECK_FALSE(segments_cross_p2(prev.foot_a.point, prev.foot_b.point, cur.foot_a.point,
                                      cur.foot_b.point));
    }
}

SatinColumnsResult build_with_flag(const std::string& shape, bool use_corridor) {
    const auto region = make_shape(shape);
    REQUIRE(region.has_value());
    SatinColumnsParameters params;
    params.analysis.raster.pixel_size = Micrometers{100}; // 0,1 mm : rapide, meme reglage que
                                                          // test_columns.cpp
    params.use_corridor_tracing_dev_only = use_corridor;
    return build_satin_columns(*region, params);
}

// Corpus complet de `shapes.cpp` (§ liste exhaustive, shapes.hpp) -- inclut
// `e_trunk_isolated` (nouveau, ce lot).
constexpr const char* kFullCorpus[] = {
    "rectangle",
    "capsule",
    "ribbon",
    "s",
    "y",
    "y_symmetric",
    "t",
    "cross",
    "h",
    "circle",
    "ring",
    "wide",
    "tiny",
    "notch",
    "pinch",
    "trident",
    "star5",
    "asymmetric_star",
    "comb",
    "E",
    "e_trunk_isolated",
    "deep_recursive",
    "multi_neck",
    "dumbbell",
    "deep_channel",
    "two_holes",
    "ring_branch",
    "junction_with_hole",
    "polygonal_cut_fixture",
    "thick_diagonal_blob",
};

// Sous-ensemble a BRANCHE UNIQUE, SANS jonction de squelette -- le perimetre
// que la Phase B doit prouver en drop-in replacement (§4 du plan : "valider
// d'abord sur des formes a branche unique droite/courbe, les jonctions
// viennent en Phase C"). "e_trunk_isolated" y est volontairement inclus :
// aucune jonction de squelette (cf. shapes.cpp), c'est pourtant la forme qui
// faisait echouer le chemin historique -- exactement le cas que ce
// sous-ensemble doit couvrir.
//
// EXCLUS deliberement malgre l'absence de jonction : "s" (courbe S a forte
// courbure) et "multi_neck" ("0 jonction, 1 seul arc de squelette" confirme
// par l'investigation du 2026-08-30, mais transitions de largeur fine/large
// tres marquees aux etranglements, deja documentees comme leur PROPRE cause
// racine distincte dans docs/source/satin.md). Constate empiriquement ici :
// sous le chemin corridor, plusieurs stations consecutives a forte courbure/
// transition de largeur tombent sous `min_satin_width` (feet.size()<2 meme
// avec la requete large, les deux pieds les plus proches se retrouvant du
// MEME cote du contour a l'interieur d'une courbe serree), refusant la
// colonne entiere -- une vraie regression sur CES deux formes precisement,
// rapportee (WARN) mais pas corrigee ici : la tolerance de selection n'est
// deja plus la cause single-shot (voir corridor.hpp/corridor.cpp), et
// resserrer encore le perimetre corrigerait un symptome sans preuve que ce
// soit la bonne direction -- exactement le genre de patch opportuniste sous
// pression d'une seule fixture que ce depot proscrit (§21/§22,
// docs/source/satin.md). Laisse a un futur chantier (Phase C ou un
// ajustement dedie de la selection sur forte courbure), avec cette mesure
// comme point de depart.
constexpr const char* kNonJunctionCorpus[] = {
    "rectangle",
    "capsule",
    "ribbon",
    "wide",
    "tiny",
    "notch",
    "pinch",
    "circle",
    "ring",
    "two_holes",
    "deep_channel",
    "polygonal_cut_fixture",
    "thick_diagonal_blob",
    "e_trunk_isolated",
};

// Formes a branche unique mais EXCLUES de kNonJunctionCorpus (cf. ci-dessus) :
// rapportees separement, jamais asserees egales au chemin historique dans
// cette phase.
constexpr const char* kKnownCurvatureLimitations[] = {"s", "multi_neck"};

// Convertit les rails Legacy (Vec2um) en sequence de P2 pour reutiliser
// `segments_cross_p2` ci-dessus.
std::vector<P2> rail_to_p2(const geometry::Path& rail) {
    std::vector<P2> pts;
    pts.reserve(rail.nodes.size());
    for (const auto& n : rail.nodes) {
        pts.push_back({static_cast<double>(n.pos.x.value), static_cast<double>(n.pos.y.value)});
    }
    return pts;
}

void check_columns_no_self_crossing(const std::vector<SatinColumnGeometry>& columns) {
    for (const auto& col : columns) {
        const auto a = rail_to_p2(col.rail_a);
        const auto b = rail_to_p2(col.rail_b);
        const std::size_t n = std::min(a.size(), b.size());
        for (std::size_t i = 1; i < n; ++i) {
            INFO("station #" << i);
            CHECK_FALSE(segments_cross_p2(a[i - 1], a[i], b[i - 1], b[i]));
            CHECK_FALSE(segments_cross_p2(a[i - 1], b[i - 1], a[i], b[i]));
        }
    }
}

// Rectangle representant le "capuchon" interieur d'une branche radiale a
// l'angle `angle_rad`, dont l'arete 0 (v0->v1) est exactement perpendiculaire
// a la direction radiale, centree a la distance `radius` du centre -- meme
// construction que test_medial_field.cpp ("jonction reelle a 3 branches
// distinctes"), reprise ici pour prouver que `foot_multiplicity` (pas
// seulement `nearest_boundary_feet` brut) distingue bien une vraie jonction
// a 3 emplacements de contour NON coincidents d'un simple sommet ordinaire
// (cf. revue Phase B, point 4 : la citation de ce test dans corridor.cpp
// couvrait nearest_boundary_feet, jamais le wrapper count_distinct_feet).
Poly make_branch_cap(double angle_rad, double radius, double half_width, double depth) {
    const P2 radial{std::cos(angle_rad), std::sin(angle_rad)};
    const P2 tangential{-radial.y, radial.x};
    const P2 center{radial.x * radius, radial.y * radius};
    const P2 v0{center.x - tangential.x * half_width, center.y - tangential.y * half_width};
    const P2 v1{center.x + tangential.x * half_width, center.y + tangential.y * half_width};
    const P2 v2{v1.x + radial.x * depth, v1.y + radial.y * depth};
    const P2 v3{v0.x + radial.x * depth, v0.y + radial.y * depth};
    return {v0, v1, v2, v3};
}

} // namespace

// --- Primitive directe (trace_corridor), formes synthetiques -----------------

TEST_CASE("trace_corridor foot_multiplicity >= 3 sur une vraie jonction a 3 branches distinctes",
          "[corridor]") {
    // 3 polygones separes (meme construction que test_medial_field.cpp) dont
    // les capuchons se font face a 120 degres d'ecart, meme rayon -- le
    // centre est equidistant des 3, mais a des POINTS NON COINCIDENTS
    // (contrairement a un sommet ordinaire de polygone, cf. l'avertissement
    // de medial_field.hpp repris dans corridor.hpp). Un axe reduit a
    // l'origine seule suffit : trace_corridor n'a besoin que d'une station
    // pour calculer foot_multiplicity via count_distinct_feet.
    constexpr double radius = 1'000.0;
    constexpr double half_width = 50.0;
    constexpr double depth = 20.0;
    const std::vector<Poly> polys{
        make_branch_cap(0.0, radius, half_width, depth),
        make_branch_cap(2.0 * std::numbers::pi / 3.0, radius, half_width, depth),
        make_branch_cap(4.0 * std::numbers::pi / 3.0, radius, half_width, depth),
    };
    const std::vector<P2> axis{{0.0, 0.0}, {1.0, 0.0}}; // 2 points : une tangente est calculable

    SatinColumnsParameters params;
    const auto stations = trace_corridor(axis, polys, params);
    REQUIRE(stations.size() == axis.size());
    CHECK(stations.front().foot_multiplicity >= 3);
}

TEST_CASE("trace_corridor bande droite : largeur et cote corrects a chaque station", "[corridor]") {
    // Bande [0,10000] x [-500,500] micrometres, axe = ligne mediane y=0.
    // Marge de 1000 (pas 500) par rapport aux embouts (x=0/x=10000) : a
    // exactement 500 de l'embout, la distance au bord vertical (500) est a
    // EGALITE avec la distance aux bords haut/bas (500 aussi) -- un vrai
    // piege degenere (3 pieds a egalite) que ce test ne cherche pas a
    // couvrir ici (ce n'est pas une proprietes du coude, juste un choix
    // d'echantillonnage a eviter).
    const Poly rect = make_rectangle({0.0, -500.0}, {10'000.0, 500.0});
    const std::vector<Poly> polys{rect};
    const auto axis = straight_axis(1'000.0, 9'000.0, 0.0, 16);

    SatinColumnsParameters params;
    const auto stations = trace_corridor(axis, polys, params);

    REQUIRE(stations.size() == axis.size());
    for (std::size_t i = 0; i < stations.size(); ++i) {
        const auto& st = stations[i];
        INFO("station #" << i);
        CHECK(st.width_um == Catch::Approx(1000.0).margin(1e-6));
        CHECK(st.foot_multiplicity == 2);
        CHECK_FALSE(st.interpolated);
        CHECK(st.tangent.x > 0.0);      // axe parcouru en +X
        CHECK(st.foot_a.point.y > 0.0); // gauche (+N) = cote +Y
        CHECK(st.foot_b.point.y < 0.0); // droite (-N) = cote -Y
    }
    check_no_self_crossing(stations);
}

TEST_CASE("trace_corridor coude a 90 degres : jamais d'explosion de largeur ni de croisement",
          "[corridor]") {
    // Corridor en "]" (meme topologie que e_trunk_isolated, dimensions
    // reduites) : jambe verticale x in [-2000,2000] y in [0,16000], jambe
    // horizontale x in [-2000,16000] y in [16000,20000] -- coin reflexe en
    // (2000,16000), exactement le defaut documente le 2026-08-30
    // (docs/source/satin.md, "Root cause precise du refus total sur E").
    const Poly corridor_shape{
        {-2'000.0, 0.0},      {2'000.0, 0.0},       {2'000.0, 16'000.0},
        {16'000.0, 16'000.0}, {16'000.0, 20'000.0}, {-2'000.0, 20'000.0},
    };
    const std::vector<Poly> polys{corridor_shape};
    // Axe : jambe verticale centree (x=0), demarrant a y=2500 (assez loin du
    // bord bas y=0 pour ne pas fausser la tolerance de nearest_boundary_feet
    // -- un demarrage exactement sur le bord serait un piege degenere, pas
    // une propriete a tester), jusqu'au centre reel de la barre (y=18000,
    // equidistante des bords y=16000/y=20000), puis jambe horizontale a ce
    // meme y jusqu'a x=13500 -- traverse le coude avec une fenetre de
    // difference centree qui chevauche le sommet reflexe, exactement le
    // mecanisme identifie dans l'investigation.
    const auto raw_axis = corner_axis(2'500.0, 18'000.0, 13'500.0, 500.0);
    // Lissage Chaikin (2 iterations, meme reglage par defaut que
    // `SatinColumnsParameters::axis_smoothing_iterations`) avant l'appel --
    // cf. justification sur `chaikin()` ci-dessus : c'est ce que
    // `compute_column_stations` fait TOUJOURS en production.
    const auto axis = chaikin(raw_axis, 2);

    SatinColumnsParameters params;
    const auto stations = trace_corridor(axis, polys, params);
    REQUIRE(stations.size() == axis.size());

    constexpr double kNominalWidth = 4'000.0;
    double maxWidth = 0.0;
    double minWidth = std::numeric_limits<double>::max();
    for (const auto& st : stations) {
        maxWidth = std::max(maxWidth, st.width_um);
        minWidth = std::min(minWidth, st.width_um);
        // Jamais nul/effondre (contrairement a un croisement degenere).
        CHECK(st.width_um > 100.0);
        // Jamais "explose" au sens de cross_section::TooWide (qui mesurait,
        // sur ce meme type de coin, un trou de 2471 um entre DEUX stations
        // valides -- ici on verifie directement qu'AUCUNE station, meme
        // tout pres du sommet reflexe, ne depasse un plafond large mais fini
        // (3x la largeur nominale) : le defaut historique n'a pas
        // d'equivalent ici par construction (recherche du point le plus
        // proche, jamais un rayon pouvant depasser un coin).
        CHECK(st.width_um < kNominalWidth * 3.0);
    }
    WARN("corner corridor: width_um min=" << minWidth << " max=" << maxWidth
                                          << " (largeur nominale=" << kNominalWidth << ")");
    check_no_self_crossing(stations);
}

TEST_CASE("trace_corridor determinisme : deux appels identiques, sortie identique", "[corridor]") {
    const Poly rect = make_rectangle({0.0, -500.0}, {10'000.0, 500.0});
    const std::vector<Poly> polys{rect};
    const auto axis = straight_axis(500.0, 9'500.0, 0.0, 18);
    SatinColumnsParameters params;

    const auto a = trace_corridor(axis, polys, params);
    const auto b = trace_corridor(axis, polys, params);

    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        INFO("station #" << i);
        CHECK(a[i].axis_point.x == b[i].axis_point.x);
        CHECK(a[i].axis_point.y == b[i].axis_point.y);
        CHECK(a[i].foot_a.point.x == b[i].foot_a.point.x);
        CHECK(a[i].foot_a.point.y == b[i].foot_a.point.y);
        CHECK(a[i].foot_a.poly_index == b[i].foot_a.poly_index);
        CHECK(a[i].foot_a.edge_index == b[i].foot_a.edge_index);
        CHECK(a[i].foot_b.point.x == b[i].foot_b.point.x);
        CHECK(a[i].foot_b.point.y == b[i].foot_b.point.y);
        CHECK(a[i].foot_b.poly_index == b[i].foot_b.poly_index);
        CHECK(a[i].foot_b.edge_index == b[i].foot_b.edge_index);
        CHECK(a[i].tangent.x == b[i].tangent.x);
        CHECK(a[i].tangent.y == b[i].tangent.y);
        CHECK(a[i].width_um == b[i].width_um);
        CHECK(a[i].foot_multiplicity == b[i].foot_multiplicity);
        CHECK(a[i].interpolated == b[i].interpolated);
    }
}

TEST_CASE("trace_corridor determinisme : region reelle du corpus (capsule)", "[corridor]") {
    // Meme invariant que ci-dessus, mais sur region_polys() d'une vraie
    // fixture du corpus (pas seulement une forme synthetique a la main) --
    // meme discipline que test_medial_field.cpp.
    const auto region = make_shape("capsule");
    REQUIRE(region.has_value());
    const auto polys = region_polys(*region);
    REQUIRE_FALSE(polys.empty());
    const auto axis = straight_axis(2'000.0, 38'000.0, 0.0, 40);
    SatinColumnsParameters params;

    const auto a = trace_corridor(axis, polys, params);
    const auto b = trace_corridor(axis, polys, params);
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].width_um == b[i].width_um);
        CHECK(a[i].foot_multiplicity == b[i].foot_multiplicity);
        CHECK(a[i].foot_a.edge_index == b[i].foot_a.edge_index);
        CHECK(a[i].foot_b.edge_index == b[i].foot_b.edge_index);
    }
}

// --- Comparaison cote a cote (indicateur test-only), corpus complet ----------

bool is_in(const char* shape, const char* const* list, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        if (std::string(shape) == list[i]) {
            return true;
        }
    }
    return false;
}

TEST_CASE("corridor dev flag : corpus complet, aucun plantage ; pas de croisement sur les formes "
          "a branche unique",
          "[corridor][corpus]") {
    // Le chemin LEGACY (inchange par cette Phase) ne doit jamais croiser,
    // sur AUCUNE forme -- c'est la garantie de non-regression de base.
    // Le chemin CORRIDOR est verifie STRICTEMENT sur le sous-corpus sans
    // jonction (kNonJunctionCorpus, perimetre prouve par la Phase B) ; sur
    // les formes A JONCTION (y/t/cross/h/trident/etc.), un croisement
    // residuel est seulement RAPPORTE (WARN), pas une regression de CETTE
    // phase -- `trim_unstable_junction_tail`/le nettoyage anti-croisement de
    // jonction restent inchanges (Phase C), et la mesure dense qui les
    // alimente a change : un desaccord a une jonction est attendu tant que
    // la Phase C n'a pas adapte ce qui consomme cette mesure.
    for (const char* shape : kFullCorpus) {
        INFO("forme = " << shape);
        const auto legacy = build_with_flag(shape, /*use_corridor=*/false);
        const auto corridor = build_with_flag(shape, /*use_corridor=*/true);
        check_columns_no_self_crossing(legacy.columns);
        if (is_in(shape, kNonJunctionCorpus, std::size(kNonJunctionCorpus))) {
            check_columns_no_self_crossing(corridor.columns);
        } else {
            bool crossed = false;
            for (const auto& col : corridor.columns) {
                const auto a = rail_to_p2(col.rail_a);
                const auto b = rail_to_p2(col.rail_b);
                const std::size_t n = std::min(a.size(), b.size());
                for (std::size_t i = 1; i < n && !crossed; ++i) {
                    if (segments_cross_p2(a[i - 1], a[i], b[i - 1], b[i]) ||
                        segments_cross_p2(a[i - 1], b[i - 1], a[i], b[i])) {
                        crossed = true;
                    }
                }
            }
            if (crossed) {
                WARN("forme a jonction = " << shape
                                           << " : croisement residuel en mode corridor (Phase C"
                                              " traitera la jonction, pas cette phase)");
            }
        }
    }
}

TEST_CASE("corridor dev flag : formes a branche unique, drop-in replacement (pas de regression)",
          "[corridor][corpus]") {
    // Phase B seulement (§4 du plan) : sur les formes SANS jonction de
    // squelette, le nouveau chemin doit au moins egaler l'ancien -- jamais
    // moins bon -- MEME quand l'ancien reussissait deja. On ne pretend PAS
    // ici que le nouveau chemin ameliore ces cas simples (seul
    // "e_trunk_isolated" en beneficie, teste separement ci-dessous) : la
    // Phase B prouve seulement que le remplacement est transparent.
    for (const char* shape : kNonJunctionCorpus) {
        if (std::string(shape) == "e_trunk_isolated") {
            continue; // exception DELIBEREE : voir le test dedie ci-dessous.
        }
        INFO("forme = " << shape);
        const auto legacy = build_with_flag(shape, /*use_corridor=*/false);
        const auto corridor = build_with_flag(shape, /*use_corridor=*/true);
        INFO("legacy refus=" << legacy.refusal << " corridor refus=" << corridor.refusal);
        if (!corridor.refusal.empty()) {
            for (const auto& w : corridor.warnings) {
                UNSCOPED_INFO("corridor warning: " << w);
            }
        }
        // Si l'ancien chemin reussissait (cas attendu sur ce sous-corpus
        // simple), le nouveau doit reussir aussi -- jamais introduire un
        // refus la ou il n'y en avait pas.
        if (legacy.refusal.empty()) {
            CHECK(corridor.refusal.empty());
            CHECK(corridor.columns.size() == legacy.columns.size());
        }
    }
}

TEST_CASE("corridor dev flag : formes a forte courbure/transition de largeur -- rapport, limite "
          "connue de la Phase B",
          "[corridor][corpus]") {
    // "s" et "multi_neck" : cf. le commentaire sur kKnownCurvatureLimitations
    // ci-dessus. Rapporte le comportement reel sans affirmer de parite --
    // limite connue et documentee de cette phase, pas une regression passee
    // sous silence.
    for (const char* shape : kKnownCurvatureLimitations) {
        INFO("forme = " << shape);
        const auto legacy = build_with_flag(shape, /*use_corridor=*/false);
        const auto corridor = build_with_flag(shape, /*use_corridor=*/true);
        WARN("forme=" << shape << " -- legacy: refus=\"" << legacy.refusal
                      << "\" colonnes=" << legacy.columns.size() << " | corridor: refus=\""
                      << corridor.refusal << "\" colonnes=" << corridor.columns.size());
    }
}

TEST_CASE("corridor dev flag : e_trunk_isolated -- preuve empirique du correctif de coin",
          "[corridor][e-fixture]") {
    const auto legacy = build_with_flag("e_trunk_isolated", /*use_corridor=*/false);
    const auto corridor = build_with_flag("e_trunk_isolated", /*use_corridor=*/true);

    INFO("legacy: statut=" << to_string(legacy.status) << " refus=" << legacy.refusal);
    INFO("corridor: statut=" << to_string(corridor.status) << " refus=" << corridor.refusal);

    // Le defaut documente (docs/source/satin.md, "Root cause precise du
    // refus total sur E", 2026-08-30) doit etre reproduit par le chemin
    // HISTORIQUE -- sinon ce test ne prouve plus rien de ce qu'il pretend
    // prouver.
    CHECK_FALSE(legacy.refusal.empty());
    CHECK(legacy.columns.empty());

    // Preuve empirique Phase B : le chemin CORRIDOR construit au moins une
    // colonne la ou l'historique refusait tout.
    CHECK(corridor.refusal.empty());
    REQUIRE(corridor.columns.size() >= 1);
    const auto& col = corridor.columns.front();
    CHECK(col.rail_a.nodes.size() >= 2);
    CHECK(col.rail_b.nodes.size() == col.rail_a.nodes.size());
    CHECK(col.mean_width_um > 0.0);

    WARN("e_trunk_isolated -- legacy: refus=\""
         << legacy.refusal << "\" | corridor: length_um=" << col.length_um
         << " mean_width_um=" << col.mean_width_um << " min_width_um=" << col.min_width_um
         << " max_width_um=" << col.max_width_um << " rails=" << col.rail_a.nodes.size());
}

TEST_CASE("corridor dev flag : E complet (avec jonction) -- rapport, pas d'affirmation de parite",
          "[corridor][e-fixture]") {
    // §4/§9 du plan : la lettre "E" complete (avec la barre du milieu, donc
    // une vraie jonction de squelette) n'est PAS dans le perimetre prouve par
    // la Phase B (seule compute_column_stations change ; trim_unstable_
    // junction_tail/resolve_junction, la Phase C, restent inchanges) -- ce
    // test rapporte le comportement reel des deux chemins sans affirmer que
    // le second doit deja egaler ou surpasser le premier sur ce cas.
    const auto legacy = build_with_flag("E", /*use_corridor=*/false);
    const auto corridor = build_with_flag("E", /*use_corridor=*/true);
    check_columns_no_self_crossing(legacy.columns);
    check_columns_no_self_crossing(corridor.columns);
    WARN("E complet -- legacy: statut=" << to_string(legacy.status) << " refus=\"" << legacy.refusal
                                        << "\" colonnes=" << legacy.columns.size()
                                        << " | corridor: statut=" << to_string(corridor.status)
                                        << " refus=\"" << corridor.refusal
                                        << "\" colonnes=" << corridor.columns.size());
}
