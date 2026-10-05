// SPDX-License-Identifier: Apache-2.0
//
// Équivalence bit à bit de `thin_zhang_suen` (version optimisée qui
// n'évalue que le bord, audit de performance 2026-09) avec l'implémentation
// naïve d'origine, recopiée ici telle quelle comme référence.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <random>
#include <string>
#include <vector>

#include "openstitch/auto_satin/auto_satin.hpp"
#include "openstitch/auto_satin/raster.hpp"
#include "openstitch/auto_satin/shapes.hpp"
#include "openstitch/auto_satin/skeleton.hpp"

using namespace openstitch;
using namespace openstitch::auto_satin;

namespace {

constexpr std::array<int, 8> DX{0, 1, 1, 1, 0, -1, -1, -1};
constexpr std::array<int, 8> DY{-1, -1, 0, 1, 1, 1, 0, -1};

int ref_get(const std::vector<std::uint8_t>& g, int w, int h, int x, int y) {
    if (x < 0 || y < 0 || x >= w || y >= h) {
        return 0;
    }
    return g[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
             static_cast<std::size_t>(x)];
}

bool ref_pass(std::vector<std::uint8_t>& g, int w, int h, int step) {
    std::vector<std::pair<int, int>> to_clear;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!ref_get(g, w, h, x, y)) {
                continue;
            }
            std::array<int, 8> p{};
            for (int k = 0; k < 8; ++k) {
                p[static_cast<std::size_t>(k)] =
                    ref_get(g, w, h, x + DX[static_cast<std::size_t>(k)],
                            y + DY[static_cast<std::size_t>(k)]);
            }
            int bsum = 0;
            for (int v : p) {
                bsum += v;
            }
            if (bsum < 2 || bsum > 6) {
                continue;
            }
            int a = 0;
            for (int k = 0; k < 8; ++k) {
                if (p[static_cast<std::size_t>(k)] == 0 &&
                    p[static_cast<std::size_t>((k + 1) % 8)] == 1) {
                    ++a;
                }
            }
            if (a != 1) {
                continue;
            }
            if (step == 0) {
                if (p[0] * p[2] * p[4] != 0)
                    continue;
                if (p[2] * p[4] * p[6] != 0)
                    continue;
            } else {
                if (p[0] * p[2] * p[6] != 0)
                    continue;
                if (p[0] * p[4] * p[6] != 0)
                    continue;
            }
            to_clear.emplace_back(x, y);
        }
    }
    for (const auto& [x, y] : to_clear) {
        g[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] =
            0;
    }
    return !to_clear.empty();
}

RasterMask reference_thin(const RasterMask& mask) {
    RasterMask out = mask;
    if (mask.width <= 0 || mask.height <= 0) {
        return out;
    }
    bool changed = true;
    int guard = 0;
    const int maxIter = mask.width + mask.height + 10;
    while (changed && guard++ < maxIter) {
        const bool c0 = ref_pass(out.pixels, out.width, out.height, 0);
        const bool c1 = ref_pass(out.pixels, out.width, out.height, 1);
        changed = c0 || c1;
    }
    return out;
}

} // namespace

TEST_CASE("thin_zhang_suen optimise : identique a la reference sur tout le corpus de formes",
          "[skeleton][perf]") {
    const std::vector<std::string> names{"rectangle",
                                         "capsule",
                                         "ribbon",
                                         "s",
                                         "y",
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
                                         "deep_recursive",
                                         "multi_neck",
                                         "dumbbell",
                                         "deep_channel",
                                         "two_holes",
                                         "ring_branch",
                                         "junction_with_hole",
                                         "polygonal_cut_fixture",
                                         "thick_diagonal_blob"};
    for (const auto& name : names) {
        const auto shape = make_shape(name);
        REQUIRE(shape.has_value());
        for (const int pixelUm : {50, 100}) {
            SkeletonRasterParameters params;
            params.pixel_size = Micrometers{pixelUm};
            const auto mask = rasterize(*shape, params);
            REQUIRE(mask.has_value());
            INFO("forme = " << name << ", pixel = " << pixelUm << " um");
            const RasterMask fast = thin_zhang_suen(*mask);
            const RasterMask ref = reference_thin(*mask);
            CHECK(fast.width == ref.width);
            CHECK(fast.height == ref.height);
            CHECK(fast.pixels == ref.pixels);
        }
    }
}

TEST_CASE("thin_zhang_suen optimise : identique a la reference sur des masques aleatoires",
          "[skeleton][perf]") {
    // Masques bruités (pixels isolés, bords de grille, diagonales) : cas
    // limites que les formes procédurales ne produisent pas.
    std::mt19937 rng(20260922u);
    for (int trial = 0; trial < 40; ++trial) {
        RasterMask mask;
        mask.width = 8 + static_cast<int>(rng() % 60);
        mask.height = 8 + static_cast<int>(rng() % 60);
        mask.pixels.assign(
            static_cast<std::size_t>(mask.width) * static_cast<std::size_t>(mask.height), 0);
        const unsigned density = 30 + rng() % 60; // en %
        for (auto& p : mask.pixels) {
            p = (rng() % 100) < density ? 1 : 0;
        }
        INFO("essai = " << trial);
        CHECK(thin_zhang_suen(mask).pixels == reference_thin(mask).pixels);
    }
    RasterMask empty;
    CHECK(thin_zhang_suen(empty).pixels.empty());
}

