// SPDX-License-Identifier: Apache-2.0
#pragma once

// Fenêtre « Options machine » : machine, cadre (bibliothèque + marge), coupes, points d'arrêt,
// points courts / longs, départ et arrivée. Aucune logique métier : la lecture, la validation et
// l'application (une commande annulable par groupe) passent par `export_settings` (cœur).

#include <filesystem>
#include <vector>

#include <QDialog>
#include <QString>

#include "document_session.hpp"
#include "hoop_store.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace openstitch::desktop_v2 {

class ExportOptionsDialog final : public QDialog {
    Q_OBJECT

public:
    // `hoopStoreFile` : fichier de « Mes cadres » ; vide = pas de cadres personnels (boutons
    // désactivés, rien n'est écrit).
    explicit ExportOptionsDialog(DocumentSession& session, std::filesystem::path hoopStoreFile = {},
                                 QWidget* parent = nullptr);

    // Applique les réglages affichés ; renvoie le message de refus ou une chaîne vide.
    QString apply();
    // Accès pour les tests (mêmes champs que l'utilisateur).
    void chooseHoop(const QString& hoopId, double marginMm);
    void chooseMachine(const QString& machineId);
    void setTrimThresholdMm(double mm);
    void setLockKind(int index);
    void setDesignStart(bool enabled, double xMm, double yMm);
    // Enregistre le cadre ACTUEL du projet dans « Mes cadres » ; refus en clair, ou chaîne vide.
    QString saveCurrentHoopAs(const QString& name);
    // Retire un cadre personnel (le document n'est pas touché) ; refus en clair, ou chaîne vide.
    QString removeUserHoopNamed(const QString& name);
    [[nodiscard]] int userHoopCount() const { return static_cast<int>(userHoops_.size()); }
    [[nodiscard]] QString checkText() const;
    [[nodiscard]] int hoopCount() const;
    [[nodiscard]] int machineCount() const;

private:
    void load();
    void refreshCheck();
    void rebuildHoopChoices();
    bool persistUserHoops();

    DocumentSession& session_;
    std::filesystem::path hoopStoreFile_;
    std::vector<UserHoop> userHoops_;
    QPushButton* saveHoop_{nullptr};
    QPushButton* removeHoop_{nullptr};
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
