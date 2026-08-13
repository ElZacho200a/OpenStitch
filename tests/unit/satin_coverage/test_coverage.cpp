// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <vector>

#include "openstitch/auto_satin/satin_column.hpp"
#include "openstitch/auto_satin/shapes.hpp"
#include "openstitch/satin_coverage/coverage.hpp"

using namespace openstitch;
using namespace openstitch::satin_coverage;
using Catch::Approx;

namespace {

geometry::Path open_path(const std::vector<Vec2um>& pts) {
    geometry::Path p;
    p.closed = false;
    for (const auto& pt : pts) {
        p.nodes.push_back({pt, geometry::NodeType::Corner, std::nullopt, std::nullopt});
    }
    return p;
}

geometry::Path closed_rect(std::int32_t x0, std::int32_t x1, std::int32_t y0, std::int32_t y1) {
    geometry::Path p;
    p.closed = true;
    p.nodes = {
        {Vec2um{Micrometers{x0}, Micrometers{y0}}, geometry::NodeType::Corner, std::nullopt, std::nullopt},
        {Vec2um{Micrometers{x1}, Micrometers{y0}}, geometry::NodeType::Corner, std::nullopt, std::nullopt},
        {Vec2um{Micrometers{x1}, Micrometers{y1}}, geometry::NodeType::Corner, std::nullopt, std::nullopt},
        {Vec2um{Micrometers{x0}, Micrometers{y1}}, geometry::NodeType::Corner, std::nullopt, std::nullopt},
    };
    return p;
}

geometry::PathSet rect_target(std::int32_t widthUm, std::int32_t heightUm) {
    return geometry::PathSet{closed_rect(0, widthUm, 0, heightUm), {}};
}

// `closed_rect` produit toujours le même sens de parcours (CCW, comme un
// contour extérieur) -- un trou doit être orienté À L'OPPOSÉ (CW) pour que
// les opérations sensibles à l'orientation (ex. `inset_path_set`, qui offsette
// chaque contour du PathSet dans le même sens sans jamais le renormaliser)
// traitent correctement le trou comme un retrait de matière, pas comme un
// second contour plein.
geometry::Path reversed(geometry::Path p) {
    std::reverse(p.nodes.begin(), p.nodes.end());
    return p;
}

// Colonne satin rectangulaire triviale : deux rails verticaux, deux barreaux
// (haut/bas) -- une seule station -> un unique quadrilatère de couverture.
SatinColumnInput rect_column(std::int32_t x0, std::int32_t x1, std::int32_t y0, std::int32_t y1) {
    SatinColumnInput col;
    col.rail_a = open_path({{Micrometers{x0}, Micrometers{y0}}, {Micrometers{x0}, Micrometers{y1}}});
    col.rail_b = open_path({{Micrometers{x1}, Micrometers{y0}}, {Micrometers{x1}, Micrometers{y1}}});
    col.rungs = {
        {Vec2um{Micrometers{x0}, Micrometers{y0}}, Vec2um{Micrometers{x1}, Micrometers{y0}}},
        {Vec2um{Micrometers{x0}, Micrometers{y1}}, Vec2um{Micrometers{x1}, Micrometers{y1}}},
    };
    col.density = Micrometers{400};
    return col;
}

SatinColumnInput to_input(const auto_satin::SatinColumnGeometry& col) {
    SatinColumnInput in;
    in.rail_a = col.rail_a;
    in.rail_b = col.rail_b;
    in.rungs.reserve(col.rungs.size());
    for (const auto& r : col.rungs) {
        in.rungs.emplace_back(r.a, r.b);
    }
    in.density = Micrometers{400};
    return in;
}

}  // namespace

TEST_CASE("couverture satin : rectangle entierement couvert -> proche de 100%, PASS") {
    const auto target = rect_target(20'000, 5'000);  // 20 x 5 mm
    const std::vector<SatinColumnInput> columns{rect_column(0, 20'000, 0, 5'000)};
    const auto report = analyze_satin_coverage(target, columns);
    REQUIRE(report.has_value());
    CHECK(report->target_area_mm2 == Approx(100.0).margin(0.02));
    CHECK(report->raw_coverage_ratio > 0.999);
    CHECK(report->core_coverage_ratio > 0.999);
    CHECK(report->missing_area_mm2 < 0.02);
    CHECK(report->missing_regions.empty());
    CHECK(report->outside_area_mm2 < 0.02);
    CHECK(report->passed);
}

