// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <numeric>
#include <string>

#include "openstitch/auto_satin/auto_satin.hpp"
#include "openstitch/auto_satin/shapes.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/satin_planning/region_split.hpp"

using namespace openstitch;
using namespace openstitch::satin_planning;

namespace {

auto_satin::AutoSatinAnalysis analyze(const std::string& shapeName, geometry::PathSet& shapeOut) {
    const auto shape = auto_satin::make_shape(shapeName);
    REQUIRE(shape.has_value());
    shapeOut = *shape;
    auto analysis = auto_satin::analyze_region(*shape, {});
    REQUIRE(analysis.has_value());
    return std::move(*analysis);
}

}  // namespace

TEST_CASE("split_region : rectangle -- aucune jonction, region inchangee") {
    geometry::PathSet shape;
    const auto analysis = analyze("rectangle", shape);
    const auto& graph = analysis.debug.graph;
    const DecompositionReport decomposition = decompose_into_paths(graph);
    REQUIRE(decomposition.paths.size() == 1);

    const RegionSplitReport split = split_region(shape, graph, decomposition);
    REQUIRE(split.regions.size() == 1);
    CHECK(split.unresolved_paths.empty());
    CHECK(split.cuts.empty());  // aucune jonction => aucun evenement de detachement

    const double originalAreaMm2 = geometry::path_set_area_um2(shape) / 1e6;
    CHECK(split.regions.front().area_mm2 == Catch::Approx(originalAreaMm2).margin(0.01));
    CHECK(split.regions.front().path_index == 0);
}

TEST_CASE("split_region : T -- une coupe separe le pied de la barre") {
    geometry::PathSet shape;
    const auto analysis = analyze("t", shape);
    const auto& graph = analysis.debug.graph;
    const DecompositionReport decomposition = decompose_into_paths(graph);
    REQUIRE(decomposition.paths.size() == 2);

    const RegionSplitReport split = split_region(shape, graph, decomposition);
    CHECK(split.cuts.size() == 1);  // un seul arc detache a l'unique jonction

    INFO(format_region_split_report(split, decomposition));
    if (!split.cuts.empty()) {
        CHECK(split.cuts.front().selected.has_value());
    }
    // Aucune coupe ne peut faire APPARAITRE de matiere : la somme des
    // regions (resolues + non isolees, encore fusionnees dans une region
    // parente comptee une fois) ne depasse jamais l'aire d'origine.
    double sumResolved = std::accumulate(split.regions.begin(), split.regions.end(), 0.0,
                                          [](double s, const SatinRegion& r) { return s + r.area_mm2; });
    const double originalAreaMm2 = geometry::path_set_area_um2(shape) / 1e6;
    CHECK(sumResolved <= originalAreaMm2 + 0.5);
    CHECK(split.regions.size() + split.unresolved_paths.size() == decomposition.paths.size());
}

TEST_CASE("split_region : propriete generale sur le corpus de formes branchees") {
    for (const std::string& name : {"t", "y", "cross", "h", "trident"}) {
        INFO("forme = " << name);
        geometry::PathSet shape;
        const auto analysis = analyze(name, shape);
        const auto& graph = analysis.debug.graph;
        const DecompositionReport decomposition = decompose_into_paths(graph);
        const RegionSplitReport split = split_region(shape, graph, decomposition);

        CHECK(split.regions.size() + split.unresolved_paths.size() == decomposition.paths.size());

        double sumResolved = 0.0;
        for (const auto& r : split.regions) {
            CHECK(r.area_mm2 > 0.0);
            sumResolved += r.area_mm2;
        }
        const double originalAreaMm2 = geometry::path_set_area_um2(shape) / 1e6;
        // Une coupe ne fait que retirer une fine bande : jamais de gain net
        // de matiere, marge generreuse pour l'aire cumulee des bandes retirees.
        CHECK(sumResolved <= originalAreaMm2 + 1.0);

        // Chaque path_index assigne est valide et unique.
        std::vector<bool> seen(decomposition.paths.size(), false);
        for (const auto& r : split.regions) {
            REQUIRE(r.path_index < decomposition.paths.size());
            CHECK_FALSE(seen[r.path_index]);
            seen[r.path_index] = true;
        }
    }
}

TEST_CASE("format_region_split_report : rendu textuel exploitable pour le debug") {
    geometry::PathSet shape;
    const auto analysis = analyze("t", shape);
    const auto& graph = analysis.debug.graph;
    const DecompositionReport decomposition = decompose_into_paths(graph);
    const RegionSplitReport split = split_region(shape, graph, decomposition);

    const std::string text = format_region_split_report(split, decomposition);
    CHECK_FALSE(text.empty());
    CHECK(text.find("Regions :") != std::string::npos);
    CHECK(text.find("Total :") != std::string::npos);
}
