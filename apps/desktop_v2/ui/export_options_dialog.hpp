// SPDX-License-Identifier: Apache-2.0
#pragma once

// Fenêtre « Options machine » : machine, cadre (bibliothèque + marge), coupes, points d'arrêt,
// points courts / longs, départ et arrivée. Aucune logique métier : la lecture, la validation et
// l'application (une commande annulable par groupe) passent par `export_settings` (cœur).

#include <QDialog>
#include <QString>

#include "document_session.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace openstitch::desktop_v2 {

class ExportOptionsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit ExportOptionsDialog(DocumentSession& session, QWidget* parent = nullptr);

    // Applique les réglages affichés ; renvoie le message de refus ou une chaîne vide.
    QString apply();
    // Accès pour les tests (mêmes champs que l'utilisateur).
    void chooseHoop(const QString& hoopId, double marginMm);
    void chooseMachine(const QString& machineId);
    void setTrimThresholdMm(double mm);
    void setLockKind(int index);
    void setDesignStart(bool enabled, double xMm, double yMm);
    [[nodiscard]] QString checkText() const;
    [[nodiscard]] int hoopCount() const;
    [[nodiscard]] int machineCount() const;

private:
    void load();
    void refreshCheck();

    DocumentSession& session_;
    QComboBox* machine_{nullptr};
    QComboBox* hoop_{nullptr};
    QDoubleSpinBox* margin_{nullptr};
    QDoubleSpinBox* trim_{nullptr};
    QCheckBox* trimColor_{nullptr};
    QComboBox* lock_{nullptr};
    QDoubleSpinBox* lockLength_{nullptr};
    QSpinBox* lockPasses_{nullptr};
    QCheckBox* filterShort_{nullptr};
    QDoubleSpinBox* minStitch_{nullptr};
    QCheckBox* splitLong_{nullptr};
    QDoubleSpinBox* maxStitch_{nullptr};
    QCheckBox* autoJoin_{nullptr};
    QCheckBox* hasStart_{nullptr};
    QDoubleSpinBox* startX_{nullptr};
    QDoubleSpinBox* startY_{nullptr};
    QCheckBox* hasEnd_{nullptr};
    QDoubleSpinBox* endX_{nullptr};
    QDoubleSpinBox* endY_{nullptr};
    QLabel* check_{nullptr};
    QLabel* feedback_{nullptr};
    bool loading_{false};
};

} // namespace openstitch::desktop_v2
