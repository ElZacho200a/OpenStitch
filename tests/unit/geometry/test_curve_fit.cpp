// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

#include "openstitch/geometry/curve_fit.hpp"
#include "openstitch/geometry/polyline.hpp"
#include "openstitch/geometry/simplify.hpp"

using namespace openstitch;
using namespace openstitch::geometry;

namespace {

PathNode node(double x, double y) {
    return PathNode{Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(x))},
                           Micrometers{static_cast<std::int32_t>(std::lround(y))}},
                    NodeType::Corner, std::nullopt, std::nullopt};
}

// Cercle echantillonne comme un contour de pixels : les points sont ramenes sur
// une grille de `grid` um (escalier), puis dedoublonnes.
Path pixel_circle(double radius, double grid, int samples = 720) {
    Path p;
    p.closed = true;
    for (int i = 0; i < samples; ++i) {
        const double a = 2.0 * std::numbers::pi * i / samples;
        const double x = std::round(radius * std::cos(a) / grid) * grid;
        const double y = std::round(radius * std::sin(a) / grid) * grid;
        if (p.nodes.empty() || p.nodes.back().pos != node(x, y).pos) {
            p.nodes.push_back(node(x, y));
        }
    }
    if (p.nodes.size() > 1 && p.nodes.front().pos == p.nodes.back().pos) {
        p.nodes.pop_back();
    }
    return p;
}

// Distance max d'un chemin aplati au cercle ideal.
double radial_error(const Path& path, double radius) {
    const Polyline f = flatten(path, Micrometers{2});
    double worst = 0.0;
    for (const Vec2um& v : f.points) {
        const double r = std::hypot(static_cast<double>(v.x.value), static_cast<double>(v.y.value));
        worst = std::max(worst, std::fabs(r - radius));
    }
    return worst;
}

std::size_t curved_nodes(const Path& p) {
    std::size_t n = 0;
    for (const auto& nd : p.nodes) {
        n += (nd.tan_in || nd.tan_out) ? 1U : 0U;
    }
    return n;
}

} // namespace

TEST_CASE("ajustement de courbes : un cercle en escalier tient en peu de noeuds") {
    constexpr double kGrid = 265.0; // ~ un pixel a 96 dpi
    // Tolerances >= ~1,5 px : en dessous, une part du budget part dans le bruit de la trame.
    for (const double radius : {20'000.0, 8'000.0}) {
        for (const int t : {400, 530}) {
            const Micrometers tol{t};
            const Path stair = pixel_circle(radius, kGrid, 2000);
            REQUIRE(stair.nodes.size() > 100);

            const Path poly = simplify(stair, tol);
            const Path curve = fit_curves(stair, {.tolerance = tol});

            // Critere de la feuille de route (HP-AUTO-004) : noeuds divises par >= 3
            // a ecart de contour borne par la meme tolerance...
            INFO("rayon " << radius << " tol " << t << " dp " << poly.nodes.size() << " fit "
                          << curve.nodes.size());
            CHECK(curve.nodes.size() * 3 <= poly.nodes.size());
            CHECK(hausdorff_distance_um(curve, stair) <= static_cast<double>(t) * 1.05);
            // ... et l'ecart au vrai cercle reste dans la tolerance.
            CHECK(radial_error(curve, radius) <= static_cast<double>(t));
            // Des courbes sont bien produites (et pas seulement des cordes).
            CHECK(curved_nodes(curve) >= 2);
        }
    }
}

