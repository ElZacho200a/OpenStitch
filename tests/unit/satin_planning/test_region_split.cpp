// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <numeric>
#include <string>
#include <utility>

#include "openstitch/auto_satin/auto_satin.hpp"
#include "openstitch/auto_satin/satin_column.hpp"
#include "openstitch/auto_satin/shapes.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/polyline.hpp"
#include "openstitch/satin_planning/beam_search.hpp"
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

TEST_CASE("split_region : notch -- chemin unique sans jonction resolu malgre une forte courbure") {
    // Non-regression (2026-08-13, trouve via la commande CLI sgsd-debug) :
    // `probe_point` retombait, faute de noeud interne (un seul arc, deux
    // noeuds), sur le MILIEU DROIT entre les deux extremites du chemin --
    // une corde qui sort de la forme des que le trace courbe suffisamment
    // (ici autour d'une entaille concave profonde), faisant echouer
    // l'assignation d'un chemin pourtant trivialement resolu (aucune
    // jonction, aucune coupe necessaire). Corrige en prenant le point
    // directement sur la centerline de l'arc median, toujours interieur par
    // construction.
    geometry::PathSet shape;
    const auto analysis = analyze("notch", shape);
    const auto& graph = analysis.debug.graph;
    const DecompositionReport decomposition = decompose_into_paths(graph);
    REQUIRE(decomposition.paths.size() == 1);
    REQUIRE(graph.junction_count() == 0);

    const RegionSplitReport split = split_region(shape, graph, decomposition);
    CHECK(split.cuts.empty());
    REQUIRE(split.unresolved_paths.empty());
    REQUIRE(split.regions.size() == 1);
    const double originalAreaMm2 = geometry::path_set_area_um2(shape) / 1e6;
    CHECK(split.regions.front().area_mm2 == Catch::Approx(originalAreaMm2).margin(0.01));
}

