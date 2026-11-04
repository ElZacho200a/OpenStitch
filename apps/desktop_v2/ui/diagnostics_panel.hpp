// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- panneau de diagnostic (espace Optimisation) : synthèse chiffrée et liste des
// constats du moteur, cliquables (le shell sélectionne les objets et centre la vue). N'analyse rien
// lui-même.

#include <QWidget>
#include <optional>

#include "diagnostics_service.hpp"

class QLabel;
class QListWidget;
class QPushButton;

namespace openstitch::desktop_v2 {

class DiagnosticsPanel : public QWidget {
    Q_OBJECT
public:
    explicit DiagnosticsPanel(QWidget* parent = nullptr);

    void setResult(const DiagnosticsResult& result);
    void setStale(bool stale); // le document a changé depuis l'analyse affichée
    void setBusy(bool busy);
    void clear();
    [[nodiscard]] int findingCount() const;
    [[nodiscard]] bool isStale() const { return stale_; }
    // Active la ligne `row` (comme un double-clic) : émet `findingActivated`.
    void activateRow(int row);

signals:
    void analyseRequested();
    void findingActivated(const openstitch::project_ops::Finding& finding);

private:
    QLabel* summary_;
    QPushButton* run_;
    QListWidget* list_;
    DiagnosticsResult result_;
    bool hasResult_{false};
    bool stale_{false};
};

} // namespace openstitch::desktop_v2
