// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <QPointer>

#include "companion_backend.hpp"
#include "openstitch/agent/agent.hpp"
#include "openstitch/mcp/live.hpp"

namespace openstitch::desktop {

// Réglages du compagnon (jamais de secret ici : la clé vit dans le stockage sécurisé du système).
namespace companion_settings {
[[nodiscard]] bool providerEnabled(); // fournisseur cloud activé par l'utilisateur
void setProviderEnabled(bool on);
[[nodiscard]] bool fullyDisabled(); // compagnon désactivé intégralement (pont + fournisseur)
void setFullyDisabled(bool off);
[[nodiscard]] QString model();
void setModel(const QString& model);
// Analyse sémantique automatique au lancement de Claude sur un projet non vide (activée par
// défaut).
[[nodiscard]] bool autoSemanticAnalysis();
void setAutoSemanticAnalysis(bool on);
[[nodiscard]] bool diagnosticsEnabled(); // journal de diagnostic (désactivé par défaut)
void setDiagnosticsEnabled(bool on);
[[nodiscard]] bool diagnosticsContentConsent(); // consentement à journaliser le contenu
void setDiagnosticsContentConsent(bool on);
inline constexpr const char* kSecretKey = "OpenStitch/anthropic-api-key";
} // namespace companion_settings

// Transport HTTPS de l'agent au-dessus de Qt Network. Appelé depuis un fil de travail (crée son
// propre gestionnaire réseau) ; honore délai et interruption.
class QtHttpClient final : public agent::IHttpClient {
public:
    agent::HttpResponse post(const std::string& url, const std::vector<agent::HttpHeader>& headers,
                             const std::string& body, std::chrono::milliseconds timeout,
                             const agent::CancelToken& cancel) override;
};

// Stockage sécurisé : Gestionnaire d'identifiants Windows ; indisponible ailleurs (la clé n'est
// alors jamais écrite sur disque).
[[nodiscard]] std::unique_ptr<agent::ISecretStore> makeSystemSecretStore();

// Accès de l'agent aux outils du projet ouvert. Bloquant, appelé depuis le fil de travail : le
// début et la fin de chaque outil (instantané, approbation, commit) passent par le fil GUI ; le
// calcul se fait sur la copie, dans le fil de travail.
class LiveToolHost final : public agent::IToolHost {
public:
    LiveToolHost(mcp::LiveService& service, QObject* guiContext)
        : service_(service), gui_(guiContext) {}
    std::vector<agent::ToolSpec> tools() override;
    agent::ToolHostResult call(const std::string& name, const std::string& args_json) override;

private:
    mcp::LiveService& service_;
    QPointer<QObject> gui_;
};

// Backend du dock pour Claude : une demande = une exécution de `AgentRunner` hors fil GUI.
class ClaudeCompanionBackend final : public CompanionBackend {
    Q_OBJECT

public:
    using ProviderFactory = std::function<std::shared_ptr<agent::IAgentProvider>(
        const std::string& apiKey, const std::string& model)>;

    // `secrets` n'est pas possédé. `factory` vide = fournisseur Anthropic réel (Qt Network).
    ClaudeCompanionBackend(mcp::LiveService& service, agent::ISecretStore& secrets,
                           ProviderFactory factory = {}, QObject* parent = nullptr);
    ~ClaudeCompanionBackend() override;

    [[nodiscard]] QString providerName() const override;
    [[nodiscard]] bool available() const override;
    // Relit les réglages et la présence de la clé (après le dialogue de configuration).
    void refresh();
    // Journal de diagnostic optionnel (non possédé) ; masque clés et chemins.
    void setDiagnostics(agent::DiagnosticLog* log) { log_ = log; }
    // Nouvelle conversation (document remplacé) : interrompt une demande en cours.
    void resetConversation();
    [[nodiscard]] bool running() const { return running_; }

public slots:
    void send(const QString& userText) override;
    void cancel() override;
    void decide(bool approved) override;

private:
    mcp::LiveService& service_;
    agent::ISecretStore& secrets_;
    ProviderFactory factory_;
    bool available_{false};
    std::atomic<bool> running_{false};
    std::shared_ptr<agent::CancelToken> token_;
    std::vector<agent::Message> history_;
    std::shared_ptr<bool> alive_;
    agent::DiagnosticLog* log_{nullptr};
};

} // namespace openstitch::desktop
