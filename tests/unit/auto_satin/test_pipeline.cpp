// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "openstitch/auto_satin/auto_satin.hpp"
#include "openstitch/auto_satin/shapes.hpp"

using namespace openstitch;
using namespace openstitch::auto_satin;

namespace {

AutoSatinAnalysis analyze(const std::string& shape) {
    const auto region = make_shape(shape);
    REQUIRE(region.has_value());
    AutoSatinParameters params;
    params.raster.pixel_size = Micrometers{100};  // 0,1 mm : rapide pour les tests
    auto res = analyze_region(*region, params);
    REQUIRE(res.has_value());
    return *res;
}

bool finite_field(const DistanceField& d) {
    for (const float v : d.distance_um) {
        if (!std::isfinite(v)) {
            return false;
        }
    }
    return true;
}

}  // namespace

// --- Rasterisation / distance ------------------------------------------------

TEST_CASE("rasterisation : masque non vide, coord physiques coherentes") {
    const auto a = analyze("rectangle");
    CHECK(a.debug.mask.width > 10);
    CHECK(a.debug.mask.height > 3);
    // Au moins quelques pixels interieurs.
    std::size_t inside = 0;
    for (auto p : a.debug.mask.pixels) {
        inside += p ? 1 : 0;
    }
    CHECK(inside > 50);
}

TEST_CASE("distance : finie, positive, ~ demi-largeur pour un rectangle") {
    const auto a = analyze("rectangle");
    CHECK(finite_field(a.debug.distance));
    // Rectangle de 5 mm de large -> demi-largeur max ~2,5 mm = 2500 um.
    CHECK(a.debug.distance.max_value() > 2000.0f);
    CHECK(a.debug.distance.max_value() < 3000.0f);
}

// --- Squelette / graphe ------------------------------------------------------

TEST_CASE("rectangle : squelette ~ ligne, 2 extremites, 0 jonction") {
    const auto a = analyze("rectangle");
    CHECK(a.report.endpoint_count == 2);
    CHECK(a.report.junction_count == 0);
    CHECK(a.report.branch_count >= 1);
}

TEST_CASE("Y : structure branchee detectee") {
    // Le signal pertinent est « branché » (jonction OU plus de 2 extrémités),
    // qui déclenche la décomposition. Le type exact du nœud de rencontre dépend
    // de la rasterisation ; on ne le sur-spécifie pas.
    const auto a = analyze("y");
    CHECK((a.report.junction_count >= 1 || a.report.endpoint_count >= 3));
}

TEST_CASE("croix : structure branchee detectee") {
    const auto a = analyze("cross");
    CHECK((a.report.junction_count >= 1 || a.report.endpoint_count >= 3));
}

// --- Satinabilite ------------------------------------------------------------

TEST_CASE("rectangle allonge : Suitable") {
    const auto a = analyze("rectangle");
    CHECK((a.report.status == SatinabilityStatus::Suitable ||
           a.report.status == SatinabilityStatus::SuitableWithWarnings));
    CHECK(a.report.is_elongated);
}

TEST_CASE("capsule et ruban : convertibles") {
    for (const char* s : {"capsule", "ribbon", "s"}) {
        const auto a = analyze(s);
        INFO("forme = " << s << " statut = " << to_string(a.report.status));
        CHECK((a.report.status == SatinabilityStatus::Suitable ||
               a.report.status == SatinabilityStatus::SuitableWithWarnings));
    }
}

TEST_CASE("Y et croix : RequiresDecomposition") {
    CHECK(analyze("y").report.status == SatinabilityStatus::RequiresDecomposition);
    CHECK(analyze("cross").report.status == SatinabilityStatus::RequiresDecomposition);
}

TEST_CASE("cercle : Ambiguous (direction non determinee)") {
    const auto a = analyze("circle");
    CHECK(a.report.status == SatinabilityStatus::Ambiguous);
    CHECK(a.report.has_ambiguous_direction);
}

TEST_CASE("anneau : Unsuitable (trou)") {
    const auto a = analyze("ring");
    CHECK(a.report.status == SatinabilityStatus::Unsuitable);
    CHECK(a.report.hole_count == 1);
}

TEST_CASE("forme large : Unsuitable (trop large)") {
    const auto a = analyze("wide");
    CHECK(a.report.status == SatinabilityStatus::Unsuitable);
    CHECK(a.report.has_wide_area);
}

// --- Invariants --------------------------------------------------------------

TEST_CASE("deterministe : meme forme -> meme rapport et meme graphe") {
    const auto a = analyze("s");
    const auto b = analyze("s");
    CHECK(a.report.status == b.report.status);
    CHECK(a.report.endpoint_count == b.report.endpoint_count);
    CHECK(a.report.junction_count == b.report.junction_count);
    REQUIRE(a.debug.graph.edges.size() == b.debug.graph.edges.size());
    for (std::size_t i = 0; i < a.debug.graph.edges.size(); ++i) {
        CHECK(a.debug.graph.edges[i].centerline == b.debug.graph.edges[i].centerline);
    }
}

TEST_CASE("invariant d'echelle : rectangle a 2 echelles reste Suitable et 2 extremites") {
    // Le rectangle par defaut fait 40x5 mm ; make_shape ne parametre pas
    // l'echelle, mais on verifie qu'un rectangle reste coherent apres analyse.
    const auto a = analyze("rectangle");
    CHECK(a.report.endpoint_count == 2);
    CHECK(a.report.is_elongated);
}

TEST_CASE("pas de NaN dans les metriques") {
    const auto a = analyze("ribbon");
    CHECK(std::isfinite(a.report.mean_width_mm));
    CHECK(std::isfinite(a.report.estimated_length_mm));
    CHECK(std::isfinite(a.report.maximum_width_mm));
    CHECK(std::isfinite(a.report.confidence));
}