namespace {

void check_same_graph(const SkeletonGraph& a, const SkeletonGraph& b) {
    REQUIRE(a.nodes.size() == b.nodes.size());
    REQUIRE(a.edges.size() == b.edges.size());
    for (std::size_t i = 0; i < a.nodes.size(); ++i) {
        CHECK(a.nodes[i].id == b.nodes[i].id);
        CHECK(a.nodes[i].position == b.nodes[i].position);
        CHECK(a.nodes[i].type == b.nodes[i].type);
        CHECK(a.nodes[i].local_radius_um == b.nodes[i].local_radius_um);
    }
    for (std::size_t i = 0; i < a.edges.size(); ++i) {
        CHECK(a.edges[i].from == b.edges[i].from);
        CHECK(a.edges[i].to == b.edges[i].to);
        CHECK(a.edges[i].centerline == b.edges[i].centerline);
        CHECK(a.edges[i].local_radii_um == b.edges[i].local_radii_um);
        CHECK(a.edges[i].length_um == b.edges[i].length_um);
    }
}

void check_same_analysis(const AutoSatinAnalysis& a, const AutoSatinAnalysis& b) {
    CHECK(a.debug.mask.pixels == b.debug.mask.pixels);
    CHECK(a.debug.distance.distance_um == b.debug.distance.distance_um);
    CHECK(a.debug.skeleton.pixels == b.debug.skeleton.pixels);
    check_same_graph(a.debug.raw_graph, b.debug.raw_graph);
    check_same_graph(a.debug.graph, b.debug.graph);
    CHECK(a.debug.removed_branches.size() == b.debug.removed_branches.size());
    CHECK(a.report.status == b.report.status);
    CHECK(a.report.confidence == b.report.confidence);
    CHECK(a.report.estimated_length_mm == b.report.estimated_length_mm);
    CHECK(a.report.minimum_width_mm == b.report.minimum_width_mm);
    CHECK(a.report.branch_count == b.report.branch_count);
}

} // namespace

TEST_CASE("SkeletonCacheScope : une analyse servie par le cache est identique a un recalcul",
          "[skeleton][perf]") {
    for (const std::string name : {"y", "cross", "ring", "trident", "two_holes", "E"}) {
        const auto shape = make_shape(name);
        REQUIRE(shape.has_value());
        INFO("forme = " << name);
        const AutoSatinParameters params;
        AutoSatinParameters stricter;
        stricter.thresholds.max_satin_width = Micrometers{2000};
        // Références calculées sans aucun scope actif.
        const auto reference = analyze_region(*shape, params);
        const auto referenceStricter = analyze_region(*shape, stricter);
        REQUIRE(reference.has_value());
        REQUIRE(referenceStricter.has_value());

        const SkeletonCacheScope scope;
        const auto miss = analyze_region(*shape, params);
        const auto hit = analyze_region(*shape, params);
        // Seuils différents : l'étape squelette est réutilisée, l'élagage et
        // le rapport sont recalculés avec les nouveaux paramètres.
        const auto hitStricter = analyze_region(*shape, stricter);
        REQUIRE(miss.has_value());
        REQUIRE(hit.has_value());
        REQUIRE(hitStricter.has_value());
        check_same_analysis(*reference, *miss);
        check_same_analysis(*reference, *hit);
        check_same_analysis(*referenceStricter, *hitStricter);
    }
}

TEST_CASE("SkeletonCacheScope : geometrie ou resolution differente jamais confondue",
          "[skeleton][perf]") {
    const auto shape = make_shape("y");
    REQUIRE(shape.has_value());
    geometry::PathSet moved = *shape;
    moved.outer.nodes.front().pos.x = moved.outer.nodes.front().pos.x + Micrometers{1};
    AutoSatinParameters coarse;
    coarse.raster.pixel_size = Micrometers{100};
    const auto movedRef = analyze_region(moved, {});
    const auto coarseRef = analyze_region(*shape, coarse);
    REQUIRE(movedRef.has_value());
    REQUIRE(coarseRef.has_value());

    const SkeletonCacheScope scope;
    REQUIRE(analyze_region(*shape, {}).has_value()); // remplit le cache
    {
        const SkeletonCacheScope nested; // imbriqué : réutilise le cache externe
        const auto movedHit = analyze_region(moved, {});
        REQUIRE(movedHit.has_value());
        check_same_analysis(*movedRef, *movedHit);
    }
    const auto coarseHit = analyze_region(*shape, coarse);
    REQUIRE(coarseHit.has_value());
    check_same_analysis(*coarseRef, *coarseHit);
}
