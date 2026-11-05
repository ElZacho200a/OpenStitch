// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include <QWidget>

#include "live_companion.hpp"
#include "openstitch/mcp/live.hpp"

class QLabel;
class QListWidget;
class QPushButton;

namespace openstitch::desktop_v2 {

// Boîte d'approbation d'une modification demandée par l'agent : ce qui va changer (opérations,
// mesures, réserves) et le rendu AVANT / APRÈS quand le moteur l'a produit. Rien n'est appliqué
// tant que l'utilisateur n'a pas cliqué « Appliquer » ; la modification reste annulable (Ctrl+Z).
// Renvoie true = appliquer.
bool askAgentApproval(QWidget* parent, const mcp::ApprovalRequest& request);

// Panneau « Compagnon IA » : état du pont, comment brancher un agent, journal des demandes (ce que
// l'agent a proposé, ce qui a été appliqué ou refusé) et annulation de la dernière modification.
class CompanionPanel final : public QWidget {
    Q_OBJECT

public:
    explicit CompanionPanel(QWidget* parent = nullptr);

    void setActive(bool active, const QString& endpoint);
    void setEvents(const std::vector<CompanionEvent>& events);
    void setCanUndo(bool canUndo, const QString& undoName);
    void setError(const QString& message);

    [[nodiscard]] bool active() const { return active_; }
    // Commande qui branche Claude Code sur ce programme (`claude mcp add … openstitch-mcp --live`),
    // avec le chemin réel du serveur MCP installé à côté de l'application.
    [[nodiscard]] static QString connectionCommand();
    [[nodiscard]] int eventCount() const;
    [[nodiscard]] QString statusText() const;

signals:
    void toggleRequested(bool enable);
    void undoRequested();

private:
    bool active_{false};
    QLabel* status_{nullptr};
    QLabel* howTo_{nullptr};
    QPushButton* toggle_{nullptr};
    QPushButton* undo_{nullptr};
    QPushButton* copy_{nullptr};
    QListWidget* log_{nullptr};
};

} // namespace openstitch::desktop_v2
