// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <memory>

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "claude_backend.hpp"
#include "live_companion.hpp"
#include "studio_controller.hpp"

class QEventLoop;

namespace openstitch::desktop_v2 {
class LiveServer;
}

namespace openstitch::desktop_v2::studio {

// Adaptateur Qt du compagnon IA : discussion avec Claude (backend partagé `apps/companion`), pont
// live MCP lié au document OUVERT (`DocumentSession::liveBridge`, serveur local de `live_server`)
// et approbation des propositions. Aucune logique métier ni mutation ici : toute modification
// passe par `LiveService` -> pont -> pile d'annulation (UNE étape), après un accord explicite.
//
// Proposition -> aperçu avant/après -> `approve()` / `reject()` : l'agent est bloqué sur le fil
// d'outils (jamais l'interface : une boucle d'événements locale attend la décision). Document
// remplacé, pont coupé ou annulation = rejet ; rien n'est alors appliqué.
//
// Clé API : écrite uniquement dans le stockage sécurisé injecté (`agent::ISecretStore`) ; ni ce
// contrôleur, ni le projet (.osp), ni les journaux ne la conservent.
class CompanionController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool bridgeActive READ bridgeActive NOTIFY changed)
    Q_PROPERTY(QString endpoint READ endpoint NOTIFY changed)
    Q_PROPERTY(QString connectionCommand READ connectionCommand CONSTANT)
    Q_PROPERTY(QVariantList events READ events NOTIFY changed)
    Q_PROPERTY(QVariantList messages READ messages NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(QString undoLabel READ undoLabel NOTIFY changed)
    Q_PROPERTY(bool providerReady READ providerReady NOTIFY changed)
    Q_PROPERTY(QString providerName READ providerName CONSTANT)
    Q_PROPERTY(bool hasApiKey READ hasApiKey NOTIFY changed)
    Q_PROPERTY(bool secretStoreAvailable READ secretStoreAvailable CONSTANT)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY changed)
    Q_PROPERTY(bool providerEnabled READ providerEnabled WRITE setProviderEnabled NOTIFY changed)
    Q_PROPERTY(bool fullyDisabled READ fullyDisabled WRITE setFullyDisabled NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString usage READ usage NOTIFY changed)
    Q_PROPERTY(bool proposalPending READ proposalPending NOTIFY proposalChanged)
    Q_PROPERTY(QVariantMap proposal READ proposal NOTIFY proposalChanged)
public:
    struct Options {
        std::filesystem::path liveDir;         // fichier de découverte du pont ; vide = défaut
        agent::ISecretStore* secrets{nullptr}; // non possédé ; null = stockage système
        desktop::ClaudeCompanionBackend::ProviderFactory factory; // vide = fournisseur réel
    };

    explicit CompanionController(StudioController& studio, Options options = {},
                                 QObject* parent = nullptr);
    ~CompanionController() override;

    [[nodiscard]] bool bridgeActive() const;
    [[nodiscard]] QString endpoint() const;
    [[nodiscard]] static QString connectionCommand();
    [[nodiscard]] QVariantList events() const;
    [[nodiscard]] QVariantList messages() const { return messages_; }
    [[nodiscard]] bool canUndo() const { return studio_.canUndo(); }
    [[nodiscard]] QString undoLabel() const { return studio_.undoLabel(); }
    [[nodiscard]] bool providerReady() const;
    [[nodiscard]] QString providerName() const;
    [[nodiscard]] bool hasApiKey() const;
    [[nodiscard]] bool secretStoreAvailable() const;
    [[nodiscard]] QString model() const;
    [[nodiscard]] bool providerEnabled() const;
    [[nodiscard]] bool fullyDisabled() const;
    [[nodiscard]] bool busy() const;
    [[nodiscard]] QString error() const { return error_; }
    [[nodiscard]] QString usage() const { return usage_; }
    [[nodiscard]] bool proposalPending() const { return loop_ != nullptr; }
    [[nodiscard]] QVariantMap proposal() const { return proposal_; }

    void setModel(const QString& model);
    void setProviderEnabled(bool on);
    void setFullyDisabled(bool off);

    // Pont live : "" si réussi, sinon l'erreur lisible.
    Q_INVOKABLE QString setBridgeEnabled(bool on);
    // Discussion.
    Q_INVOKABLE bool send(const QString& text);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void resetConversation();
    // Décision sur la proposition affichée (sans effet s'il n'y en a pas).
    Q_INVOKABLE void approve();
    Q_INVOKABLE void reject();
    // Annule la dernière étape d'annulation (celle de l'agent si c'est la dernière).
    Q_INVOKABLE void undoLast();
    // Clé : stockage sécurisé seulement ; la valeur n'est jamais relue ni exposée.
    Q_INVOKABLE bool setApiKey(const QString& key);
    Q_INVOKABLE void clearApiKey();
    // Ouvre Claude Code : active le pont et copie la commande de branchement dans le presse-papiers
    // (le lancement d'un terminal reste propre au bureau V1). "" ou message d'erreur.
    Q_INVOKABLE QString prepareClaudeCode();
    [[nodiscard]] desktop::ClaudeCompanionBackend* backend() { return backend_; }
    [[nodiscard]] LiveCompanion& companion() { return ensureCompanion(); }

signals:
    void changed();
    void proposalChanged();

private:
    LiveCompanion& ensureCompanion();
    desktop::ClaudeCompanionBackend& ensureBackend();
    bool askApproval(const mcp::ApprovalRequest& request);
    void decide(bool approved);
    void append(const QString& role, const QString& text);
    void onDocumentReplaced();
    agent::ISecretStore& secrets();

    StudioController& studio_;
    Options options_;
    std::unique_ptr<agent::ISecretStore> ownedSecrets_;
    DocumentSession::Token token_{};
    std::unique_ptr<LiveCompanion> companion_;
    std::unique_ptr<LiveServer> server_;
    desktop::ClaudeCompanionBackend* backend_{nullptr};
    QVariantList messages_;
    QVariantMap proposal_;
    QString error_;
    QString usage_;
    QEventLoop* loop_{nullptr};
    bool decision_{false};
};

} // namespace openstitch::desktop_v2::studio
