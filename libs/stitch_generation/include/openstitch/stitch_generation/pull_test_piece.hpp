// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "openstitch/stitch/sequence.hpp"
#include "openstitch/stitch_generation/pull_field.hpp"

namespace openstitch::stitch_generation {

// Pièce-test de calibration du tirage (HP-ENG-001.a) : un motif déterministe, brodé une fois sur le
// tissu réel, dont on mesure des distances au pied à coulisse. Chaque mesure (`PullMetric`) est la
// distance entre deux pénétrations nominales connues ; le modèle prédit la même distance après
// tirage (`simulate_fabric_pull`), l'écart mesuré/prédit sert de métrique de calibration.
//
// Le motif isole une variable à la fois :
//   - croix de repérage cousues EN PREMIER (tirage global, chaîne contre trame) ;
//   - colonnes de satin de largeurs 3 / 5 / 8 / 12 mm (gain le long du point) ;
//   - colonnes de 4 mm à 0° / 45° / 90° du grain (anisotropie, sens du tissu) ;
//   - carrés de tatami à trois densités (tassement) ;
//   - remplissage + contour dans les deux ordres de couture (ordre : jour entre couleurs).
// Le motif se brode avec l'axe X du dessin dans le sens de la chaîne (grain 0°). Aucune mesure
// physique n'est incorporée : ce module fournit le protocole, pas des coefficients.
struct PullMetric {
    std::string id;          // stable, ASCII (clé du fichier de mesures)
    std::string description; // pour l'opérateur (français)
    std::string variable;   // variable du modèle sondée : gain, grain, compression, ordre, global
    std::size_t first{0};   // indice de la commande Stitch de départ dans la séquence
    std::size_t second{0};  // indice de la commande Stitch d'arrivée
    double nominal_mm{0.0}; // distance dessinée entre les deux pénétrations
};

struct PullTestPiece {
    stitch::StitchSequence sequence;
    std::vector<PullMetric> metrics;
};

[[nodiscard]] PullTestPiece build_pull_test_piece();

struct PullMetricResult {
    PullMetric metric;
    double predicted_mm{0.0};
    bool has_measure{false};
    double measured_mm{0.0};
    double error_mm{0.0}; // prédit - mesuré (0 sans mesure)
};

// Distance prédite après tirage pour chaque métrique (séquence simulée, ordre de couture compris).
[[nodiscard]] std::vector<PullMetricResult> predict_pull_metrics(const PullTestPiece& piece,
                                                                 const FabricResponse& fabric);

// Tableau CSV (séparateur « ; », virgule décimale tolérée à la relecture) : à remplir dans la
// colonne `measured_mm` puis à relire avec `apply_measurements_csv`.
[[nodiscard]] std::string pull_metrics_csv(const std::vector<PullMetricResult>& results);

// Reporte les mesures d'un CSV (colonne `measured_mm`, repérée par `id`) ; renvoie le nombre de
// mesures retenues. Lignes inconnues, vides ou non numériques ignorées.
[[nodiscard]] std::size_t apply_measurements_csv(std::vector<PullMetricResult>& results,
                                                 std::string_view csv);

// Écart quadratique moyen (mm) prédit/mesuré sur les métriques mesurées ; 0 si aucune.
[[nodiscard]] double pull_metrics_rms(const std::vector<PullMetricResult>& results);

struct PullFit {
    FabricResponse response; // meilleur réglage de la grille (calibrated reste faux)
    double rms_before_mm{0.0};
    double rms_after_mm{0.0};
    std::size_t used{0};     // mesures utilisées
    bool enough_data{false}; // au moins 6 mesures : en dessous, l'ajustement n'est pas fiable
};

// Recherche en grille déterministe de gain, tassement et souplesse de trame minimisant l'écart
// quadratique moyen. Les portées et le coefficient de Poisson restent ceux de `base`.
[[nodiscard]] PullFit fit_pull_response(const PullTestPiece& piece,
                                        const std::vector<PullMetricResult>& measured,
                                        const FabricResponse& base);

} // namespace openstitch::stitch_generation
