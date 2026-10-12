// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

#include "openstitch/auto_satin/graph_cleanup.hpp"
#include "openstitch/auto_satin/shapes.hpp"
#include "openstitch/auto_satin/skeleton_satin.hpp"

using namespace openstitch;
using namespace openstitch::auto_satin;

namespace {

Vec2um um(double x, double y) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(x))},
                  Micrometers{static_cast<std::int32_t>(std::lround(y))}};
}

// Jonction J (rayon rJ) a l'origine, trois branches terminales :
//   0 : longue (axe +x), 1 : longue (axe -x), 2 : rameau de (longueur, rayon) donnes.
SkeletonGraph make_tee(double rJ, double hairLength, double hairRadius) {
    SkeletonGraph g;
    const auto node = [&](double x, double y, double r, SkeletonNodeType t) {
        SkeletonNode n;
        n.id = static_cast<std::uint32_t>(g.nodes.size());
        n.position = um(x, y);
        n.type = t;
        n.local_radius_um = r;
        g.nodes.push_back(n);
        return n.id;
    };
    const auto edge = [&](std::uint32_t a, std::uint32_t b, double rA, double rB) {
        SkeletonEdge e;
        e.id = static_cast<std::uint32_t>(g.edges.size());
        e.from = a;
        e.to = b;
        e.centerline = {g.nodes[a].position, g.nodes[b].position};
        e.length_um = length_um(g.nodes[b].position - g.nodes[a].position);
        e.local_radii_um = {rA, rB};
        g.edges.push_back(e);
    };
    const auto j = node(0, 0, rJ, SkeletonNodeType::Junction);
    const auto a = node(20'000, 0, rJ, SkeletonNodeType::Endpoint);
    const auto b = node(-20'000, 0, rJ, SkeletonNodeType::Endpoint);
    const auto c = node(0, hairLength, hairRadius, SkeletonNodeType::Endpoint);
    edge(j, a, rJ, rJ);
    edge(j, b, rJ, rJ);
    edge(j, c, rJ, hairRadius);
    return g;
}

GraphCleanupParameters only_bounded(double abs_um, double ratio) {
    GraphCleanupParameters p;
    p.minimum_branch_length = Micrometers{0}; // coupe l'ancienne regle
    p.minimum_length_to_radius_ratio = 0.0;
    p.max_protrusion = Micrometers{static_cast<std::int32_t>(abs_um)};
    p.max_protrusion_to_radius = ratio;
    return p;
}

} // namespace

TEST_CASE("elagage borne : desactive par defaut, graphe inchange") {
    const SkeletonGraph g = make_tee(1'000, 300, 200);
    GraphCleanupParameters p;
    p.minimum_branch_length = Micrometers{0};
    p.minimum_length_to_radius_ratio = 0.0;
    const auto out = prune_graph(g, p);
    CHECK(out.graph.edges.size() == 3);
    CHECK(out.removed.empty());
}

TEST_CASE("elagage borne : un rameau qui ne depasse pas le disque de la jonction disparait") {
    // L + rE - rJ = 300 + 200 - 1000 < 0 : le rameau est dans le disque de la jonction.
    const auto out = prune_graph(make_tee(1'000, 300, 200), only_bounded(100, 0.0));
    CHECK(out.graph.edges.size() == 2);
    // La branche, puis le diagnostic habituel du noeud devenu orphelin.
    REQUIRE(out.removed.size() == 2);
    CHECK(out.removed[0].reason.rfind("branche terminale", 0) == 0);
    // La jonction redevient une continuation (degre 2).
    std::size_t junctions = 0;
    for (const auto& n : out.graph.nodes) {
        junctions += n.type == SkeletonNodeType::Junction ? 1U : 0U;
    }
    CHECK(junctions == 0);
}

TEST_CASE("elagage borne : un rameau qui porte un vrai detail est garde, meme court") {
    // Court (1,2 mm) mais epais et saillant : h = 1200 + 800 - 1000 = 1000 µm.
    // L'ancienne regle (longueur < 1,5 mm) l'aurait retire.
    GraphCleanupParameters legacy;
    const auto g = make_tee(1'000, 1'200, 800);
    CHECK(prune_graph(g, legacy).graph.edges.size() == 2);
    const auto kept = prune_graph(g, only_bounded(300, 0.1));
    CHECK(kept.graph.edges.size() == 3);
    CHECK(kept.removed.empty());
}

TEST_CASE("elagage borne : le seuil est monotone, la saillie decide") {
    // Rameau de saillie h = 600 : retire des que le seuil absolu atteint 600.
    const SkeletonGraph g = make_tee(1'000, 1'400, 200); // h = 1400 + 200 - 1000 = 600
    CHECK(prune_graph(g, only_bounded(599, 0.0)).graph.edges.size() == 3);
    CHECK(prune_graph(g, only_bounded(600, 0.0)).graph.edges.size() == 2);
    // Seuil relatif : 0,6 * rJ = 600.
    CHECK(prune_graph(g, only_bounded(0, 0.59)).graph.edges.size() == 3);
    CHECK(prune_graph(g, only_bounded(0, 0.60)).graph.edges.size() == 2);
}

TEST_CASE("elagage borne : borne de Hausdorff verifiee sur les disques de la branche") {
    // Pour tout point c de la branche (s = abscisse, r(s) rayon) : le disque
    // (c, r) reste dans le disque de la jonction agrandi de h.
    const double rJ = 1'000.0;
    const double L = 1'400.0;
    const double rE = 200.0;
    const double h = L + rE - rJ;
    for (int i = 0; i <= 20; ++i) {
        const double s = L * i / 20.0;
        const double r = rJ + (rE - rJ) * i / 20.0; // rayon qui decroit lineairement
        REQUIRE(r >= rJ - s);                       // 1-lipschitz
        CHECK(s + r <= rJ + h + 1e-9);
    }
}

TEST_CASE("elagage borne : ne supprime jamais tout le graphe, deterministe") {
    SkeletonGraph g = make_tee(5'000, 100, 100);
    const auto p = only_bounded(100'000, 1.0);
    const auto a = prune_graph(g, p);
    const auto b = prune_graph(g, p);
    CHECK(a.graph.edges.size() >= 1);
    CHECK(a.graph.edges.size() == b.graph.edges.size());
    CHECK(a.removed.size() == b.removed.size());
}

TEST_CASE("elagage borne : contour propre inchange, defaut du moteur = analyse stable") {
    const SkeletonSatinParameters params;
    CHECK(params.analysis.cleanup.max_protrusion.value == 400);
    CHECK(params.analysis.cleanup.max_protrusion_to_radius == 0.15);
    // AutoSatinParameters seul reste a l'ancien comportement.
    CHECK(AutoSatinParameters{}.cleanup.max_protrusion.value == 0);
}

namespace {

// Densifie un contour (un point tous les `step` µm) puis le bruite de ±amp
// (generateur congruentiel entier : reproductible partout).
geometry::PathSet noisy(const geometry::PathSet& in, double step, double amp) {
    std::uint32_t s = 7;
    const auto rnd = [&] {
        s = s * 1664525U + 1013904223U;
        return (static_cast<double>(s >> 8) / 16777216.0) * 2.0 - 1.0;
    };
    const auto dens = [&](const geometry::Path& p) {
        geometry::Path o;
        o.closed = true;
        for (std::size_t i = 0; i < p.nodes.size(); ++i) {
            const Vec2um a = p.nodes[i].pos;
            const Vec2um b = p.nodes[(i + 1) % p.nodes.size()].pos;
            const int n = std::max(1, static_cast<int>(length_um(b - a) / step));
            for (int k = 0; k < n; ++k) {
                const double t = static_cast<double>(k) / n;
                o.nodes.push_back({um(a.x.value + (b.x.value - a.x.value) * t + rnd() * amp,
                                      a.y.value + (b.y.value - a.y.value) * t + rnd() * amp),
                                   geometry::NodeType::Corner, std::nullopt, std::nullopt});
            }
        }
        return o;
    };
    geometry::PathSet out;
    out.outer = dens(in.outer);
    for (const auto& h : in.holes) {
        out.holes.push_back(dens(h));
    }
    return out;
}

} // namespace

TEST_CASE(
    "elagage borne : contour bruite -> moins de colonnes parasites qu'avec l'ancienne regle") {
    std::size_t legacyTotal = 0;
    std::size_t stableTotal = 0;
    for (const char* name : {"capsule", "cross", "h", "comb", "E"}) {
        const auto clean = make_shape(name);
        REQUIRE(clean.has_value());
        const geometry::PathSet region = noisy(*clean, 400.0, 200.0);

        SkeletonSatinParameters legacy;
        legacy.analysis = AutoSatinParameters{}; // ancien comportement
        const SkeletonSatinParameters stable;    // defaut du moteur
        const auto a = generate_skeleton_satin(region, legacy);
        const auto b = generate_skeleton_satin(region, stable);
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        INFO(name);
        CHECK(b->columns.size() <= a->columns.size());
        legacyTotal += a->columns.size();
        stableTotal += b->columns.size();
    }
    CHECK(stableTotal < legacyTotal);
}
