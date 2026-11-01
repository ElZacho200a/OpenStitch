// SPDX-License-Identifier: Apache-2.0
//
// Pièce-test de calibration du tirage (HP-ENG-001.a) : protocole de mesure déterministe, tableau
// prédit / mesuré et ajustement. Valide la mécanique (indices, nominaux, CSV, retrouver des
// coefficients connus), jamais une efficacité textile réelle : aucune mesure n'est incorporée.
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <set>
#include <string>

#include "openstitch/stitch_generation/pull_test_piece.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

TEST_CASE("pull_test_piece: generation deterministe", "[pull_test_piece]") {
    const PullTestPiece a = build_pull_test_piece();
    const PullTestPiece b = build_pull_test_piece();
    CHECK(a.sequence.commands == b.sequence.commands);
    REQUIRE(a.metrics.size() == b.metrics.size());
    for (std::size_t i = 0; i < a.metrics.size(); ++i) {
        CHECK(a.metrics[i].id == b.metrics[i].id);
        CHECK(a.metrics[i].first == b.metrics[i].first);
        CHECK(a.metrics[i].second == b.metrics[i].second);
        CHECK(a.metrics[i].nominal_mm == b.metrics[i].nominal_mm);
    }
}

TEST_CASE("pull_test_piece: les metriques pointent des penetrations cousues", "[pull_test_piece]") {
    const PullTestPiece piece = build_pull_test_piece();
    REQUIRE(piece.metrics.size() >= 16);
    std::set<std::string> ids;
    for (const PullMetric& m : piece.metrics) {
        CHECK(ids.insert(m.id).second); // identifiants uniques
        REQUIRE(m.first < piece.sequence.commands.size());
        REQUIRE(m.second < piece.sequence.commands.size());
        CHECK(piece.sequence.commands[m.first].type == stitch::CommandType::Stitch);
        CHECK(piece.sequence.commands[m.second].type == stitch::CommandType::Stitch);
        // Le nominal est la distance dessinee ; seuls les jours d'ordre valent zero.
        if (m.variable == "ordre") {
            CHECK(m.nominal_mm < 0.01);
        } else {
            CHECK(m.nominal_mm > 1.0);
        }
    }
}

TEST_CASE("pull_test_piece: les largeurs nominales des satins sont celles du dessin",
          "[pull_test_piece]") {
    const PullTestPiece piece = build_pull_test_piece();
    const auto nominal = [&](const std::string& id) {
        for (const PullMetric& m : piece.metrics) {
            if (m.id == id) {
                return m.nominal_mm;
            }
        }
        return -1.0;
    };
    CHECK(std::abs(nominal("satin_w3_width") - 3.0) < 0.01);
    CHECK(std::abs(nominal("satin_w12_width") - 12.0) < 0.01);
    CHECK(std::abs(nominal("grain_a45_width") - 4.0) < 0.01);
    CHECK(std::abs(nominal("ref_span_bottom") - 120.0) < 0.01);
}

TEST_CASE("pull_test_piece: sans tirage le predit egale le nominal", "[pull_test_piece]") {
    const PullTestPiece piece = build_pull_test_piece();
    FabricResponse none;
    none.gain = 0.0;
    none.poisson = 0.0;
    for (const PullMetricResult& r : predict_pull_metrics(piece, none)) {
        CHECK(std::abs(r.predicted_mm - r.metric.nominal_mm) < 0.002);
    }
}

TEST_CASE("pull_test_piece: le tirage retrecit les largeurs predites", "[pull_test_piece]") {
    const PullTestPiece piece = build_pull_test_piece();
    int shrunk = 0;
    for (const PullMetricResult& r : predict_pull_metrics(piece, FabricResponse{})) {
        if (r.metric.id.ends_with("_width") && r.predicted_mm < r.metric.nominal_mm - 1e-6) {
            ++shrunk;
        }
    }
    CHECK(shrunk >= 4);
}

