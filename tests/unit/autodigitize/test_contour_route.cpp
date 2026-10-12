// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

#include "openstitch/autodigitize/contour_route.hpp"
#include "openstitch/geometry/polyline.hpp"

using namespace openstitch;
using namespace openstitch::autodigitize;

namespace {

Vec2um pt(double x, double y) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(x))},
                  Micrometers{static_cast<std::int32_t>(std::lround(y))}};
}

struct Builder {
    ContourComponent comp;

    std::int32_t node(double x, double y) {
        comp.nodes.push_back({pt(x, y), ContourNodeKind::Endpoint, 0});
        return static_cast<std::int32_t>(comp.nodes.size() - 1);
    }
    std::size_t segment(std::int32_t a, std::int32_t b, std::vector<Vec2um> mid = {}) {
        ContourSegment s;
        s.start_node = a;
        s.end_node = b;
        s.centerline.push_back(comp.nodes[static_cast<std::size_t>(a)].position);
        for (const Vec2um m : mid) {
            s.centerline.push_back(m);
        }
        s.centerline.push_back(comp.nodes[static_cast<std::size_t>(b)].position);
        s.length_um = geometry::polyline_length(s.centerline);
        comp.segments.push_back(std::move(s));
        return comp.segments.size() - 1;
    }
    std::vector<std::size_t> all() const {
        std::vector<std::size_t> v(comp.segments.size());
        for (std::size_t i = 0; i < v.size(); ++i) {
            v[i] = i;
        }
        return v;
    }
};

double trails_length(const std::vector<RoutedTrail>& trails) {
    double total = 0.0;
    for (const auto& t : trails) {
        total += geometry::polyline_length(t.points);
        if (t.closed && t.points.size() > 1) {
            total += length_um(t.points.front() - t.points.back());
        }
    }
    return total;
}

double segments_length(const Builder& b) {
    double total = 0.0;
    for (const auto& s : b.comp.segments) {
        total += s.length_um;
    }
    return total;
}

