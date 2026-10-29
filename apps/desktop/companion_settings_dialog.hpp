// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QDialog>

#include "openstitch/agent/agent.hpp"

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace openstitch::desktop {

// Réglages du compagnon IA : fournisseur Claude (clé API dans le stockage sécurisé du système,
// jamais dans un fichier), modèle, désactivation totale, politique de données affichée en clair.
// Aucune logique métier : lit et écrit `companion_settings` et le magasin de secrets.
class CompanionSettingsDialog : public QDialog {
    Q_OBJECT

public:
    CompanionSettingsDialog(agent::ISecretStore& secrets, agent::DiagnosticLog& log,
                            QWidget* parent = nullptr);

    // Écrit les réglages (activation, modèle, désactivation totale). La clé a son bouton propre.
    void applyToSettings();

private:
    void refreshKeyStatus();
    void saveKey();
    void removeKey();

    agent::ISecretStore& secrets_;
    agent::DiagnosticLog& log_;
    QCheckBox* diagnostics_{nullptr};
    QCheckBox* diagnosticsContent_{nullptr};
    QCheckBox* fullyOff_{nullptr};
    QCheckBox* enableProvider_{nullptr};
    QCheckBox* autoSemantic_{nullptr};
    QLineEdit* model_{nullptr};
    QLineEdit* key_{nullptr};
    QLabel* keyStatus_{nullptr};
    QPushButton* saveKey_{nullptr};
    QPushButton* removeKey_{nullptr};
};

} // namespace openstitch::desktop