TEST_CASE("ajustement de courbes : un rectangle reste quatre coins droits") {
    Path r;
    r.closed = true;
    for (int x = 0; x <= 10'000; x += 500)
        r.nodes.push_back(node(x, 0));
    for (int y = 500; y <= 6'000; y += 500)
        r.nodes.push_back(node(10'000, y));
    for (int x = 9'500; x >= 0; x -= 500)
        r.nodes.push_back(node(x, 6'000));
    for (int y = 5'500; y > 0; y -= 500)
        r.nodes.push_back(node(0, y));

    const Path out = fit_curves(r, {.tolerance = Micrometers{100}});
    CHECK(out.nodes.size() == 4);
    CHECK(curved_nodes(out) == 0);
    for (const auto& nd : out.nodes) {
        CHECK(nd.type == NodeType::Corner);
    }
    CHECK(hausdorff_distance_um(out, r) <= 1.0);
}

TEST_CASE("ajustement de courbes : un rectangle a coins arrondis garde ses coins et ses droites") {
    // Rectangle 40 x 24 mm, rayon de coin 5 mm.
    constexpr double W = 40'000.0, H = 24'000.0, R = 5'000.0;
    Path p;
    p.closed = true;
    const auto arc = [&](double cx, double cy, double a0) {
        for (int i = 0; i <= 40; ++i) {
            const double a = a0 + std::numbers::pi / 2.0 * i / 40.0;
            p.nodes.push_back(node(cx + R * std::cos(a), cy + R * std::sin(a)));
        }
    };
    arc(W / 2 - R, H / 2 - R, 0.0);
    arc(-W / 2 + R, H / 2 - R, std::numbers::pi / 2.0);
    arc(-W / 2 + R, -H / 2 + R, std::numbers::pi);
    arc(W / 2 - R, -H / 2 + R, 1.5 * std::numbers::pi);

    const Path out = fit_curves(p, {.tolerance = Micrometers{100}});
    CHECK(hausdorff_distance_um(out, p) <= 100.0 * 1.15);
    CHECK(out.nodes.size() <= 12);
    // Les quatre cotes droits restent des segments droits : il existe des
    // aretes sans tangente.
    std::size_t straight_edges = 0;
    for (std::size_t i = 0; i < out.nodes.size(); ++i) {
        const auto& a = out.nodes[i];
        const auto& b = out.nodes[(i + 1) % out.nodes.size()];
        straight_edges += (!a.tan_out && !b.tan_in) ? 1U : 0U;
    }
    CHECK(straight_edges >= 4);
}

TEST_CASE("ajustement de courbes : chemin ouvert, extremites conservees") {
    Path open;
    open.closed = false;
    for (int i = 0; i <= 200; ++i) {
        const double a = std::numbers::pi * i / 200.0;
        open.nodes.push_back(node(10'000.0 * std::cos(a), 10'000.0 * std::sin(a)));
    }
    const Path out = fit_curves(open, {.tolerance = Micrometers{50}});
    CHECK_FALSE(out.closed);
    CHECK(out.nodes.front().pos == open.nodes.front().pos);
    CHECK(out.nodes.back().pos == open.nodes.back().pos);
    CHECK(out.nodes.size() <= 6);
    CHECK(hausdorff_distance_um(out, open) <= 50.0 * 1.15);
}

TEST_CASE("ajustement de courbes : deterministe, cas degeneres inchanges") {
    const Path stair = pixel_circle(8'000.0, 265.0);
    const Path a = fit_curves(stair);
    const Path b = fit_curves(stair);
    CHECK(a == b);

    Path tri;
    tri.closed = true;
    tri.nodes = {node(0, 0), node(1000, 0), node(0, 1000)};
    CHECK(fit_curves(tri) == tri);
    CHECK(fit_curves(stair, {.tolerance = Micrometers{0}}) == stair);
}

TEST_CASE(
    "ajustement de courbes : tolerance plus serree -> jamais moins de noeuds ni plus d'ecart") {
    const Path stair = pixel_circle(15'000.0, 265.0);
    const Path loose = fit_curves(stair, {.tolerance = Micrometers{600}});
    const Path tight = fit_curves(stair, {.tolerance = Micrometers{300}});
    CHECK(tight.nodes.size() >= loose.nodes.size());
    CHECK(hausdorff_distance_um(tight, stair) <= hausdorff_distance_um(loose, stair) + 1.0);
}
