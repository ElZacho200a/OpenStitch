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
//
// § HP-STI-018 Phase C (meme plan, §2.2/§4/§5 corrige) : a partir de
// "find_stable_corridor_end_index", plus bas dans ce fichier --
// `find_stable_corridor_end`/`CorridorEnd`, remplacement de
// `trim_unstable_junction_tail` par le critere de multiplicite dans
// `resolve_junction`, et verification explicite du mode Parametric (§2.6).
//
// § HP-STI-018 Phase B.5 (meme plan, §4 "Phase B.5 (added after Phase B
// review...)") : gathering DIRECTION-AWARE de `nearest_boundary_feet`
// (nouvelle primitive `nearest_boundary_feet_oriented`, medial_field.hpp) --
// corrige le defaut identifie en cloture de Phase C ("s"/"multi_neck"/"y"/
// "y_symmetric"/"trident" tombaient tous sous le MEME mecanisme : gathering
// aveugle a la direction, les deux candidats globalement les plus proches
// pouvant atterrir du MEME cote physique, affamant l'autre cote). Resultat
// empirique, honnete, PAS force a "tout passe" :
//   - "s" : CORRIGE -- deplacee de `kKnownCurvatureLimitations` vers
//     `kNonJunctionCorpus` ci-dessous, asseree au meme titre que le reste de
//     ce sous-corpus (parite stricte avec le chemin historique).
//   - "multi_neck" : PAS corrige, mais pour une cause racine DIFFERENTE de
//     celle visee par cette phase (confirme en isolant le probleme station
//     par station, cf. commentaire sur `kKnownCurvatureLimitations`
//     ci-dessous) -- reste dans `kKnownCurvatureLimitations`.
//   - "y"/"y_symmetric" : corriges ensuite par Phase B.5b (`extend_tip`
//     direction-aware) ; "trident" reste rapporte separement, cf. plus bas.
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
// EXCLUE deliberement malgre l'absence de jonction : "multi_neck" ("0
// jonction, 1 seul arc de squelette" confirme par l'investigation du
// 2026-08-30, mais transitions de largeur fine/large tres marquees aux
// etranglements, deja documentees comme leur PROPRE cause racine distincte
// dans docs/source/satin.md).
//
// "s" (courbe S a forte courbure) a REJOINT ce sous-corpus en Phase B.5
// (§4/§Phase B.5 du plan) : la gathering direction-aware
// (`nearest_boundary_feet_oriented`, medial_field.hpp) corrige exactement le
// mecanisme qui la faisait echouer (les deux candidats globalement les plus
// proches tombaient du meme cote physique a forte courbure, affamant l'autre
// cote) -- verifie empiriquement, "s" egale desormais le chemin historique
// (meme nombre de colonnes, aucun refus introduit), cf. le test "formes a
// branche unique, drop-in replacement" ci-dessous qui l'assert desormais au
// meme titre que le reste de ce sous-corpus.
//
// "multi_neck" reste exclue -- Phase B.5 a identifie, en isolant la station
// precise qui echoue (axe reechantillonne, premier echantillon EXACTEMENT au
// centre geometrique du premier cercle du corpus, largeur mesuree ~12,0 mm,
// a comparer a la largeur reelle du col voisin ~1,2 mm 500 um plus loin), que
// sa cause racine est DIFFERENTE de celle visee par cette phase : ce n'est
// PAS un probleme de famine d'un cote (les deux cotes trouvent chacun un
// candidat, independamment, exactement comme concu) mais un probleme plus
// profond -- "le plus proche point dans un demi-plan" n'est pas la meme
// chose que "le bon point de rail perpendiculaire au corridor" quand
// l'echantillon d'axe se trouve, par construction du squelette amincil, au
// centre d'une region localement large/ronde (le moyeu d'un cercle de 12 mm
// de diametre) plutot que dans un vrai corridor etroit 1D. Le point "le plus
// proche en direction +N" y est alors un point presque EN FACE (vers la
// branche voisine du meme polygone), pas un point lateral au sens d'un rail
// -- confirme directement (test unitaire ad hoc sur
// `nearest_boundary_feet_oriented` au point degenere, pas conserve ici) : la
// gathering fonctionne exactement comme concue, mais le concept "pied le
// plus proche par demi-plan" lui-meme ne s'applique pas a ce point d'axe
// precis. Resoudre ceci proprement ressemble au probleme que `IsoOffsetRing`
// (§2.3 du plan, Phase D, explicitement hors perimetre de cette phase) est
// cense couvrir (axe median degenere sur une forme large/ronde) -- PAS un
// reglage supplementaire de tolerance sur la gathering, qui serait a nouveau
// le patch opportuniste sous pression d'une seule fixture que ce depot
// proscrit (§21/§22, docs/source/satin.md). Rapportee (WARN) mais pas
// corrigee ici, en toute honnetete : la gathering direction-aware ETAIT la
// bonne premiere piste (elle a corrige "s"), mais ne suffit pas seule pour
// "multi_neck".
//
// Tente d'abord (puis REVERTE, cf. historique git de ce lot) : faire subir
// le meme traitement demi-plan a `extend_tip` (sonde de largeur par pas,
// satin_column.cpp) pour "y"/"y_symmetric". Phase B.5b corrige finalement ce
// cas avec une requete plus stricte par cone angulaire, cf. plus bas.
constexpr const char* kNonJunctionCorpus[] = {
    "rectangle",
    "capsule",
    "ribbon",
    "s",
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

// Forme a branche unique mais EXCLUE de kNonJunctionCorpus (cf. ci-dessus) :
// rapportee separement, jamais asseree egale au chemin historique.
constexpr const char* kKnownCurvatureLimitations[] = {"multi_neck"};

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
          "connue (Phase B.5)",
          "[corridor][corpus]") {
    // "multi_neck" seule desormais ("s" a rejoint kNonJunctionCorpus en
    // Phase B.5, corrigee par la gathering direction-aware) : cf. le
    // commentaire sur kKnownCurvatureLimitations ci-dessus pour la cause
    // racine distincte identifiee. Rapporte le comportement reel sans
    // affirmer de parite -- limite connue et documentee, pas une regression
    // passee sous silence.
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

// =============================================================================
// § HP-STI-018 Phase C (specs/plans/hp-sti-018-turning-satin.md §2.2/§4/§5) :
// `find_stable_corridor_end`/`find_stable_corridor_end_index`, remplacement de
// `trim_unstable_junction_tail` par le critere de multiplicite, et verification
// explicite du mode Parametric (§2.6, correction de revue B4).
//
// Resultats empiriques etablis en construisant ces tests (voir le rapport de
// livraison pour le detail) :
//  - `junction_stability_margin_stations` par defaut est 6, PAS 3 (le defaut
//    suggere par le plan initial) -- deviation documentee sur
//    `SatinColumnsParameters::junction_stability_margin_stations`
//    (satin_column.hpp) : a margin=3, la fixture "h" (pont jonction-jonction)
//    choisit une StableCorridorEnd encore dans la zone de virage du squelette
//    pres du noeud (tangente locale diagonale), produisant un `JunctionCore`
//    auto-croise -- teste empiriquement a 3/6/10/15/20/30, 6 est la plus
//    petite valeur qui passe.
//  - "y"/"trident" echouent sous le chemin corridor (Legacy ET Parametric)
//    pour une raison ANTERIEURE a cette phase, confirmee empiriquement en
//    rejouant le MEME echec avec `trim_unstable_junction_tail` (ancien,
//    largeur) a la place du critere de multiplicite : la branche en pointe
//    de "trident" (triangle effile) et une branche de "y" produisent un creux
//    de largeur (`TooNarrow`)/un croisement de barreaux deja present dans la
//    mesure dense (`trace_corridor`/`select_feet`) elle-meme, en amont de tout
//    critere de troncature -- cf. le risque deja documente par la Phase B
//    ("foot-selection flicker at near-degenerate corners", §3 du plan) sur
//    courbure/transition de largeur forte. PAS une regression de cette phase
//    (Phase B.5, deja trackee, en est le bon perimetre) : ces deux formes
//    rejoignent `kKnownCurvatureLimitations` ci-dessus plutot que d'etre
//    forcees a passer par un ajustement de la Phase C qui ne serait pas le
//    bon niveau pour les corriger. CORRECTIF (revue, cette prose etait deja
//    perimee avant meme la suite Phase B.5 ci-dessous) : "y"/"trident" ne
//    rejoignent PAS `kKnownCurvatureLimitations` (qui ne contient que
//    "multi_neck") -- elles vivent dans `kKnownAsymmetricJunctionLimitations`,
//    un tableau distinct declare plus bas, cf. la suite Phase B.5 juste
//    apres pour l'etat reel (post-gathering direction-aware) de chacune.
//  - la boucle de retrait iteratif de `resolve_junction` (ancien mecanisme
//    PRINCIPAL contre la contamination par une branche voisine) n'a ete
//    declenchee NULLE PART sur le corpus complet sous le nouveau critere de
//    multiplicite (margin=6) -- cf. le test dedie plus bas. Gardee comme filet
//    defensif inerte (cout nul tant qu'elle ne se declenche pas) -- MAIS cf.
//    la suite Phase B.5 : "trident" est precisement la forme qui exercerait
//    ce filet, et elle n'atteint toujours pas resolve_junction aujourd'hui --
//    ce zero reste une preuve d'absence de besoin uniquement pour les formes
//    testables aujourd'hui, pas une preuve generale.

// =============================================================================
// § HP-STI-018 Phase B.5, suite -- "y"/"y_symmetric"/"trident" apres la
// gathering direction-aware (nearest_boundary_feet_oriented, medial_field.hpp) :
//
// Toujours PAS corrigees, mais pour DEUX mecanismes distincts, aucun des deux
// n'etant la famine d'un cote par gathering aveugle que cette phase corrige
// (confirme independamment pour chacune, pas suppose) :
//
//  - "y"/"y_symmetric" : le refus restant ("croisement entre barreaux #0/1")
//    vient ENTIEREMENT de `extend_tip` (satin_column.cpp, extension du bout
//    OUVERT), PAS de `trace_corridor` -- confirme en isolant
//    `extend_open_ends=false` : les deux formes construisent alors une
//    colonne complete pour les 3 branches (jonction correctement resolue,
//    seul un diagnostic "zone centrale significative -- necessite un objet
//    de remplissage separe" subsiste, attendu et documente pour une jonction
//    a 3 branches de meme largeur). `extend_tip` appelle encore
//    `cross_section` meme sous le chemin corridor (jamais bascule vers
//    `nearest_boundary_feet`, contrairement a ce que le plan §2.2 anticipe
//    pour une phase ulterieure) -- une tentative de bascule a ete faite puis
//    REVERTEE dans ce meme lot : `nearest_boundary_feet_oriented` applique
//    au pas-a-pas de `extend_tip` choisit systematiquement le mur d'EMBOUT
//    (le bout ouvert lui-meme, tres proche par construction de la marche)
//    comme "pied le plus proche du cote +N/-N" plutot que le vrai mur
//    LATERAL de la branche -- regression reproductible et confirmee sur
//    "rectangle"/"notch"/"t" (formes simples, jusque-la saines), revertee
//    immediatement plutot que risquee. Corriger `extend_tip` proprement
//    demanderait une restriction angulaire supplementaire (pas seulement un
//    demi-plan) que cette phase n'a pas le mandat d'inventer sous pression
//    d'une seule paire de fixtures (meme risque de patch opportuniste que
//    documente plus haut) -- une dette reelle, pas cachee, a reprendre
//    specifiquement sur `extend_tip` avant la Phase F.
//  - "trident" : le refus restant ("trou de 8690 um entre stations axe #0 et
//    #19, largeur inferieure a min_satin_width") vient de `trace_corridor`
//    lui-meme, sur la branche interne pointue (triangle effile, shapes.cpp)
//    -- mais PAS par famine d'un cote : `foot_multiplicity` y vaut 1 (un
//    seul pied exploitable, pas deux insuffisamment separes), signe d'un
//    point d'axe ou les deux cotes du triangle convergent deja presque au
//    meme point de contour (la pointe elle-meme). La gathering
//    direction-aware ne peut rien y faire par construction : si le cote
//    droit et le cote gauche du contour sont reellement, geometriquement,
//    le MEME point a cet endroit (la pointe d'un triangle effile), aucune
//    independance de collecte par cote ne fait apparaitre un second point
//    qui n'existe pas. Un gain reel est neanmoins mesure (rapporte
//    honnetement, pas cache) : la jonction de "trident" passe de 1/3 a 2/3
//    branches disponibles sous ce lot (la branche laterale etroite, qui
//    echouait aussi en Phase C, reussit desormais) -- seule la branche en
//    pointe reste bloquee, par le mecanisme "tres fin par nature", pas par
//    le mecanisme que cette phase corrige.
//
// Net : la gathering direction-aware a corrige exactement ce qu'elle visait
// (demonstre sur "s" et sur la branche laterale de "trident"), mais
// "y"/"y_symmetric"/"trident" restent bloquees par deux mecanismes
// DIFFERENTS et PRE-EXISTANTS (respectivement : `extend_tip` n'a jamais ete
// migre vers `nearest_boundary_feet`, et une pointe effilee genuinement
// degeneree n'a qu'un seul pied exploitable par construction) -- ni l'un ni
// l'autre n'est "le meme bug pas encore completement corrige", ce sont deux
// chantiers distincts, correctement non entrepris ici (hors mandat de cette
// phase, cf. discipline de perimetre du lot).
//
// CORRECTIF Phase B.5b (2026-10) : le premier chantier ci-dessus est termine
// pour "y"/"y_symmetric". `extend_tip` garde un repli `cross_section`, mais
// tente d'abord une sonde laterale a cone angulaire et refuse localement une
// station de fermeture qui croiserait la precedente. "trident" reste la seule
// limitation rapportee ici, pour la pointe effilee distincte.
// =============================================================================

namespace {

// Formes a jonction qui REUSSISSENT sous le chemin corridor (Phase C,
// margin=6) -- perimetre etabli empiriquement en ecrivant ce lot, PAS
// suppose a priori (cf. commentaire ci-dessus). "t"/"cross" : jonctions a
// largeurs egales (rien a amputer, cf. test_columns.cpp "parametrique :
// recouvrement de jonction"). "h" : topologie jonction-jonction (le pont),
// le cas precis qui a motive margin=6 ci-dessus.
constexpr const char* kJunctionSuccessCorpus[] = {"t", "cross", "h"};

// Formes a jonction asymetrique/forte courbure qui echouent encore --
// RAPPORTEES, jamais asserees : deux limitations DISTINCTES, ni l'une ni
// l'autre n'etant la famine par gathering aveugle que la Phase B.5 corrige
// -- cf. le bloc de commentaire Phase B.5 ci-dessus pour le detail par forme.
constexpr const char* kKnownAsymmetricJunctionLimitations[] = {"trident"};

} // namespace

TEST_CASE("find_stable_corridor_end_index : critere de marge sur sequence de multiplicite "
          "synthetique",
          "[corridor][junction]") {
    // Teste l'algorithme directement, sans passer par trace_corridor/une
    // forme reelle -- sequences de foot_multiplicity choisies a la main.
    SatinColumnsParameters params;
    params.junction_stability_margin_stations = 2; // marge reduite : sequences courtes, lisibles.

    auto make_stations = [](std::initializer_list<int> mults) {
        std::vector<CorridorStation> st;
        for (int m : mults) {
            CorridorStation s;
            s.foot_multiplicity = m;
            st.push_back(s);
        }
        return st;
    };

    // atEnd=false (depuis l'avant) : indices 0..1 contamines (3,3), stable a
    // partir de l'indice 2 (2,2,2,2 -- au moins 2+marge(2)=3 valeurs a 2
    // consecutives a partir de la).
    {
        const auto st = make_stations({3, 3, 2, 2, 2, 2});
        CHECK(find_stable_corridor_end_index(st, /*atEnd=*/false, params) == 2);
    }
    // atEnd=true (depuis l'arriere) : miroir exact.
    {
        const auto st = make_stations({2, 2, 2, 2, 3, 3});
        CHECK(find_stable_corridor_end_index(st, /*atEnd=*/true, params) == 3);
    }
    // Un "2" ISOLE au milieu d'une zone contaminee ne doit PAS etre retenu
    // (la marge de 2 stations suivantes echoue : la 2e est un "3") -- la
    // marche continue jusqu'au VRAI plateau stable, plus loin.
    {
        const auto st = make_stations({3, 2, 3, 3, 2, 2, 2, 2});
        CHECK(find_stable_corridor_end_index(st, /*atEnd=*/false, params) == 4);
    }
    // Degenere : jamais assez de plateau stable (toujours contamine, ou trop
    // court pour tester la marge) -- repli conservateur (aucune troncature),
    // jamais un index qui viderait la sequence.
    {
        const auto st = make_stations({3, 3, 3, 3});
        CHECK(find_stable_corridor_end_index(st, /*atEnd=*/false, params) == 0);
        CHECK(find_stable_corridor_end_index(st, /*atEnd=*/true, params) == st.size() - 1);
    }
    // Vecteur vide : jamais de dereferencement hors bornes.
    {
        const std::vector<CorridorStation> empty;
        CHECK(find_stable_corridor_end_index(empty, false, params) == 0);
        CHECK(find_stable_corridor_end_index(empty, true, params) == 0);
        const auto end = find_stable_corridor_end(empty, false, params);
        CHECK(end.at_end == false);
        CHECK(end.edge_id == 0);
    }
}

namespace {

// Trace brute (SANS lissage Chaikin/resample_arc -- non necessaire pour une
// verification purement structurelle sur la geometrie reelle d'une arete de
// squelette) d'une arete de `SkeletonGraph` -- reutilise directement pour
// appeler `trace_corridor`/`find_stable_corridor_end` SANS dependre de
// `build_satin_columns` (qui echoue encore sur "trident", cf. commentaire
// plus haut, pour une raison anterieure a cette phase et independante de
// `find_stable_corridor_end` lui-meme).
std::vector<P2> edge_axis(const SkeletonEdge& e) {
    std::vector<P2> axis;
    axis.reserve(e.centerline.size());
    for (const auto& v : e.centerline) {
        axis.push_back({static_cast<double>(v.x.value), static_cast<double>(v.y.value)});
    }
    return axis;
}

} // namespace

TEST_CASE("find_stable_corridor_end : trident, verification empirique de l'invariant historique "
          "StableBranchEnd (satin.md:690-703) sur la geometrie reelle",
          "[corridor][junction]") {
    // "trident" (shapes.cpp) est la fixture qui reproduit REELLEMENT
    // aujourd'hui la jonction tres asymetrique citee par corridor.hpp/
    // satin.md:690-703 (grande branche verticale 6 mm vs branche laterale
    // 1,2 mm) -- cf. le commentaire en tete de ce bloc : la citation
    // litterale du plan ("t") designe une fixture dont les deux bras sont
    // aujourd'hui de MEME largeur (shapes.cpp:159-164, verifie directement),
    // donc ne stresse plus ce bug precis.
    //
    // RESULTAT EMPIRIQUE (pas celui espere, rapporte honnetement plutot que
    // force) : sur la geometrie REELLE de "trident", la StableCorridorEnd de
    // la branche fine, choisie par le seul critere de multiplicite
    // (margin=6), CROISE ENCORE le rail de la branche large a une station
    // interne (cf. le WARN plus bas pour l'index exact) -- `find_stable_
    // corridor_end` seul ne suffit donc PAS a reproduire completement
    // l'ancienne garantie de `resolve_junction`'s retraction loop sur cette
    // asymetrie extreme precise. Ceci est COHERENT avec deux faits deja
    // etablis ailleurs dans ce lot : (1) "trident" echoue de toute facon a
    // construire sa colonne complete sous le chemin corridor, pour une raison
    // ANTERIEURE a cette phase (creux de largeur dans `trace_corridor` sur la
    // branche en pointe, confirme identique sous l'ancien `trim_unstable_
    // junction_tail`, cf. le commentaire en tete de bloc) -- `resolve_junction`
    // n'est donc jamais atteint sur cette forme, et cette croisee ne s'est
    // jamais manifestee en pratique dans aucun test de ce lot ; (2) le risque
    // "foot-selection flicker at near-degenerate corners" est deja documente
    // par le plan (§3) comme un risque CONNU de cette conception, pas une
    // garantie absolue. Rapporte ici en PREMIER TEST DIRECT sur la primitive
    // elle-meme (independamment du pipeline complet, qui echoue pour une
    // autre raison en amont) : une limitation reelle a tracker, pas une
    // regression silencieuse.
    const auto region = make_shape("trident");
    REQUIRE(region.has_value());
    const auto polys = region_polys(*region);
    REQUIRE_FALSE(polys.empty());

    SatinColumnsParameters params;
    AutoSatinParameters analysisParams;
    analysisParams.raster.pixel_size = Micrometers{100};
    const auto analysis = analyze_region(*region, analysisParams);
    REQUIRE(analysis.has_value());
    const auto& graph = analysis->debug.graph;
    REQUIRE(graph.edges.size() >= 3);

    // Largeur moyenne par arete (mesuree directement par trace_corridor, sur
    // l'axe BRUT) pour identifier la branche la plus LARGE (verticale, 6 mm)
    // et la plus FINE (laterale, 1,2 mm) sans dependre de l'ordre/l'id des
    // aretes du graphe.
    struct EdgeTrace {
        std::vector<CorridorStation> stations;
        double meanWidth{0.0};
    };
    std::vector<EdgeTrace> traces;
    traces.reserve(graph.edges.size());
    for (const auto& e : graph.edges) {
        const auto axis = edge_axis(e);
        if (axis.size() < 2) {
            continue;
        }
        auto stations = trace_corridor(axis, polys, params);
        double sum = 0.0;
        for (const auto& s : stations) {
            sum += s.width_um;
        }
        const double meanWidth = sum / static_cast<double>(stations.size());
        traces.push_back({std::move(stations), meanWidth});
    }
    REQUIRE(traces.size() >= 3);

    std::size_t wideIdx = 0, thinIdx = 0;
    for (std::size_t i = 1; i < traces.size(); ++i) {
        if (traces[i].meanWidth > traces[wideIdx].meanWidth) {
            wideIdx = i;
        }
        if (traces[i].meanWidth < traces[thinIdx].meanWidth) {
            thinIdx = i;
        }
    }
    REQUIRE(wideIdx != thinIdx);
    INFO("largeur moyenne : large=" << traces[wideIdx].meanWidth
                                    << " fine=" << traces[thinIdx].meanWidth);
    CHECK(traces[wideIdx].meanWidth > traces[thinIdx].meanWidth * 2.0); // ecart net, pas du bruit.

    // SEUL le bout qui touche reellement la jonction est concerne par
    // l'invariant historique (satin.md:690-703) -- le bout OUVERT distant
    // (ex. la pointe du triangle effile) n'a jamais ete cense rester hors du
    // corridor de la branche voisine, il peut legitimement s'en approcher ou
    // le croiser selon la forme (ce n'est pas ce que `find_stable_corridor_end`
    // garantit : seul un bout de JONCTION, cf. corridor.hpp). Identifie via le
    // graphe (meme correspondance d'indice edges<->traces que la boucle de
    // construction ci-dessus : aucune arete n'a ete sautee sur "trident", les
    // trois ont une centerline >= 2 points).
    REQUIRE(graph.edges.size() == traces.size());
    const auto& thinEdge = graph.edges[thinIdx];
    const bool thinEndIsJunction =
        graph.nodes[thinEdge.to].type == openstitch::auto_satin::SkeletonNodeType::Junction;
    const bool thinStartIsJunction =
        graph.nodes[thinEdge.from].type == openstitch::auto_satin::SkeletonNodeType::Junction;
    REQUIRE((thinEndIsJunction || thinStartIsJunction));
    const bool junctionAtEnd = thinEndIsJunction;

    const auto& wideStations = traces[wideIdx].stations;
    const auto end = find_stable_corridor_end(traces[thinIdx].stations, junctionAtEnd, params);
    std::size_t crossingCount = 0;
    for (std::size_t i = 1; i < wideStations.size(); ++i) {
        if (segments_cross_p2(end.station.foot_a.point, end.station.foot_b.point,
                              wideStations[i - 1].foot_a.point, wideStations[i].foot_a.point) ||
            segments_cross_p2(end.station.foot_a.point, end.station.foot_b.point,
                              wideStations[i - 1].foot_b.point, wideStations[i].foot_b.point)) {
            ++crossingCount;
        }
    }
    WARN("trident -- StableCorridorEnd de la branche fine (atEnd="
         << junctionAtEnd << ") croise le rail de la branche large sur " << crossingCount << "/"
         << wideStations.size() << " intervalles -- cf. commentaire du test (limitation connue, "
         << "pas une regression de cette phase)");
}

TEST_CASE("corridor dev flag Phase C : t/cross/h -- colonnes completes, JunctionCore simple et "
          "borne par le rayon local (pas de valeur mm2 figee)",
          "[corridor][junction]") {
    for (const char* shape : kJunctionSuccessCorpus) {
        INFO("forme = " << shape);
        const auto r = build_with_flag(shape, /*use_corridor=*/true);
        INFO("refus=\"" << r.refusal << "\"");
        REQUIRE(r.refusal.empty());
        REQUIRE_FALSE(r.columns.empty());
        check_columns_no_self_crossing(r.columns);

        REQUIRE_FALSE(r.junction_cores.empty());
        for (const auto& core : r.junction_cores) {
            INFO("junction_id=" << core.junction_id);
            // Simple par construction (resolve_junction refuse deja la
            // colonne entiere si le noyau s'auto-croise, cf.
            // polygon_self_intersects) -- revérifié ici en boite noire, sans
            // dependre de cette garantie interne.
            std::vector<P2> boundary;
            boundary.reserve(core.boundary.size());
            for (const auto& p : core.boundary) {
                boundary.push_back(
                    {static_cast<double>(p.x.value), static_cast<double>(p.y.value)});
            }
            const std::size_t m = boundary.size();
            bool crossed = false;
            for (std::size_t i = 0; i < m && !crossed; ++i) {
                for (std::size_t j = i + 2; j < m && !crossed; ++j) {
                    if (i == 0 && j + 1 == m) {
                        continue;
                    }
                    crossed = segments_cross_p2(boundary[i], boundary[(i + 1) % m], boundary[j],
                                                boundary[(j + 1) % m]);
                }
            }
            CHECK_FALSE(crossed);

            // Borne par le rayon local (pas une constante arbitraire, cf.
            // satin_column.cpp : `local_radius_um` <= `configured_radius_um`,
            // et le noyau reste proche de ce rayon -- marge de 1,5x pour le
            // repli "milieu d'arc" de `build_separator`, deja la tolerance
            // utilisee en interne).
            CHECK(core.local_radius_um > 0.0);
            CHECK(core.local_radius_um <= core.configured_radius_um + 1.0);
            CHECK(core.actual_max_radius_um <= core.local_radius_um * 1.5 + 1.0);
            CHECK(core.area_um2 >= 0.0);
        }
    }
}

TEST_CASE("corridor dev flag Phase C : trident -- limitation anterieure rapportee, "
          "pas une regression de cette phase",
          "[corridor][junction]") {
    for (const char* shape : kKnownAsymmetricJunctionLimitations) {
        INFO("forme = " << shape);
        const auto r = build_with_flag(shape, /*use_corridor=*/true);
        WARN("forme=" << shape << " -- corridor: statut=" << to_string(r.status) << " refus=\""
                      << r.refusal << "\" colonnes=" << r.columns.size());
        for (const auto& warning : r.warnings) {
            WARN("  " << warning);
        }
    }
}

TEST_CASE("corridor dev flag Phase B.5b : y/y_symmetric -- extend_tip ne croise plus les "
          "premiers barreaux",
          "[corridor][junction]") {
    for (const char* shape : {"y", "y_symmetric"}) {
        INFO("forme = " << shape);
        const auto r = build_with_flag(shape, /*use_corridor=*/true);
        INFO("statut=" << to_string(r.status) << " refus=\"" << r.refusal
                       << "\" colonnes=" << r.columns.size());
        for (const auto& warning : r.warnings) {
            INFO("  " << warning);
        }
        REQUIRE(r.refusal.empty());
        REQUIRE(r.columns.size() == 3);
        bool hasCoreFillWarning = false;
        for (const auto& warning : r.warnings) {
            CHECK(warning.find("croisement entre barreaux") == std::string::npos);
            hasCoreFillWarning =
                hasCoreFillWarning ||
                warning.find("zone centrale significative") != std::string::npos;
        }
        CHECK(hasCoreFillWarning);
    }
}

TEST_CASE("corridor dev flag Phase C : boucle de retrait iteratif jamais declenchee sur le corpus "
          "complet (decision empirique, repli defensif conserve)",
          "[corridor][corpus][junction]") {
    // §9.4 du plan ("decide after Phase C's empirical results") : la boucle
    // de retrait iteratif de `resolve_junction` (ancien mecanisme PRINCIPAL
    // contre la contamination par une branche voisine, cf. satin_column.cpp)
    // est TOUJOURS presente et active ; ce test verifie qu'elle ne se
    // declenche jamais sur les formes qui atteignent effectivement
    // `resolve_junction` aujourd'hui sous le nouveau critere de multiplicite.
    //
    // CORRECTIF (revue Phase C) -- NE PAS lire ce resultat comme "le critere
    // structurel suffit a lui seul, prouve" : "trident", la forme qui
    // reproduit historiquement le defaut que cette boucle corrige (jonction
    // asymetrique, cf. satin.md §636-725), n'atteint PAS resolve_junction
    // aujourd'hui -- elle echoue plus tot dans trace_corridor (Phase B.5,
    // trou de largeur sur la branche effilee). Teste directement sur la
    // geometrie brute de "trident" (hors pipeline complet), la StableCorridorEnd
    // choisie croise bel et bien le rail voisin a margin=6 (1/204 intervalles) --
    // la boucle SERAIT necessaire si "trident" atteignait ce code, ce zero
    // n'est donc une preuve d'absence de besoin QUE pour les formes testables
    // aujourd'hui, pas une preuve generale. Decision : GARDEE comme repli
    // defensif (cout nul tant qu'elle ne se declenche pas) -- mais sa
    // necessite reelle reste a reverifier une fois Phase B.5 corrigee et
    // "trident" de nouveau capable d'atteindre ce chemin.
    for (const char* shape : kFullCorpus) {
        INFO("forme = " << shape);
        const auto r = build_with_flag(shape, /*use_corridor=*/true);
        bool retracted = false;
        for (const auto& w : r.warnings) {
            if (w.find("repli de retrait iteratif") != std::string::npos) {
                retracted = true;
                UNSCOPED_INFO("  " << w);
            }
        }
        CHECK_FALSE(retracted);
    }
}

TEST_CASE("corridor dev flag Phase C : determinisme du pipeline complet sur t/cross/h "
          "(deux executions identiques)",
          "[corridor][junction]") {
    for (const char* shape : kJunctionSuccessCorpus) {
        INFO("forme = " << shape);
        const auto a = build_with_flag(shape, /*use_corridor=*/true);
        const auto b = build_with_flag(shape, /*use_corridor=*/true);
        REQUIRE(a.refusal.empty());
        REQUIRE(b.refusal.empty());
        REQUIRE(a.columns.size() == b.columns.size());
        for (std::size_t i = 0; i < a.columns.size(); ++i) {
            CHECK(a.columns[i].rail_a == b.columns[i].rail_a);
            CHECK(a.columns[i].rail_b == b.columns[i].rail_b);
        }
        REQUIRE(a.junction_cores.size() == b.junction_cores.size());
        for (std::size_t i = 0; i < a.junction_cores.size(); ++i) {
            CHECK(a.junction_cores[i].area_um2 == b.junction_cores[i].area_um2);
            CHECK(a.junction_cores[i].boundary == b.junction_cores[i].boundary);
        }
    }
}

// --- § mode Parametric (§2.6, correction de revue B4) -----------------------
//
// `compute_column_stations` est PARTAGEE entre Legacy et Parametric -- la
// correction de revue B4 exige de ne pas se contenter de "la suite de tests
// Parametric existante reste verte" : il faut verifier explicitement que
// `extend_into_confluence` (qui demarre desormais depuis la StableCorridorEnd
// choisie par multiplicite, au lieu de la queue amputee par
// `trim_unstable_junction_tail`) produit un recouvrement correct, pas
// seulement "ne plante pas".

TEST_CASE("corridor dev flag Phase C : mode Parametric, t/cross/h -- recouvrement de jonction "
          "correct depuis la nouvelle StableCorridorEnd",
          "[corridor][junction][parametric]") {
    for (const char* shape : kJunctionSuccessCorpus) {
        INFO("forme = " << shape);
        const auto region = make_shape(shape);
        REQUIRE(region.has_value());
        SatinColumnsParameters params;
        params.analysis.raster.pixel_size = Micrometers{100};
        params.use_corridor_tracing_dev_only = true;
        params.geometry_mode = SatinGeometryMode::Parametric;
        const auto r = build_satin_columns(*region, params);
        INFO("refus=\"" << r.refusal << "\"");
        REQUIRE(r.refusal.empty());
        REQUIRE_FALSE(r.parametric_columns.empty());

        SatinColumnsParameters defaults;
        for (const auto& obj : r.parametric_columns) {
            // Toujours un recouvrement PLAUSIBLE : jamais negatif, jamais au-
            // dela du plafond configure (§ etape 9, junction_overlap_max).
            CHECK(obj.start_overlap_um >= 0.0);
            CHECK(obj.end_overlap_um >= 0.0);
            CHECK(obj.start_overlap_um <=
                  static_cast<double>(defaults.junction_overlap_max.value) + 1.0);
            CHECK(obj.end_overlap_um <=
                  static_cast<double>(defaults.junction_overlap_max.value) + 1.0);
            CHECK(obj.rail_a.nodes.size() >= 2);
            CHECK(obj.rail_b.nodes.size() == obj.rail_a.nodes.size());
        }

        // "t"/"cross" (largeurs egales) : le recouvrement n'etait JAMAIS
        // applique avant cette phase (rien a amputer par l'ancien critere de
        // largeur, cf. test_columns.cpp "parametrique : recouvrement de
        // jonction borne") -- sous le critere de multiplicite, une queue de
        // jonction EST desormais amputee (la jonction elle-meme fait
        // toujours apparaitre un 3e pied a portee), donc un recouvrement
        // REEL doit maintenant apparaitre : verifie ici que ce changement de
        // comportement, attendu par §2.6, se produit reellement (pas
        // silencieusement absent).
        const std::string s = shape;
        if (s == "t" || s == "cross" || s == "h") {
            bool anyOverlap = false;
            for (const auto& obj : r.parametric_columns) {
                anyOverlap = anyOverlap || obj.start_overlap_um > 0.0 || obj.end_overlap_um > 0.0;
            }
            CHECK(anyOverlap);
        }
    }
}

TEST_CASE("corridor dev flag Phase C : mode Parametric, y/trident -- meme limitation anterieure "
          "qu'en mode Legacy (rapportee, pas une regression)",
          "[corridor][junction][parametric]") {
    // Confirme que le mode Parametric partage bien la MEME limitation
    // anterieure a cette phase que le mode Legacy (cf. commentaire en tete de
    // bloc) -- aucune branche de "trident"/"y" n'echappe a la cause racine
    // commune (creux de largeur dans compute_column_stations, partagee entre
    // les deux modes), donc ni l'un ni l'autre ne peut construire les 3/3 (ou
    // 2/2) colonnes attendues sous le chemin corridor aujourd'hui.
    for (const char* shape : {"y", "trident"}) {
        INFO("forme = " << shape);
        const auto region = make_shape(shape);
        REQUIRE(region.has_value());
        SatinColumnsParameters params;
        params.analysis.raster.pixel_size = Micrometers{100};
        params.use_corridor_tracing_dev_only = true;
        params.geometry_mode = SatinGeometryMode::Parametric;
        const auto r = build_satin_columns(*region, params);
        WARN("forme=" << shape << " -- parametric: statut=" << to_string(r.status) << " refus=\""
                      << r.refusal << "\" objets=" << r.parametric_columns.size());
    }
}
