// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "openstitch/auto_satin/satin_column.hpp"
#include "openstitch/auto_satin/shapes.hpp"
#include "openstitch/satin_coverage/coverage.hpp"

using namespace openstitch;
using namespace openstitch::auto_satin;

namespace {

satin_coverage::SatinColumnInput to_input(const SatinColumnGeometry& col) {
    satin_coverage::SatinColumnInput in;
    in.rail_a = col.rail_a;
    in.rail_b = col.rail_b;
    in.rungs.reserve(col.rungs.size());
    for (const auto& r : col.rungs) {
        in.rungs.emplace_back(r.a, r.b);
    }
    in.density = Micrometers{400};
    return in;
}

struct Baseline {
    std::string shape;
    double min_raw_coverage;  // plancher observe, marge de securite incluse
};

}  // namespace

TEST_CASE("couverture satin : non-regression sur le corpus de formes historiques") {
    // Photographie la couverture geometrique ACTUELLE de chaque forme du
    // corpus de test auto_satin (cf. shapes.cpp) -- ne verifie PAS que la
    // couverture est parfaite (les residus de noyau de jonction sont un fait
    // deja documente, cf. docs/source/satin.md), seulement qu'elle ne se
    // degrade pas SILENCIEUSEMENT sous son niveau actuel. Formes
    // volontairement exclues : circle/tiny/notch/pinch/wide, qui refusent
    // ENTIEREMENT par construction (testees ailleurs pour ce comportement
    // precis, pas pour leur couverture) -- wide en particulier est un
    // rectangle 100x20mm delibere ("un vrai cas trop large pour satin",
    // shapes.cpp) : sa largeur (20mm) depasse max_satin_width (9mm par
    // defaut, satinability.cpp:76) et declenche `Unsuitable` par conception,
    // pas un defaut (verifie en calibrant ce test -- l'hypothese initiale
    // d'un refus surprenant etait une lecture trop rapide de shapes.cpp).
    //
    // Planchers = valeur mesuree lors de la calibration de ce test, moins
    // une marge de securite de quelques points -- un vrai plancher de
    // non-regression, pas un objectif de qualite (les residus de noyau de
    // jonction sur y/t/cross/h/trident sont un fait deja documente).
    const std::vector<Baseline> baselines = {
        {"rectangle", 0.94}, {"capsule", 0.89}, {"ribbon", 0.90}, {"s", 0.95},
        {"y", 0.85},        {"t", 0.86},       {"cross", 0.83},  {"h", 0.83},
        {"ring", 0.99},     {"trident", 0.86},
    };
    for (const auto& baseline : baselines) {
        DYNAMIC_SECTION("forme : " << baseline.shape) {
            const auto region = make_shape(baseline.shape);
            REQUIRE(region.has_value());
            SatinColumnsParameters params;
            params.analysis.raster.pixel_size = Micrometers{100};
            const auto network = build_satin_columns(*region, params);
            INFO("refus: " << network.refusal);
            REQUIRE_FALSE(network.columns.empty());

            std::vector<satin_coverage::SatinColumnInput> columns;
            columns.reserve(network.columns.size());
            for (const auto& col : network.columns) {
                columns.push_back(to_input(col));
            }
            const auto report = satin_coverage::analyze_satin_coverage(*region, columns);
            REQUIRE(report.has_value());
            INFO("couverture brute: " << report->raw_coverage_ratio * 100.0
                                      << "% (plancher: " << baseline.min_raw_coverage * 100.0
                                      << "%), plus grande zone manquante: "
                                      << report->largest_missing_area_mm2 << " mm2");
            CHECK(report->raw_coverage_ratio >= baseline.min_raw_coverage);
        }
    }
}
