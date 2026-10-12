// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>

#include "openstitch/stipple/tour.hpp"

using namespace openstitch::stipple;

namespace {

bool is_permutation(const std::vector<std::size_t>& order, std::size_t n) {
    std::vector<std::size_t> sorted = order;
    std::sort(sorted.begin(), sorted.end());
    if (sorted.size() != n) {
        return false;
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (sorted[i] != i) {
            return false;
        }
    }
    return true;
}

std::vector<PointD> grid(int w, int h) {
    std::vector<PointD> p;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            p.push_back({static_cast<double>(x), static_cast<double>(y)});
        }
    }
    return p;
}

// Generateur congruentiel : points pseudo-aleatoires reproductibles.
std::vector<PointD> scatter(int n, std::uint32_t seed, double size) {
    std::vector<PointD> p;
    std::uint32_t s = seed;
    const auto next = [&] {
        s = s * 1664525U + 1013904223U;
        return static_cast<double>(s >> 8) / 16777216.0 * size;
    };
    for (int i = 0; i < n; ++i) {
        const double x = next();
        const double y = next();
        p.push_back({x, y});
    }
    return p;
}

} // namespace

TEST_CASE("tour : cas triviaux") {
    CHECK(optimize_tour({}).order.empty());
    CHECK(optimize_tour({{1, 2}}).order.size() == 1);
    const auto three = optimize_tour({{0, 0}, {1, 0}, {0, 1}});
    CHECK(is_permutation(three.order, 3));
    CHECK(three.length == tour_length({{0, 0}, {1, 0}, {0, 1}}, three.order));
}

TEST_CASE("tour : permutation valide, jamais plus long que le tour de Hilbert") {
    for (const int n : {50, 400, 2000}) {
        const auto pts = scatter(n, 42, 1000.0);
        const auto r = optimize_tour(pts);
        INFO("n = " << n);
        CHECK(is_permutation(r.order, pts.size()));
        CHECK(r.length <= r.initial_length);
        CHECK(r.length == tour_length(pts, r.order));
        // 2-opt + Or-opt font beaucoup mieux qu'une courbe de remplissage.
        CHECK(r.length < 0.92 * r.initial_length);
    }
}

TEST_CASE("tour : proche de l'optimum connu sur une grille et sur un semis uniforme") {
    // Grille 20 x 20 : un cycle hamiltonien de pas 1 existe, optimum = 400.
    const auto g = grid(20, 20);
    const auto rg = optimize_tour(g);
    CHECK(rg.length <= 1.10 * 400.0);

    // Semis uniforme : longueur optimale ~ 0,7124 * sqrt(n * aire) (Beardwood,
    // Halton, Hammersley), plus un effet de bord ; 2-opt + Or-opt vise ~5 % de plus.
    const int n = 2000;
    const auto pts = scatter(n, 7, 1000.0);
    const auto r = optimize_tour(pts);
    const double asymptotic = 0.7124 * std::sqrt(static_cast<double>(n) * 1000.0 * 1000.0);
    CHECK(r.length < 1.20 * asymptotic);
}

TEST_CASE("tour : deterministe, points confondus acceptes") {
    auto pts = scatter(300, 3, 100.0);
    pts.push_back(pts[10]);
    pts.push_back(pts[10]);
    const auto a = optimize_tour(pts);
    const auto b = optimize_tour(pts);
    CHECK(a.order == b.order);
    CHECK(is_permutation(a.order, pts.size()));
}

TEST_CASE("tour : points alignes -> longueur = 2 x l'etendue") {
    std::vector<PointD> line;
    for (int i = 0; i < 100; ++i) {
        line.push_back({static_cast<double>((i * 37) % 100), 0.0});
    }
    const auto r = optimize_tour(line);
    CHECK(r.length <= 2.0 * 99.0 + 1e-6);
}
