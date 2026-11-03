// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QDialog>

#include "ai_preferences.hpp"
#include "openstitch/ai_segmentation/backend_registry.hpp"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

namespace openstitch::desktop {

class SamWorkerClient;

// Préférences « Intelligence artificielle » : environnement d'exécution
// (WSL/natif), chemins du worker et des modèles, valeurs par défaut, et un
// bouton « Tester la configuration » qui démarre réellement le worker pour
// vérifier qu'il répond (cf. exigence : test réel, pas une simple
// validation de champs).
class AiPreferencesDialog : public QDialog {
    Q_OBJECT

public:
    explicit AiPreferencesDialog(QWidget* parent = nullptr);
    ~AiPreferencesDialog() override;

    void setPreferences(const AiPreferences& prefs);
    [[nodiscard]] AiPreferences preferences() const;
    // Fabrique de transports pour « Sonder les backends » (tests : backends factices en mémoire).
    void setTransportFactory(ai_segmentation::TransportFactory factory);

private slots:
    void browseVenvPython();
    void browseWorkerScript();
    void browseModelsDir();
    void testConfiguration();
    void browseBackendsFile();
    void probeBackends();

private:
    void appendTestLog(const QString& line);
    // Recharge la liste des moteurs depuis le fichier de backends en cours de saisie.
    void refreshEngineChoices(const QString& wanted);

    QCheckBox* enabledCheck_{nullptr};
    QComboBox* runtimeCombo_{nullptr};
    QLineEdit* wslDistroEdit_{nullptr};
    QLineEdit* venvPythonEdit_{nullptr};
    QLineEdit* workerScriptEdit_{nullptr};
    QLineEdit* modelsDirEdit_{nullptr};
    QComboBox* defaultModelCombo_{nullptr};
    QComboBox* defaultDeviceCombo_{nullptr};
    QSpinBox* maxResolutionSpin_{nullptr};
    QCheckBox* keepDiagnosticsCheck_{nullptr};
    QComboBox* logLevelCombo_{nullptr};
    QLineEdit* backendsFileEdit_{nullptr};
    QComboBox* engineCombo_{nullptr};
    QPlainTextEdit* testLog_{nullptr};
    ai_segmentation::TransportFactory transportFactory_;
    QPushButton* testButton_{nullptr};

    SamWorkerClient* testClient_{nullptr};
};

} // namespace openstitch::desktop