// Chaque segment est recousu dans au moins une polyligne : tous ses points
// apparaissent consecutivement (dans un sens ou dans l'autre).
bool covers(const Builder& b, const std::vector<RoutedTrail>& trails) {
    for (const auto& s : b.comp.segments) {
        bool found = false;
        for (const auto& t : trails) {
            std::vector<Vec2um> p = t.points;
            if (t.closed && !p.empty()) {
                p.push_back(p.front());
            }
            const std::size_t n = s.centerline.size();
            for (std::size_t i = 0; i + n <= p.size() && !found; ++i) {
                bool fwd = true;
                bool bwd = true;
                for (std::size_t k = 0; k < n; ++k) {
                    fwd = fwd && p[i + k] == s.centerline[k];
                    bwd = bwd && p[i + k] == s.centerline[n - 1 - k];
                }
                found = fwd || bwd;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("routage eulerien : un trait simple reste une seule polyligne") {
    Builder b;
    const auto a = b.node(0, 0);
    const auto c = b.node(20'000, 0);
    b.segment(a, c);
    RouteStats st;
    const auto trails = route_single_path(b.comp, b.all(), {}, &st);
    REQUIRE(trails.size() == 1);
    CHECK_FALSE(trails[0].closed);
    CHECK(st.retraced_um == 0.0);
    CHECK(covers(b, trails));
}

TEST_CASE("routage eulerien : eventail de 4 branches -> 2 polylignes au lieu de 4") {
    // Quatre branches qui partent d'une jonction dans un cone de 90 deg : aucune
    // paire n'est « alignee » (virage > 90 deg), l'ancien assemblage les laisse
    // toutes separees.
    Builder b;
    const auto j = b.node(0, 0);
    for (int i = 0; i < 4; ++i) {
        const double a = std::numbers::pi / 2.0 * i / 3.0;
        const auto e = b.node(30'000 * std::cos(a), 30'000 * std::sin(a));
        b.segment(j, e);
    }
    RouteStats st;
    const auto trails = route_single_path(b.comp, b.all(), {}, &st);
    CHECK(trails.size() == 2);    // 4 extremites impaires / 2
    CHECK(st.retraced_um == 0.0); // branches trop longues pour etre repassees
    CHECK(covers(b, trails));
    CHECK(trails_length(trails) == Catch::Approx(segments_length(b)).epsilon(1e-9));
}

TEST_CASE("routage eulerien : branches courtes repassees pour eviter les coupes") {
    // Meme eventail mais branches de 5 mm : les extremites sont a plus de 3 mm
    // les unes des autres (coupe) mais reliees par <= 12 mm de trait.
    Builder b;
    const auto j = b.node(0, 0);
    for (int i = 0; i < 4; ++i) {
        const double a = std::numbers::pi / 2.0 * i / 3.0;
        const auto e = b.node(5'000 * std::cos(a), 5'000 * std::sin(a));
        b.segment(j, e);
    }
    RouteStats st;
    const auto trails = route_single_path(b.comp, b.all(), {}, &st);
    REQUIRE(trails.size() == 1);
    CHECK(st.retraces >= 1);
    CHECK(st.retraced_um > 0.0);
    CHECK(covers(b, trails));
    CHECK(trails_length(trails) ==
          Catch::Approx(segments_length(b) + st.retraced_um).epsilon(1e-9));
}

TEST_CASE("routage eulerien : extremites proches -> simple saut, rien n'est repasse") {
    // Deux branches de 20 mm formant un V etroit : extremites a 1 mm l'une de
    // l'autre (un saut suffit), jonction trop loin de l'autre extremite pour
    // repasser.
    Builder b;
    const auto j = b.node(0, 0);
    const auto e1 = b.node(20'000, 500);
    const auto e2 = b.node(20'000, -500);
    b.segment(j, e1);
    b.segment(j, e2);
    // + une troisieme branche pour rendre la jonction impaire.
    const auto e3 = b.node(-20'000, 0);
    b.segment(j, e3);
    RouteStats st;
    const auto trails = route_single_path(b.comp, b.all(), {}, &st);
    CHECK(st.retraced_um == 0.0);
    CHECK(trails.size() == 2);
    CHECK(covers(b, trails));
}

TEST_CASE("routage eulerien : huit en deux boucles -> un seul circuit ferme") {
    Builder b;
    const auto j = b.node(0, 0);
    b.segment(j, j, {pt(10'000, 10'000), pt(20'000, 0), pt(10'000, -10'000)});
    b.segment(j, j, {pt(-10'000, 10'000), pt(-20'000, 0), pt(-10'000, -10'000)});
    RouteStats st;
    const auto trails = route_single_path(b.comp, b.all(), {}, &st);
    REQUIRE(trails.size() == 1);
    CHECK(trails[0].closed);
    CHECK(st.retraced_um == 0.0);
    CHECK(covers(b, trails));
}

TEST_CASE("routage eulerien : anneau pur et segment sans noeud passent tels quels") {
    Builder b;
    ContourSegment ring;
    ring.closed = true;
    ring.centerline = {pt(0, 0), pt(5'000, 0), pt(5'000, 5'000), pt(0, 5'000)};
    ring.length_um = 20'000;
    b.comp.segments.push_back(ring);
    ContourSegment loose;
    loose.centerline = {pt(0, 0), pt(8'000, 0)};
    loose.length_um = 8'000;
    b.comp.segments.push_back(loose);
    const auto trails = route_single_path(b.comp, b.all());
    REQUIRE(trails.size() == 2);
    CHECK(trails[0].closed);
    CHECK_FALSE(trails[1].closed);
}

TEST_CASE("routage eulerien : deterministe, couverture complete sur un reseau en grille") {
    // Grille 3x3 de traits de 15 mm : degres 2/3/4 melanges.
    Builder b;
    std::int32_t id[3][3];
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) {
            id[y][x] = b.node(x * 15'000.0, y * 15'000.0);
        }
    }
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) {
            if (x + 1 < 3)
                b.segment(id[y][x], id[y][x + 1]);
            if (y + 1 < 3)
                b.segment(id[y][x], id[y + 1][x]);
        }
    }
    RouteStats s1;
    RouteStats s2;
    const auto a = route_single_path(b.comp, b.all(), {}, &s1);
    const auto c = route_single_path(b.comp, b.all(), {}, &s2);
    REQUIRE(a.size() == c.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].points == c[i].points);
        CHECK(a[i].closed == c[i].closed);
    }
    CHECK(covers(b, a));
    // 4 coins de degre 2, 4 bords de degre 3 (impairs), centre de degre 4 :
    // 4 impairs -> 2 polylignes.
    CHECK(a.size() == 2);
    CHECK(s1.odd_nodes == 4);
}