TEST_CASE("couverture satin : rectangle couvert a moitie -> ~50%, FAIL") {
    const auto target = rect_target(20'000, 5'000);
    const std::vector<SatinColumnInput> columns{rect_column(0, 10'000, 0, 5'000)};  // moitie gauche
    const auto report = analyze_satin_coverage(target, columns);
    REQUIRE(report.has_value());
    CHECK(report->raw_coverage_ratio == Approx(0.5).margin(0.01));
    CHECK_FALSE(report->passed);
    REQUIRE(report->missing_regions.size() == 1);
    CHECK(report->missing_regions.front().area_mm2 == Approx(50.0).margin(0.5));
    CHECK(report->largest_missing_area_mm2 == Approx(50.0).margin(0.5));
}

TEST_CASE("couverture satin : trou de la forme source jamais compte comme manquant") {
    // Anneau : grand rectangle 20x10mm troue d'un rectangle 4x6mm centre.
    // Quatre colonnes tuilent exactement l'anneau (aucun recouvrement,
    // aucun trou de tuilage) -- la couverture doit etre ~complete malgre le
    // trou de la forme source.
    const geometry::Path hole = reversed(closed_rect(8'000, 12'000, 2'000, 8'000));
    const geometry::PathSet target{closed_rect(0, 20'000, 0, 10'000), {hole}};
    const std::vector<SatinColumnInput> columns{
        rect_column(0, 8'000, 0, 10'000),        // bande gauche
        rect_column(12'000, 20'000, 0, 10'000),  // bande droite
        rect_column(8'000, 12'000, 8'000, 10'000),  // bande haute (au-dessus du trou)
        rect_column(8'000, 12'000, 0, 2'000),       // bande basse (au-dessous du trou)
    };
    const auto report = analyze_satin_coverage(target, columns);
    REQUIRE(report.has_value());
    CHECK(report->target_area_mm2 == Approx(176.0).margin(0.05));  // 200 - 24 (trou)
    CHECK(report->raw_coverage_ratio > 0.999);
    CHECK(report->missing_area_mm2 < 0.05);
    CHECK(report->passed);
}

TEST_CASE("couverture satin : vraie poche non couverte entouree de matiere -> FAIL, gap radius significatif") {
    // Meme decoupage en quatre bandes que le test precedent, MAIS la cible
    // est le rectangle PLEIN (sans trou declare) : la zone laissee sans
    // colonne est donc une vraie poche manquante, entierement entouree de
    // couverture, jamais un trou legitime de la forme source.
    const auto target = rect_target(20'000, 10'000);
    const std::vector<SatinColumnInput> columns{
        rect_column(0, 8'000, 0, 10'000),
        rect_column(12'000, 20'000, 0, 10'000),
        rect_column(8'000, 12'000, 8'000, 10'000),
        rect_column(8'000, 12'000, 0, 2'000),
    };
    const auto report = analyze_satin_coverage(target, columns);
    REQUIRE(report.has_value());
    CHECK(report->missing_area_mm2 == Approx(24.0).margin(0.1));  // 4mm x 6mm
    CHECK_FALSE(report->passed);
    REQUIRE(report->missing_regions.size() == 1);
    CHECK(report->missing_regions.front().area_mm2 == Approx(24.0).margin(0.1));
    // Rayon inscrit d'un rectangle 4x6mm : la moitie du petit cote, 2mm.
    CHECK(report->missing_regions.front().max_gap_radius_mm == Approx(2.0).margin(0.05));
    CHECK(report->max_gap_radius_mm == Approx(2.0).margin(0.05));
}

TEST_CASE("couverture satin : debordement hors de la forme cible -> outside_area > 0") {
    const auto target = rect_target(10'000, 5'000);  // 10 x 5 mm
    // La colonne deborde de 3mm a droite de la cible.
    const std::vector<SatinColumnInput> columns{rect_column(0, 13'000, 0, 5'000)};
    const auto report = analyze_satin_coverage(target, columns);
    REQUIRE(report.has_value());
    CHECK(report->outside_area_mm2 == Approx(15.0).margin(0.1));  // 3mm x 5mm
    CHECK(report->outside_ratio > 0.0);
    CHECK(report->raw_coverage_ratio > 0.999);  // la cible elle-meme reste entierement couverte
}

TEST_CASE("couverture satin : deux colonnes qui se chevauchent -> l'union fait foi, pas de double comptage") {
    const auto target = rect_target(20'000, 5'000);
    const std::vector<SatinColumnInput> columns{
        rect_column(0, 15'000, 0, 5'000),
        rect_column(5'000, 20'000, 0, 5'000),  // chevauche la precedente sur [5000,15000]
    };
    const auto report = analyze_satin_coverage(target, columns);
    REQUIRE(report.has_value());
    CHECK(report->covered_area_mm2 == Approx(100.0).margin(0.02));  // pas 150
    CHECK(report->raw_coverage_ratio > 0.999);
    CHECK(report->passed);
}

TEST_CASE("couverture satin : trident, branche manquante detectee comme un vrai defaut de couverture") {
    // Formes historiquement problematiques pour Auto-Satin (cf. docs/source/satin.md,
    // audit jonctions branchees/concaves) : verifie que l'analyseur de couverture
    // distingue nettement "toutes les branches presentes" de "une branche absente",
    // plutot que de se contenter de compter les branches/stations traitees.
    const auto region = auto_satin::make_shape("trident");
    REQUIRE(region.has_value());
    auto_satin::SatinColumnsParameters params;
    params.analysis.raster.pixel_size = Micrometers{100};
    const auto network = auto_satin::build_satin_columns(*region, params);
    REQUIRE(network.columns.size() >= 2);  // decomposition Legacy multi-branches

    std::vector<SatinColumnInput> allColumns;
    for (const auto& col : network.columns) {
        allColumns.push_back(to_input(col));
    }
    const auto fullReport = analyze_satin_coverage(*region, allColumns);
    REQUIRE(fullReport.has_value());

    // Les trois branches d'un trident ont des tailles tres differentes (grande
    // branche verticale epaisse / branche interne pointue / branche laterale
    // etroite, cf. docs/source/satin.md). Retirer TOUJOURS l'index 0 testerait
    // au hasard une branche parfois minuscule -- on retire ici, une a la fois,
    // chaque branche et on ne juge la degradation que sur celle dont l'absence
    // penalise le plus la couverture (la branche principale), pour un test
    // robuste au tri interne de `network.columns`.
    double worstRawCoverage = fullReport->raw_coverage_ratio;
    double worstLargestMissing = fullReport->largest_missing_area_mm2;
    bool worstPassed = fullReport->passed;
    for (std::size_t dropped = 0; dropped < allColumns.size(); ++dropped) {
        std::vector<SatinColumnInput> variant;
        variant.reserve(allColumns.size() - 1);
        for (std::size_t i = 0; i < allColumns.size(); ++i) {
            if (i != dropped) variant.push_back(allColumns[i]);
        }
        const auto variantReport = analyze_satin_coverage(*region, variant);
        REQUIRE(variantReport.has_value());
        if (variantReport->raw_coverage_ratio < worstRawCoverage) {
            worstRawCoverage = variantReport->raw_coverage_ratio;
            worstLargestMissing = variantReport->largest_missing_area_mm2;
            worstPassed = variantReport->passed;
        }
    }

    INFO("couverture complete: " << fullReport->raw_coverage_ratio * 100.0
                                 << " %, plus grande zone manquante (complete): "
                                 << fullReport->largest_missing_area_mm2
                                 << " mm2 ; pire branche retiree -> couverture: " << worstRawCoverage * 100.0
                                 << " %, plus grande zone manquante: " << worstLargestMissing << " mm2");

    // Le retrait de la branche principale doit degrader la couverture de
    // facon tres nette (pas une baisse marginale de bruit d'arrondi), et
    // faire nettement grossir la plus grande zone manquante par rapport au
    // residu deja present (noyau de jonction) meme avec toutes les colonnes.
    CHECK(fullReport->raw_coverage_ratio - worstRawCoverage > 0.10);
    CHECK(worstLargestMissing > fullReport->largest_missing_area_mm2 + 1.0);
    CHECK_FALSE(worstPassed);
}
