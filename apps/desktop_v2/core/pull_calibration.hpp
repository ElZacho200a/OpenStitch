// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- compensation de tirage : comparaison nominal <-> prédit et étalonnage sur
// pièce-test.
//
// Deux services, tous deux sans Qt et déterministes :
//  - `PullCalibration` : la pièce-test du moteur (`build_pull_test_piece`), le tableau nominal /
//    prédit / mesuré, la relecture des mesures (CSV ou une par une) et l'ajustement en grille de
//    gain, tassement et souplesse de trame. L'ajustement NE MARQUE JAMAIS le modèle « calibré » :
//    il propose des coefficients, la décision de les adopter reste humaine.
//  - `compareProjectPull` : pour le motif ouvert, ce que le tirage prédit fait aux points
//    (déplacement moyen et maximal, emprise nominale contre emprise prédite).
//
// HONNÊTETÉ : aucune mesure physique n'est incorporée ; les chiffres prédits sont ceux d'un modèle
// empirique non validé sur machine.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/stitch/sequence.hpp"
#include "openstitch/stitch_generation/pull_test_piece.hpp"

namespace openstitch::desktop_v2 {

struct CalibrationRow {
    std::string id;
    std::string description;
    std::string variable;
    double nominalMm{0.0};
    double predictedMm{0.0};
    bool hasMeasure{false};
    double measuredMm{0.0};
    double errorMm{0.0}; // prédit - mesuré (0 sans mesure)
};

struct CalibrationFit {
    bool enoughData{false}; // au moins 6 mesures
    std::size_t used{0};
    double rmsBeforeMm{0.0};
    double rmsAfterMm{0.0};
    double gain{0.0};
    double compression{0.0};
    double weftSoftness{0.0};
    std::string message; // phrase prête à afficher
};

class PullCalibration {
public:
    explicit PullCalibration(std::string fabric = "default");

    // Tissus connus (réglages nommés du moteur).
    [[nodiscard]] static std::vector<std::string> fabricNames();
    // Change de tissu (les mesures déjà saisies sont conservées) ; faux si le nom est inconnu.
    bool setFabric(const std::string& name);
    [[nodiscard]] const std::string& fabric() const { return fabric_; }

    // Reporte les mesures d'un CSV (colonne `measured_mm`) ; renvoie le nombre retenu.
    std::size_t applyCsv(const std::string& csv);
    // Saisit ou efface la mesure d'une métrique ; faux si l'identifiant est inconnu ou la valeur
    // non finie ou négative.
    bool setMeasure(const std::string& id, double millimeters);
    bool clearMeasure(const std::string& id);
    void clearMeasures();

    [[nodiscard]] std::vector<CalibrationRow> rows() const;
    [[nodiscard]] std::size_t measuredCount() const;
    [[nodiscard]] double rmsMm() const; // 0 sans mesure
    [[nodiscard]] std::string csv() const;
    [[nodiscard]] CalibrationFit fit() const;

    // Séquence de la pièce-test (à broder : X du dessin = sens de la chaîne).
    [[nodiscard]] const stitch::StitchSequence& testSequence() const { return piece_.sequence; }
    // Écrit la pièce-test en DST.
    [[nodiscard]] Result<void> writeTestPiece(const std::string& path) const;

private:
    void refresh();

    stitch_generation::PullTestPiece piece_;
    std::string fabric_;
    std::vector<stitch_generation::PullMetricResult> results_;
};

struct PullComparison {
    bool ok{false};
    std::string message;
    std::size_t points{0};
    double maxShiftMm{0.0};
    double meanShiftMm{0.0};
    double nominalWidthMm{0.0};
    double nominalHeightMm{0.0};
    double predictedWidthMm{0.0};
    double predictedHeightMm{0.0};
    bool calibrated{false};
    std::string fabric;
    [[nodiscard]] std::string summary() const;
};

// Compare la séquence EFFECTIVE du projet à celle que le tissu restituera (`fabric` : réglage
// nommé du moteur). Lecture seule.
[[nodiscard]] PullComparison compareProjectPull(const document::Project& project,
                                                const std::string& fabric = "default");

} // namespace openstitch::desktop_v2
