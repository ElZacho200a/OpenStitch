// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <functional>
#include <vector>

#include <QDockWidget>

#include "companion_backend.hpp"

class QLabel;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTextBrowser;
class QGroupBox;

namespace openstitch::desktop {

// Dock « Compagnon IA » : conversation, cartes d'appels d'outils, panneau « changement proposé »
// (confirmer / rejeter / annuler la dernière application). Aucune logique métier : le dock affiche
// ce que le backend et l'hôte lui donnent et renvoie les décisions par signaux. Utilisable sans
// fournisseur connecté (écran d'accueil) et sans réseau.
class CompanionDock : public QDockWidget {
    Q_OBJECT

public:
    explicit CompanionDock(QWidget* parent = nullptr);

    // Backend non possédé ; nullptr = aucun fournisseur.
    void setBackend(CompanionBackend* backend);
    [[nodiscard]] CompanionBackend* backend() const { return backend_; }

    // Contexte du document actif (mis à jour par la fenêtre principale).
    void setContext(const QString& documentName, int selectionCount, bool hasDocument);
    void setLiveBridgeActive(bool active);
    void setUndoAvailable(bool available, const QString& undoName);
    // Le document a été remplacé (nouveau, ouverture) ou fermé : rejette toute proposition en
    // attente et avertit que la conversation concerne l'ancien document.
    void documentReplaced();

    // Proposition affichée sans bloquer ; `decision(true|false)` est appelé une seule fois (rejet
    // automatique si une autre proposition ou un changement de document survient).
    void showProposal(const CompanionProposal& proposal, std::function<void(bool)> decision);
    // Variante bloquante (boucle d'événements locale) pour l'approbation d'un outil MCP.
    bool askApproval(const CompanionProposal& proposal);

    void appendUserMessage(const QString& text);
    void appendAssistantMessage(const QString& text);
    void appendToolCall(const CompanionToolCall& call);
    void showError(const QString& message);
    void clearConversation();
    [[nodiscard]] bool busy() const { return busy_; }
    [[nodiscard]] bool hasPendingProposal() const { return static_cast<bool>(pendingDecision_); }

signals:
    void focusObjectRequested(quint64 objectId);
    void undoLastRequested();
    void enableLiveBridgeRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    struct Entry {
        enum class Kind { User, Assistant, Tool, Error, Note } kind{Kind::Note};
        QString text;
        CompanionToolCall tool;
    };

    void buildUi();
    void retranslate();
    void addEntry(Entry entry);
    void rebuildTranscript();
    void submit();
    void setBusy(bool busy);
    void resolveProposal(bool approved);
    void refreshHeader();
    void connectBackend();

    CompanionBackend* backend_{nullptr};
    std::function<void(bool)> pendingDecision_;
    std::vector<Entry> entries_;
    bool busy_{false};
    bool liveBridgeActive_{false};
    bool hasDocument_{false};
    int selectionCount_{0};
    QString documentName_;
    QString undoName_;

    QStackedWidget* stack_{nullptr};
    QLabel* welcome_{nullptr};
    QPushButton* enableBridge_{nullptr};
    QLabel* header_{nullptr};
    QLabel* errorBanner_{nullptr};
    QTextBrowser* transcript_{nullptr};
    QGroupBox* proposalBox_{nullptr};
    QLabel* proposalSummary_{nullptr};
    QListWidget* proposalItems_{nullptr};
    QLabel* proposalWarnings_{nullptr};
    QLabel* beforeImage_{nullptr};
    QLabel* afterImage_{nullptr};
    QPushButton* confirm_{nullptr};
    QPushButton* reject_{nullptr};
    QPushButton* undoLast_{nullptr};
    QPlainTextEdit* input_{nullptr};
    QPushButton* send_{nullptr};
    QPushButton* cancel_{nullptr};
    QProgressBar* progress_{nullptr};
};

} // namespace openstitch::desktop
