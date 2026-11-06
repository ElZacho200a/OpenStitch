// SPDX-License-Identifier: Apache-2.0
#pragma once

// Assistant d'étalonnage du tirage : pièce-test à broder, tableau nominal / prédit / mesuré,
// relecture des mesures (CSV ou saisie dans le tableau), proposition d'ajustement. Aucune logique
// métier ici : tout passe par `PullCalibration` (cœur). Le modèle n'est jamais déclaré « calibré ».

#include <QDialog>
#include <QString>

#include "pull_calibration.hpp"

class QComboBox;
class QLabel;
class QTableWidget;

namespace openstitch::desktop_v2 {

class CalibrationDialog final : public QDialog {
    Q_OBJECT

public:
    explicit CalibrationDialog(const document::Project* project, QWidget* parent = nullptr);

    // Reporte des mesures au format CSV du moteur ; renvoie le nombre retenu (testable sans
    // fichier).
    int loadMeasurementsCsv(const QString& csv);
    // Saisit une mesure comme dans le tableau ; faux si l'identifiant est inconnu.
    bool setMeasure(const QString& id, double millimeters);
    void setFabric(const QString& name);

    [[nodiscard]] int rowCount() const;
    [[nodiscard]] QString comparisonText() const;
    [[nodiscard]] QString fitText() const;
    [[nodiscard]] const PullCalibration& calibration() const { return calibration_; }

private:
    void rebuildTable();
    void updateSummary();
    void chooseCsv();
    void saveCsv();
    void saveTestPiece();

    PullCalibration calibration_;
    const document::Project* project_{nullptr};
    QComboBox* fabric_{nullptr};
    QTableWidget* table_{nullptr};
    QLabel* comparison_{nullptr};
    QLabel* fit_{nullptr};
    bool building_{false};
};

} // namespace openstitch::desktop_v2
