// SPDX-License-Identifier: Apache-2.0
#pragma once

// Discussion avec l'agent, intégrée à la V2. Widgets seulement : la conversation, les cartes
// d'outils, les erreurs et l'état du fournisseur ; l'envoi, l'annulation et le fournisseur sont
// ceux d'un `CompanionBackend` (réel ou faux de test). Les MODIFICATIONS du document ne passent
// jamais par ce panneau : elles passent par le pont (`LiveCompanion`) et sa boîte d'approbation
// avant/après, une étape d'annulation chacune.

#include <QString>
#include <QWidget>

#include "companion_backend.hpp"

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace openstitch::desktop_v2 {

class ChatPanel final : public QWidget {
    Q_OBJECT

public:
    explicit ChatPanel(QWidget* parent = nullptr);

    // Le backend n'est pas possédé ; nullptr = aucun fournisseur (écran d'accueil).
    void setBackend(CompanionBackend* backend);
    void clearConversation();

    // Équivalent de saisir `text` et valider ; faux si vide, occupé ou sans fournisseur.
    bool submit(const QString& text);

    [[nodiscard]] bool busy() const { return busy_; }
    [[nodiscard]] int messageCount() const;
    [[nodiscard]] QString messageText(int index) const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] QString usageText() const;

signals:
    void configureRequested();
    void resetRequested(); // « Nouvelle conversation » : l'appelant remet l'historique à zéro

private:
    void append(const QString& text, const QString& role, bool error = false);
    void refreshState();

    CompanionBackend* backend_{nullptr};
    bool busy_{false};
    QLabel* status_{nullptr};
    QLabel* privacy_{nullptr};
    QLabel* usage_{nullptr};
    QListWidget* log_{nullptr};
    QLineEdit* input_{nullptr};
    QPushButton* send_{nullptr};
    QPushButton* cancel_{nullptr};
    QPushButton* configure_{nullptr};
    QPushButton* reset_{nullptr};
};

} // namespace openstitch::desktop_v2
