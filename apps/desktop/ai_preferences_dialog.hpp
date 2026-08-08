// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QDialog>

#include "ai_preferences.hpp"

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

private slots:
    void browseVenvPython();
    void browseWorkerScript();
    void browseModelsDir();
    void testConfiguration();

private:
    void appendTestLog(const QString& line);

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
    QPlainTextEdit* testLog_{nullptr};
    QPushButton* testButton_{nullptr};

    SamWorkerClient* testClient_{nullptr};
};

}  // namespace openstitch::desktop