TEST_CASE("split_region : propriete generale sur le corpus de formes branchees et courbes") {
    for (const std::string& name : {"t", "y", "cross", "h", "trident", "s", "ribbon", "notch", "wide"}) {
        INFO("forme = " << name);
        geometry::PathSet shape;
        const auto analysis = analyze(name, shape);
        const auto& graph = analysis.debug.graph;
        const DecompositionReport decomposition = decompose_into_paths(graph);
        const RegionSplitReport split = split_region(shape, graph, decomposition);

        CHECK(split.regions.size() + split.unresolved_paths.size() == decomposition.paths.size());
        // Sans jonction, aucune coupe n'est jamais tentee : le chemin unique
        // doit TOUJOURS se resoudre (regle stricte qui aurait attrape le
        // defaut `notch` ci-dessus si elle avait deja existe).
        if (graph.junction_count() == 0) CHECK(split.unresolved_paths.empty());

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

TEST_CASE("region reelle avec boucle -- SGSD accepte chaque sous-region mais laisse un reliquat massif non couvert") {
    // Coordonnees exactes exportees depuis l'application (export debug) sur
    // une forme utilisateur reelle (lettre avec contre-poincon) qui
    // apparaissait visiblement sans aucun point (ni satin ni repli tatami)
    // sur une grande partie de sa surface -- defaut trouve et corrige le
    // 2026-08-14 (§ docs/source/satin.md « SGSD exclusif »). Root cause :
    // les 4 chemins SGSD sont tous isoles ET acceptes (chacun produit au
    // moins une colonne, donc AUCUN signal structurel de refus/non-isolation)
    // -- mais la sous-region contenant la boucle (un contre-poincon,
    // topologie proche d'un anneau) n'est approximee que tres partiellement
    // par un unique ruban satin. L'ancien signal de repli
    // (`AutoOptions`/`build_satin_sections::structural_gap`, avant
    // correctif) ne mesurait qu'un refus/une non-isolation structurelle,
    // jamais la qualite reelle de l'ajustement -- ce test le prouve en
    // mesurant directement le reliquat geometrique reel malgre un succes
    // structurel complet (regions=4, non_isoles=0).
    geometry::PathSet shape;
    shape.outer.closed = true;
    for (auto [x, y] : std::vector<std::pair<int, int>>{
             {-62278, 51151}, {-62576, 51301}, {-63174, 50853}, {-64219, 51151}, {-66310, 53093},
             {-66011, 53690}, {-60336, 53093}, {-58544, 55333}, {-58843, 55632}, {-64219, 55930},
             {-64667, 56378}, {-64219, 57722}, {-64368, 58768}, {-65862, 60261}, {-67206, 60261},
             {-68401, 59515}, {-71238, 56677}, {-73329, 53839}, {-71985, 54437}, {-69745, 52794},
             {-65563, 48463}, {-65713, 47268}}) {
        shape.outer.nodes.push_back({Vec2um{Micrometers{x}, Micrometers{y}}, geometry::NodeType::Corner});
    }
    geometry::Path hole;
    hole.closed = true;
    for (auto [x, y] : std::vector<std::pair<int, int>>{
             {-68849, 55184}, {-70342, 56826}, {-69745, 57573}, {-67953, 58171},
             {-67206, 57872}, {-65862, 56229}, {-66011, 55034}, {-67057, 53839}}) {
        hole.nodes.push_back({Vec2um{Micrometers{x}, Micrometers{y}}, geometry::NodeType::Corner});
    }
    shape.holes.push_back(hole);

    // Memes reglages que la production (autodigitize.cpp::auto_digitize) : mode
    // Parametric, max_satin_width=6000um (correspond au SatinParams exporte par
    // l'utilisateur, max_width=6000).
    auto_satin::SatinColumnsParameters prodParams;
    prodParams.analysis.thresholds.max_satin_width = Micrometers{6'000};
    prodParams.geometry_mode = auto_satin::SatinGeometryMode::Parametric;

    const auto analysis = auto_satin::analyze_region(shape, prodParams.analysis);
    REQUIRE(analysis.has_value());
    const auto& graph = analysis->debug.graph;
    WARN("jonctions=" << graph.junction_count() << " extremites=" << graph.endpoint_count()
                       << " arcs=" << graph.edges.size());

    const DecompositionReport decomposition = decompose_into_paths(graph);
    WARN("chemins=" << decomposition.paths.size());

    satin_planning::BeamSearchParams beamParams;
    beamParams.beam_width = 3;
    beamParams.genParams = prodParams;
    satin_planning::OracleGuidedSelector selector(beamParams);
    satin_planning::CutCandidateParams cutParams;
    cutParams.selector = std::ref(selector);

    const RegionSplitReport split = split_region(shape, graph, decomposition, cutParams);
    INFO(format_region_split_report(split, decomposition));

    // Succes structurel COMPLET : les 4 chemins sont isoles, aucun ne reste
    // fusionne avec un voisin.
    REQUIRE(split.regions.size() == 4);
    CHECK(split.unresolved_paths.empty());

    std::size_t acceptedCount = 0;
    std::vector<geometry::Path> strips;
    for (const auto& r : split.regions) {
        const auto built = auto_satin::build_satin_columns(r.region, prodParams);
        const std::size_t count =
            !built.parametric_columns.empty() ? built.parametric_columns.size() : built.columns.size();
        INFO("chemin " << r.path_index << " (" << r.area_mm2 << "mm2) -> " << count << " colonne(s), statut="
                        << auto_satin::to_string(built.report.status));
        if (count > 0) ++acceptedCount;
        for (const auto& col : built.parametric_columns) {
            const auto flatA = geometry::flatten(col.rail_a, Micrometers{30});
            const auto flatB = geometry::flatten(col.rail_b, Micrometers{30});
            geometry::Path strip;
            strip.closed = true;
            for (const auto& p : flatA.points) strip.nodes.push_back({p, geometry::NodeType::Corner});
            for (auto it = flatB.points.rbegin(); it != flatB.points.rend(); ++it)
                strip.nodes.push_back({*it, geometry::NodeType::Corner});
            strips.push_back(std::move(strip));
        }
    }
    // Chaque sous-region PRODUIT au moins une colonne : aucun signal
    // structurel de refus ne pourrait alerter l'appelant.
    REQUIRE(acceptedCount == split.regions.size());

    // Et pourtant, la surface REELLEMENT couverte par ces colonnes (mesuree
    // geometriquement, jamais par un comptage de colonnes) laisse un reliquat
    // massif -- la preuve que le signal structurel seul est insuffisant, et
    // la justification du correctif de mesure reelle dans
    // `autodigitize::build_satin_sections`.
    const double originalAreaMm2 = geometry::path_set_area_um2(shape) / 1e6;
    const auto leftover = geometry::subtract_polygons(shape, strips);
    REQUIRE(leftover.has_value());
    double leftoverAreaMm2 = 0.0;
    for (const auto& piece : *leftover) leftoverAreaMm2 += geometry::path_set_area_um2(piece) / 1e6;
    INFO("aire totale=" << originalAreaMm2 << "mm2 -- reliquat non couvert=" << leftoverAreaMm2 << "mm2");
    CHECK(leftoverAreaMm2 > 0.3 * originalAreaMm2);
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