TEST_CASE("pull_test_piece: le contour cousu apres ouvre un jour, pas celui cousu avant",
          "[pull_test_piece]") {
    const PullTestPiece piece = build_pull_test_piece();
    double fillFirst = 0.0;
    double outlineFirst = 0.0;
    for (const PullMetricResult& r : predict_pull_metrics(piece, FabricResponse{})) {
        if (r.metric.id == "order_fill_then_outline_gap") {
            fillFirst = r.predicted_mm;
        } else if (r.metric.id == "order_outline_then_fill_gap") {
            outlineFirst = r.predicted_mm;
        }
    }
    // L'ordre de couture change le resultat : les deux jours ne sont pas identiques.
    CHECK(std::abs(fillFirst - outlineFirst) > 1e-4);
}

TEST_CASE("pull_test_piece: csv aller-retour des mesures", "[pull_test_piece]") {
    const PullTestPiece piece = build_pull_test_piece();
    auto results = predict_pull_metrics(piece, FabricResponse{});
    const std::string csv = pull_metrics_csv(results);
    CHECK(csv.starts_with("id;description;variable;nominal_mm;predicted_mm;measured_mm;error_mm"));

    std::string filled = "id;description;variable;nominal_mm;predicted_mm;measured_mm;error_mm\n";
    filled += "satin_w5_width;x;gain;5;5;4,87;\n"; // virgule decimale tolerée
    filled += "ref_span_bottom;x;global;120;120;119.4;\n";
    filled += "inconnu;x;gain;1;1;2;\n";          // ignoré
    filled += "satin_w3_width;x;gain;3;3;;\n";    // vide : ignoré
    filled += "satin_w8_width;x;gain;8;8;abc;\n"; // non numérique : ignoré
    CHECK(apply_measurements_csv(results, filled) == 2);
    int measured = 0;
    for (const PullMetricResult& r : results) {
        if (r.has_measure) {
            ++measured;
            CHECK(std::abs(r.error_mm - (r.predicted_mm - r.measured_mm)) < 1e-9);
        }
    }
    CHECK(measured == 2);
    CHECK(pull_metrics_rms(results) > 0.0);
}

TEST_CASE("pull_test_piece: l'ajustement retrouve des coefficients connus", "[pull_test_piece]") {
    const PullTestPiece piece = build_pull_test_piece();
    // « Mesures » synthetiques produites par un tissu connu (valeurs de la grille d'ajustement).
    FabricResponse truth;
    truth.gain = 0.03;
    truth.compression = 0.12;
    truth.grain_anisotropy = 1.3;
    auto measured = predict_pull_metrics(piece, truth);
    for (PullMetricResult& r : measured) {
        r.has_measure = true;
        r.measured_mm = r.predicted_mm;
    }
    const PullFit fit = fit_pull_response(piece, measured, FabricResponse{});
    CHECK(fit.enough_data);
    CHECK(fit.rms_before_mm > fit.rms_after_mm);
    CHECK(fit.rms_after_mm < 0.002);
    CHECK(std::abs(fit.response.gain - 0.03) < 1e-9);
    CHECK(std::abs(fit.response.compression - 0.12) < 1e-9);
    CHECK(std::abs(fit.response.grain_anisotropy - 1.3) < 1e-9);
    CHECK_FALSE(fit.response.calibrated); // l'appelant décide de valider la calibration
    // Deterministe.
    CHECK(fit_pull_response(piece, measured, FabricResponse{}).response.gain == fit.response.gain);
}

TEST_CASE("pull_test_piece: sans mesure l'ajustement n'est pas fiable", "[pull_test_piece]") {
    const PullTestPiece piece = build_pull_test_piece();
    const auto results = predict_pull_metrics(piece, FabricResponse{});
    const PullFit fit = fit_pull_response(piece, results, FabricResponse{});
    CHECK(fit.used == 0);
    CHECK_FALSE(fit.enough_data);
    CHECK(fit.response.gain == FabricResponse{}.gain);
}
