// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <memory>
#include <optional>

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "claude_backend.hpp"
#include "claude_code_process_backend.hpp"
#include "companion_launcher.hpp"
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
    Q_PROPERTY(QString providerName READ providerName NOTIFY changed)
    Q_PROPERTY(bool claudeCodeActive READ claudeCodeActive NOTIFY changed)
    Q_PROPERTY(bool hasApiKey READ hasApiKey NOTIFY changed)
    Q_PROPERTY(bool secretStoreAvailable READ secretStoreAvailable CONSTANT)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY changed)
    Q_PROPERTY(bool providerEnabled READ providerEnabled WRITE setProviderEnabled NOTIFY changed)
    Q_PROPERTY(bool fullyDisabled READ fullyDisabled WRITE setFullyDisabled NOTIFY changed)
    Q_PROPERTY(bool autoApply READ autoApply WRITE setAutoApply NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString usage READ usage NOTIFY changed)
    Q_PROPERTY(QString activity READ activity NOTIFY changed)
    Q_PROPERTY(bool proposalPending READ proposalPending NOTIFY proposalChanged)
    Q_PROPERTY(QVariantMap proposal READ proposal NOTIFY proposalChanged)
public:
    struct Options {
        std::filesystem::path liveDir;         // fichier de découverte du pont ; vide = défaut
        agent::ISecretStore* secrets{nullptr}; // non possédé ; null = stockage système
        desktop::ClaudeCompanionBackend::ProviderFactory factory; // vide = fournisseur réel
        // Environnement de lancement de Claude Code (terminal, `claude`) ; vide = système.
        std::optional<desktop::ClaudeCodeLauncher::Env> launcher;
    };

    explicit CompanionController(StudioController& studio);
    explicit CompanionController(StudioController& studio, Options options,
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
    [[nodiscard]] bool autoApply() const;
    [[nodiscard]] bool claudeCodeActive() const { return codeBackend_ != nullptr; }
    [[nodiscard]] bool busy() const;
    [[nodiscard]] QString error() const { return error_; }
    [[nodiscard]] QString usage() const { return usage_; }
    [[nodiscard]] QString activity() const { return busy() ? activity_ : QString(); }
    [[nodiscard]] bool proposalPending() const { return loop_ != nullptr; }
    [[nodiscard]] QVariantMap proposal() const { return proposal_; }

    void setModel(const QString& model);
    void setProviderEnabled(bool on);
    void setFullyDisabled(bool off);
    void setAutoApply(bool on);

    // Pont live : "" si réussi, sinon l'erreur lisible.
    Q_INVOKABLE QString setBridgeEnabled(bool on);
    // Discussion.
    Q_INVOKABLE bool send(const QString& text);
    // Confie un dossier à l'agent en lecture (images, .ora, notes d'intention…) : plus aucune
    // question pour ses fichiers. Les secrets restent refusés. "" ou message d'erreur.
    Q_INVOKABLE QString grantFolder(const QString& path);
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
    // Claude Code INTÉGRÉ (sans terminal) : active le pont, prépare l'espace de travail de cette
    // instance (serveur MCP --live relié à ce processus, consignes, skills) et branche la CLI
    // `claude` de l'utilisateur comme fournisseur du chat (processus headless, pipes redirigés,
    // outils MCP seulement). "" ou message d'erreur.
    Q_INVOKABLE QString prepareClaudeCode();
    // Variante secondaire : même préparation, mais ouvre `claude` dans un terminal externe ; la
    // commande de branchement manuel reste copiée dans le presse-papiers.
    Q_INVOKABLE QString openClaudeCodeTerminal();
    [[nodiscard]] desktop::ClaudeCompanionBackend* backend() { return backend_; }
    [[nodiscard]] LiveCompanion& companion() { return ensureCompanion(); }

signals:
    void changed();
    void proposalChanged();

private:
    LiveCompanion& ensureCompanion();
    desktop::ClaudeCompanionBackend& ensureBackend();
    void wire(desktop::CompanionBackend* backend);
    void dropClaudeCode();
    bool askApproval(const mcp::ApprovalRequest& request);
    void decide(bool approved);
    void append(const QString& role, const QString& text, const QString& title = {});
    void onDocumentReplaced();
    agent::ISecretStore& secrets();

    StudioController& studio_;
    Options options_;
    std::unique_ptr<agent::ISecretStore> ownedSecrets_;
    DocumentSession::Token token_{};
    std::unique_ptr<LiveCompanion> companion_;
    std::unique_ptr<LiveServer> server_;
    QString activity_;
    desktop::ClaudeCompanionBackend* backend_{nullptr};
    desktop::ClaudeCodeProcessBackend* codeBackend_{nullptr};
    QVariantList messages_;
    QVariantMap proposal_;
    QString error_;
    QString usage_;
    QEventLoop* loop_{nullptr};
    bool decision_{false};
};

} // namespace openstitch::desktop_v2::studio
