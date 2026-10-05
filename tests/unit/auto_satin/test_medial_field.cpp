// SPDX-License-Identifier: Apache-2.0
//
// Tests analytiques de nearest_boundary_feet (medial_field.hpp, HP-STI-018
// Phase A, specs/plans/hp-sti-018-turning-satin.md §2.1/§5). Formes à
// solution calculée à la main : un polygone régulier approximant un cercle
// (projection exacte sur la bissectrice perpendiculaire d'une arête), un
// rectangle (arêtes/coin), et un L avec un coin réflexe (multiplicité de
// pieds près d'une concavité -- le cas que foot_multiplicity, § Phase B/C,
// devra exploiter).
#include "medial_field.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace openstitch::auto_satin::detail;

namespace {

// Polygone régulier à `sides` côtés, rayon `radius`, centré à l'origine --
// approximation polygonale d'un cercle. Sommet i à l'angle 2*pi*i/sides.
Poly make_regular_polygon(double radius, int sides) {
    Poly poly;
    poly.reserve(static_cast<std::size_t>(sides));
    for (int i = 0; i < sides; ++i) {
        const double theta =
            2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(sides);
        poly.push_back({radius * std::cos(theta), radius * std::sin(theta)});
    }
    return poly;
}

// Rectangle axis-aligned CCW : v0=(min) -> v1 -> v2(max) -> v3, donc
// e0=bas, e1=droite, e2=haut (parcouru droite->gauche), e3=gauche (haut->bas).
Poly make_rectangle(P2 min_corner, P2 max_corner) {
    return {min_corner, {max_corner.x, min_corner.y}, max_corner, {min_corner.x, max_corner.y}};
}

// "L" : union de [0,2000]x[0,1000] et [0,1000]x[0,2000], coin réflexe en
// (1000,1000) (v3) où e2 (v2->v3, horizontale y=1000) et e3 (v3->v4,
// verticale x=1000) se rejoignent avec un angle intérieur de 270°.
Poly make_l_shape() {
    return {{0.0, 0.0},       {2000.0, 0.0},    {2000.0, 1000.0},
            {1000.0, 1000.0}, {1000.0, 2000.0}, {0.0, 2000.0}};
}

// Rectangle representant le "capuchon" interieur d'une branche radiale a
// l'angle `angle_rad`, dont l'arete 0 (v0->v1) est exactement perpendiculaire
// a la direction radiale, centree a la distance `radius` du centre -- sert a
// construire une vraie jonction a N branches (N polygones distincts dont les
// capuchons se font face a egale distance), par opposition a un simple sommet
// partage par deux aretes du MEME polygone (cf. ATTENTION dans
// medial_field.hpp).
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

TEST_CASE("nearest_boundary_feet empty polys returns empty", "[medial_field]") {
    const std::vector<Poly> polys; // aucune region
    const auto feet = nearest_boundary_feet(polys, P2{0.0, 0.0});
    CHECK(feet.empty());
}

TEST_CASE("nearest_boundary_feet max_feet non positif renvoie vide", "[medial_field]") {
    const std::vector<Poly> polys{make_rectangle({0.0, 0.0}, {100.0, 100.0})};
    FootQuery query;
    query.max_feet = 0;
    const auto feet = nearest_boundary_feet(polys, P2{50.0, 50.0}, query);
    CHECK(feet.empty());
}

TEST_CASE("nearest_boundary_feet ignore un polygone degenere sans planter", "[medial_field]") {
    // polys[0] : un seul sommet (degenere, aucune arete valable).
    // polys[1] : rectangle valide -- doit rester seul contributeur, et garder
    // son poly_index=1 d'origine (pas de reindexation en sautant le degenere).
    const std::vector<Poly> polys{Poly{{0.0, 0.0}}, make_rectangle({0.0, 0.0}, {1000.0, 1000.0})};
    const auto feet = nearest_boundary_feet(polys, P2{500.0, 10.0});
    REQUIRE_FALSE(feet.empty());
    for (const auto& foot : feet) {
        CHECK(foot.poly_index == 1);
    }
    CHECK(feet.front().distance_um == Catch::Approx(10.0).margin(1e-9));
}

TEST_CASE("nearest_boundary_feet polygone a 2 sommets ignore sans planter", "[medial_field]") {
    const std::vector<Poly> polys{Poly{{0.0, 0.0}, {10.0, 0.0}}};
    const auto feet = nearest_boundary_feet(polys, P2{5.0, 5.0});
    CHECK(feet.empty());
}

TEST_CASE("nearest_boundary_feet cercle (polygone regulier) pied exact sur la bissectrice d'arete",
          "[medial_field]") {
    // Un polygone regulier n'est pas un cercle exact : le long de la
    // bissectrice perpendiculaire d'une arete (direction du milieu de
    // l'arete depuis le centre), la projection reste exactement au milieu
    // de cette arete (t=0.5) quel que soit le rayon interroge -- preuve :
    // v_i + v_{i+1} = 2*R*cos(pi/N) * direction(theta_i + pi/N) (somme des
    // cosinus/sinus), donc le milieu de l'arete i est exactement a
    // l'apotheme R*cos(pi/N) le long de cette direction. C'est la valeur
    // "rayon du cercle" hand-computed pertinente ici (l'apotheme, pas R).
    constexpr double radius = 1000.0;
    constexpr int sides = 720;
    const Poly circle = make_regular_polygon(radius, sides);
    const std::vector<Poly> polys{circle};

    constexpr int edge_index = 100;
    const double apothem = radius * std::cos(std::numbers::pi / sides);
    const double edge_angle = 2.0 * std::numbers::pi * (static_cast<double>(edge_index) + 0.5) /
                              static_cast<double>(sides);
    const P2 dir{std::cos(edge_angle), std::sin(edge_angle)};

    SECTION("point exactement sur l'arete (distance 0)") {
        const P2 p{dir.x * apothem, dir.y * apothem};
        const auto feet = nearest_boundary_feet(polys, p);
        REQUIRE_FALSE(feet.empty());
        const auto& foot = feet.front();
        CHECK(foot.poly_index == 0);
        CHECK(foot.edge_index == static_cast<std::size_t>(edge_index));
        CHECK(foot.edge_t == Catch::Approx(0.5).margin(1e-9));
        CHECK(foot.distance_um == Catch::Approx(0.0).margin(1e-6));
        CHECK(foot.point.x == Catch::Approx(p.x).margin(1e-6));
        CHECK(foot.point.y == Catch::Approx(p.y).margin(1e-6));
    }

    SECTION("point interieur -- distance = apotheme - rayon interroge") {
        constexpr double query_radius = 700.0;
        const P2 p{dir.x * query_radius, dir.y * query_radius};
        const auto feet = nearest_boundary_feet(polys, p);
        REQUIRE_FALSE(feet.empty());
        const auto& foot = feet.front();
        CHECK(foot.edge_index == static_cast<std::size_t>(edge_index));
        CHECK(foot.edge_t == Catch::Approx(0.5).margin(1e-9));
        CHECK(foot.distance_um == Catch::Approx(apothem - query_radius).margin(1e-6));
        // L'angle du pied doit correspondre a l'angle du point interroge
        // (meme bissectrice).
        CHECK(std::atan2(foot.point.y, foot.point.x) == Catch::Approx(edge_angle).margin(1e-9));
    }

    SECTION("point exterieur -- distance = rayon interroge - apotheme") {
        constexpr double query_radius = 1300.0;
        const P2 p{dir.x * query_radius, dir.y * query_radius};
        const auto feet = nearest_boundary_feet(polys, p);
        REQUIRE_FALSE(feet.empty());
        const auto& foot = feet.front();
        CHECK(foot.edge_index == static_cast<std::size_t>(edge_index));
        CHECK(foot.edge_t == Catch::Approx(0.5).margin(1e-9));
        CHECK(foot.distance_um == Catch::Approx(query_radius - apothem).margin(1e-6));
        CHECK(std::atan2(foot.point.y, foot.point.x) == Catch::Approx(edge_angle).margin(1e-9));
    }
}

TEST_CASE("nearest_boundary_feet rectangle pied sur le bon segment avec le bon edge_t",
          "[medial_field]") {
    const Poly rect = make_rectangle({0.0, 0.0}, {2000.0, 1000.0});
    const std::vector<Poly> polys{rect};

    SECTION("pres du bord haut (e2), au milieu") {
        const auto feet = nearest_boundary_feet(polys, P2{1000.0, 950.0});
        REQUIRE_FALSE(feet.empty());
        const auto& foot = feet.front();
        CHECK(foot.edge_index == 2);
        CHECK(foot.edge_t == Catch::Approx(0.5).margin(1e-9));
        CHECK(foot.distance_um == Catch::Approx(50.0).margin(1e-9));
        CHECK(foot.point.x == Catch::Approx(1000.0).margin(1e-9));
        CHECK(foot.point.y == Catch::Approx(1000.0).margin(1e-9));
    }

    SECTION("pres du bord droit (e1)") {
        const auto feet = nearest_boundary_feet(polys, P2{1990.0, 500.0});
        REQUIRE_FALSE(feet.empty());
        const auto& foot = feet.front();
        CHECK(foot.edge_index == 1);
        CHECK(foot.edge_t == Catch::Approx(0.5).margin(1e-9));
        CHECK(foot.distance_um == Catch::Approx(10.0).margin(1e-9));
    }

    SECTION("pres du coin v0 -- equidistant de e0 et e3 (bissectrice du coin)") {
        FootQuery query;
        query.max_feet = 2;
        const auto feet = nearest_boundary_feet(polys, P2{10.0, 10.0}, query);
        REQUIRE(feet.size() == 2);
        // Tries par distance (egalite ici) puis edge_index croissant.
        CHECK(feet[0].edge_index == 0);
        CHECK(feet[1].edge_index == 3);
        CHECK(feet[0].distance_um == Catch::Approx(10.0).margin(1e-9));
        CHECK(feet[1].distance_um == Catch::Approx(10.0).margin(1e-9));
    }

    SECTION("exactement sur le sommet v0 -- les deux aretes adjacentes a distance 0") {
        FootQuery query;
        query.max_feet = 3;
        const auto feet = nearest_boundary_feet(polys, P2{0.0, 0.0}, query);
        REQUIRE(feet.size() == 2);
        CHECK(feet[0].edge_index == 0);
        CHECK(feet[1].edge_index == 3);
        CHECK(feet[0].distance_um == Catch::Approx(0.0).margin(1e-9));
        CHECK(feet[1].distance_um == Catch::Approx(0.0).margin(1e-9));
        // Meme point (le sommet), identite differente (edge_index).
        CHECK(feet[0].point.x == Catch::Approx(feet[1].point.x).margin(1e-9));
        CHECK(feet[0].point.y == Catch::Approx(feet[1].point.y).margin(1e-9));
    }
}

TEST_CASE("nearest_boundary_feet L concave -- multiplicite de pieds pres du coin reflexe",
          "[medial_field]") {
    const Poly l_shape = make_l_shape();
    const std::vector<Poly> polys{l_shape};
    const P2 p{950.0, 950.0}; // juste a l'interieur, pres du coin reflexe (1000,1000)
    const double expected_distance = std::sqrt(50.0 * 50.0 + 50.0 * 50.0);

    SECTION("max_feet=2 -- les deux aretes du coin reflexe, toutes deux au sommet") {
        FootQuery query;
        query.max_feet = 2;
        const auto feet = nearest_boundary_feet(polys, p, query);
        REQUIRE(feet.size() == 2);
        CHECK(feet[0].edge_index == 2);
        CHECK(feet[1].edge_index == 3);
        CHECK(feet[0].distance_um == Catch::Approx(expected_distance).margin(1e-6));
        CHECK(feet[1].distance_um == Catch::Approx(expected_distance).margin(1e-6));
        CHECK(feet[0].point.x == Catch::Approx(1000.0).margin(1e-9));
        CHECK(feet[0].point.y == Catch::Approx(1000.0).margin(1e-9));
        CHECK(feet[1].point.x == Catch::Approx(1000.0).margin(1e-9));
        CHECK(feet[1].point.y == Catch::Approx(1000.0).margin(1e-9));
    }

    SECTION("max_feet=3 -- le filtre de tolerance n'en garde que 2 (pas de 3e a egalite)") {
        FootQuery query;
        query.max_feet = 3;
        const auto feet = nearest_boundary_feet(polys, p, query);
        // Filtre de tolerance, pas un compte fixe : rien d'autre n'est a
        // moins de 2% du minimum ici, donc 2 pieds seulement malgre
        // max_feet=3.
        REQUIRE(feet.size() == 2);
    }
}

TEST_CASE("nearest_boundary_feet jonction reelle a 3 branches distinctes -- pieds NON coincidents",
          "[medial_field]") {
    // 3 polygones separes (3 branches d'un squelette en Y/trident), chacun un
    // capuchon radial a 120 degres d'ecart, meme rayon -- le centre est donc
    // equidistant des 3 capuchons, mais les 3 pieds sont a des POINTS
    // DISTINCTS (un par branche), contrairement au cas "sommet ordinaire" du
    // test rectangle ci-dessus ou les 2 pieds partagent le MEME point. C'est
    // la distinction que medial_field.hpp documente comme necessaire a toute
    // detection de jonction en Phase B/C : compter >= 3 pieds ne suffit pas,
    // il faut aussi qu'ils ne soient pas coincidents.
    constexpr double radius = 1000.0;
    constexpr double half_width = 50.0;
    constexpr double depth = 20.0;
    const std::vector<Poly> polys{
        make_branch_cap(0.0, radius, half_width, depth),
        make_branch_cap(2.0 * std::numbers::pi / 3.0, radius, half_width, depth),
        make_branch_cap(4.0 * std::numbers::pi / 3.0, radius, half_width, depth),
    };

    FootQuery query;
    query.max_feet = 3;
    const auto feet = nearest_boundary_feet(polys, P2{0.0, 0.0}, query);

    REQUIRE(feet.size() == 3);
    // Un pied par branche, chacun sur l'arete 0 (le capuchon interieur) de son
    // propre polygone, tous a la meme distance (le rayon) -- aux imprecisions
    // flottantes de cos/sin pres (~1e-13), qui departagent reellement l'ordre
    // de tri entre branches sans jamais atteindre le tie-break poly_index (il
    // ne s'agit donc PAS d'egalites exactes malgre la construction
    // geometriquement symetrique) : on verifie l'ensemble des poly_index
    // presents, pas un ordre positionnel precis.
    std::vector<std::size_t> seen_poly_indices;
    for (const auto& foot : feet) {
        CHECK(foot.edge_index == 0);
        CHECK(foot.distance_um == Catch::Approx(radius).margin(1e-6));
        seen_poly_indices.push_back(foot.poly_index);
    }
    std::ranges::sort(seen_poly_indices);
    CHECK(seen_poly_indices == std::vector<std::size_t>{0, 1, 2});
    // Le coeur de la distinction : les 3 points sont mutuellement NON
    // coincidents (contrairement au cas "sommet ordinaire" ci-dessus, ou les
    // pieds partagent le meme point). Un consommateur en aval qui ne
    // verifierait que foot_multiplicity >= 3 ne pourrait pas, a lui seul,
    // distinguer ce cas d'un sommet ordinaire pres duquel un 3e pied
    // lointain serait aussi dans la tolerance -- d'ou l'avertissement dans
    // medial_field.hpp : verifier aussi la non-coincidence des points.
    constexpr double min_separation = 100.0; // tres inferieur a la distance reelle entre branches
    for (std::size_t i = 0; i < feet.size(); ++i) {
        for (std::size_t j = i + 1; j < feet.size(); ++j) {
            const double dx = feet[i].point.x - feet[j].point.x;
            const double dy = feet[i].point.y - feet[j].point.y;
            const double separation = std::sqrt(dx * dx + dy * dy);
            CHECK(separation > min_separation);
        }
    }
}

TEST_CASE("nearest_boundary_feet determinisme -- deux appels identiques, sortie identique",
          "[medial_field]") {
    const Poly l_shape = make_l_shape();
    const std::vector<Poly> polys{l_shape};
    const P2 p{950.0, 950.0};
    FootQuery query;
    query.max_feet = 3;

    const auto feet_a = nearest_boundary_feet(polys, p, query);
    const auto feet_b = nearest_boundary_feet(polys, p, query);

    REQUIRE(feet_a.size() == feet_b.size());
    for (std::size_t i = 0; i < feet_a.size(); ++i) {
        CHECK(feet_a[i].poly_index == feet_b[i].poly_index);
        CHECK(feet_a[i].edge_index == feet_b[i].edge_index);
        CHECK(feet_a[i].edge_t == feet_b[i].edge_t);
        CHECK(feet_a[i].distance_um == feet_b[i].distance_um);
        CHECK(feet_a[i].point.x == feet_b[i].point.x);
        CHECK(feet_a[i].point.y == feet_b[i].point.y);
    }
}

TEST_CASE("nearest_boundary_feet tolerance_relative filtre les candidats trop loin",
          "[medial_field]") {
    const Poly rect = make_rectangle({0.0, 0.0}, {2000.0, 1000.0});
    const std::vector<Poly> polys{rect};
    // Loin de tout coin : seule la plus proche arete (e0, bas) doit rester
    // sous le seuil de tolerance de 2% -- e3 (gauche) est a 500um, a la fois
    // bien plus loin que 1.02x la distance minimale (10um).
    FootQuery query;
    query.max_feet = 4;
    const auto feet = nearest_boundary_feet(polys, P2{500.0, 10.0}, query);
    REQUIRE(feet.size() == 1);
    CHECK(feet.front().edge_index == 0);
}
