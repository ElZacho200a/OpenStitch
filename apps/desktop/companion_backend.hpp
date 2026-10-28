// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

namespace openstitch::desktop {

// Appel d'outil affiché dans la conversation (carte structurée) : jamais du JSON brut.
struct CompanionToolCall {
    QString tool;    // nom de l'outil (ex. project_satin_compare)
    QString summary; // une phrase : cible, paramètres, résultat
    bool isError{false};
    QString refusal; // motif du refus / de l'erreur, vide sinon
};

// Un objet touché par un changement proposé ; `objectId` ≠ 0 donne un lien de focus sur le canevas.
struct CompanionProposalItem {
    quint64 objectId{0};
    QString text;
};

// Changement proposé à l'utilisateur AVANT toute mutation du projet ouvert.
struct CompanionProposal {
    QString title;
    QString summary;
    QList<CompanionProposalItem> items;
    QStringList warnings;
    QByteArray beforePng; // aperçu facultatif (PNG) ; vide = pas de comparaison visuelle
    QByteArray afterPng;
};

// Frontière entre le dock (widgets, sans logique métier) et l'agent. Le fournisseur réel (Claude)
// et le faux backend des tests l'implémentent ; le dock fonctionne sans aucun des deux.
class CompanionBackend : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    [[nodiscard]] virtual QString providerName() const = 0;
    // Faux = aucun fournisseur connecté : le dock affiche l'écran d'accueil.
    [[nodiscard]] virtual bool available() const = 0;

public slots:
    virtual void send(const QString& userText) = 0;
    virtual void cancel() = 0;
    // Décision de l'utilisateur sur la dernière `proposalReady`.
    virtual void decide(bool approved) = 0;

signals:
    void assistantText(const QString& text); // message complet de l'assistant
    void toolCalled(const openstitch::desktop::CompanionToolCall& call);
    void proposalReady(const openstitch::desktop::CompanionProposal& proposal);
    void failed(const QString& message); // réseau, quota, refus… (non bloquant)
    void usage(quint64 inputTokens, quint64 outputTokens); // cumul de la demande, si connu
    void finished(); // fin du tour (succès, échec ou annulation)
};

} // namespace openstitch::desktop

Q_DECLARE_METATYPE(openstitch::desktop::CompanionToolCall)
Q_DECLARE_METATYPE(openstitch::desktop::CompanionProposal)
