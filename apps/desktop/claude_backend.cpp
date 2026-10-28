// SPDX-License-Identifier: Apache-2.0
#include "claude_backend.hpp"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QThreadPool>
#include <QTimer>
#include <set>

// clang-format off
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincred.h> // après windows.h : l'ordre alphabétique casse la compilation
#endif
// clang-format on

namespace openstitch::desktop {

// --- Réglages ---------------------------------------------------------------------------------

namespace companion_settings {

bool providerEnabled() {
    return QSettings().value(QStringLiteral("ai/companion/enabled"), false).toBool();
}
void setProviderEnabled(bool on) {
    QSettings().setValue(QStringLiteral("ai/companion/enabled"), on);
}
bool fullyDisabled() {
    return QSettings().value(QStringLiteral("ai/companion/off"), false).toBool();
}
void setFullyDisabled(bool off) {
    QSettings().setValue(QStringLiteral("ai/companion/off"), off);
}
QString model() {
    return QSettings()
        .value(QStringLiteral("ai/companion/model"), QStringLiteral("claude-sonnet-5-5"))
        .toString();
}
void setModel(const QString& model) {
    QSettings().setValue(QStringLiteral("ai/companion/model"), model);
}
bool diagnosticsEnabled() {
    return QSettings().value(QStringLiteral("ai/companion/diagnostics"), false).toBool();
}
void setDiagnosticsEnabled(bool on) {
    QSettings().setValue(QStringLiteral("ai/companion/diagnostics"), on);
}
bool diagnosticsContentConsent() {
    return QSettings().value(QStringLiteral("ai/companion/diagnosticsContent"), false).toBool();
}
void setDiagnosticsContentConsent(bool on) {
    QSettings().setValue(QStringLiteral("ai/companion/diagnosticsContent"), on);
}

} // namespace companion_settings

// --- HTTP -------------------------------------------------------------------------------------

agent::HttpResponse QtHttpClient::post(const std::string& url,
                                       const std::vector<agent::HttpHeader>& headers,
                                       const std::string& body, std::chrono::milliseconds timeout,
                                       const agent::CancelToken& cancel) {
    agent::HttpResponse out;
    QNetworkAccessManager manager; // créé dans le fil appelant : jamais partagé entre fils
    QNetworkRequest request{QUrl(QString::fromStdString(url))};
    for (const auto& h : headers) {
        request.setRawHeader(QByteArray::fromStdString(h.name), QByteArray::fromStdString(h.value));
    }
    request.setTransferTimeout(static_cast<int>(timeout.count()));
    QNetworkReply* reply = manager.post(request, QByteArray::fromStdString(body));
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    bool cancelled = false;
    QTimer poll;
    poll.setInterval(50);
    QObject::connect(&poll, &QTimer::timeout, &poll, [&] {
        if (cancel.cancelled() && !cancelled) {
            cancelled = true;
            reply->abort();
        }
    });
    poll.start();
    loop.exec();
    poll.stop();
    out.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    out.body = reply->readAll().toStdString();
    out.cancelled = cancelled;
    out.timed_out = !cancelled && reply->error() == QNetworkReply::OperationCanceledError;
    if (out.status == 0 && !out.timed_out && !cancelled) {
        out.error = reply->errorString().toStdString(); // le texte ne contient jamais la clé
    }
    const QByteArray retry = reply->rawHeader("retry-after");
    if (!retry.isEmpty()) {
        out.retry_after_ms = retry.toInt() * 1000;
    }
    reply->deleteLater();
    return out;
}

// --- Stockage sécurisé ------------------------------------------------------------------------

namespace {

#ifdef _WIN32
class WindowsCredentialStore final : public agent::ISecretStore {
public:
    [[nodiscard]] bool available() const override { return true; }

    [[nodiscard]] std::optional<std::string> read(const std::string& key) override {
        PCREDENTIALW cred = nullptr;
        const std::wstring target = widen(key);
        if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &cred) || cred == nullptr) {
            return std::nullopt;
        }
        std::string secret(reinterpret_cast<const char*>(cred->CredentialBlob),
                           cred->CredentialBlobSize);
        CredFree(cred);
        return secret;
    }

    bool write(const std::string& key, const std::string& secret) override {
        std::wstring target = widen(key);
        CREDENTIALW cred{};
        cred.Type = CRED_TYPE_GENERIC;
        cred.TargetName = target.data();
        cred.CredentialBlobSize = static_cast<DWORD>(secret.size());
        cred.CredentialBlob =
            reinterpret_cast<LPBYTE>(const_cast<char*>(secret.data())); // lu seulement par l'API
        cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
        return CredWriteW(&cred, 0) != FALSE;
    }

    bool remove(const std::string& key) override {
        const std::wstring target = widen(key);
        return CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) != FALSE;
    }

private:
    static std::wstring widen(const std::string& s) {
        return QString::fromStdString(s).toStdWString();
    }
};
#else
class UnavailableSecretStore final : public agent::ISecretStore {
public:
    [[nodiscard]] bool available() const override { return false; }
    [[nodiscard]] std::optional<std::string> read(const std::string&) override {
        return std::nullopt;
    }
    bool write(const std::string&, const std::string&) override { return false; }
    bool remove(const std::string&) override { return false; }
};
#endif

} // namespace

std::unique_ptr<agent::ISecretStore> makeSystemSecretStore() {
#ifdef _WIN32
    return std::make_unique<WindowsCredentialStore>();
#else
    return std::make_unique<UnavailableSecretStore>();
#endif
}

// --- Outils du projet -------------------------------------------------------------------------

std::vector<agent::ToolSpec> LiveToolHost::tools() {
    return agent::parse_tool_specs(mcp::live_tools_json());
}

agent::ToolHostResult LiveToolHost::call(const std::string& name, const std::string& args_json) {
    if (gui_.isNull()) {
        agent::ToolHostResult gone;
        gone.is_error = true;
        agent::ContentBlock b;
        b.text = "l'application se ferme";
        gone.content.push_back(std::move(b));
        return gone;
    }
    std::shared_ptr<mcp::LivePendingCall> pending;
    QMetaObject::invokeMethod(
        gui_.data(), [&] { pending = service_.begin(name, args_json); },
        Qt::BlockingQueuedConnection);
    pending->run(); // calcul sur la copie figée, hors fil GUI
    std::string reply;
    QMetaObject::invokeMethod(
        gui_.data(), [&] { reply = service_.finish(*pending); }, Qt::BlockingQueuedConnection);
    return agent::parse_tool_result(reply);
}

// --- Backend Claude ---------------------------------------------------------------------------

namespace {

constexpr const char* kSystemPrompt =
    "Tu es le compagnon IA d'OpenStitch Studio, un logiciel de numérisation de broderie machine. "
    "Tu travailles sur le projet actuellement ouvert, uniquement avec les outils fournis.\n"
    "- Commence par live_context (ou live_status) : révision, sélection, palette des FILS, "
    "objets.\n"
    "- Ciblage : live_target (« orange », « #RRGGBB », « selection »). Si ambiguous=true, pose la "
    "question avec live_ask_user (l'utilisateur choisit dans le panneau) AVANT toute "
    "modification.\n"
    "- Satin : live_plan_satin compare skeleton / parallel_sweep / medial_axis et justifie les "
    "refus ; live_preview montre l'avant/après (points, sauts, constats, risque textile, rendu) "
    "sur une copie.\n"
    "- Predicted est une compensation de tirage EMPIRIQUE, non calibrée sur machine : dis-le.\n"
    "- Au plus 3 itérations d'amélioration par demande ; chaque application est un point de "
    "reprise (Ctrl+Z).\n"
    "- Les outils de lecture (project_inspect, project_palette, project_select, project_check, "
    "project_render, project_satin_compare, project_fabric_risk…) ne modifient jamais le projet.\n"
    "- Pour modifier : prépare d'abord project_apply avec dry_run=true, résume le résultat, puis "
    "appelle project_apply sans dry_run. L'utilisateur approuve chaque modification, qui s'annule "
    "en une étape.\n"
    "- Ne promets jamais de convertir toute forme en satin : justifie les refus et garde le "
    "tatami quand c'est nécessaire. Ne confonds pas teinte de segmentation et couleur de fil.\n"
    "- Tu ne peux ni ouvrir, ni enregistrer, ni exporter de fichier, ni exécuter de commande.\n"
    "Réponds en français, de façon concise.";

class RunObserver final : public agent::IAgentObserver {
public:
    RunObserver(ClaudeCompanionBackend* backend, agent::DiagnosticLog* log)
        : backend_(backend), log_(log) {}

    void on_text(const std::string& text) override {
        emit backend_->assistantText(QString::fromStdString(text));
    }
    void on_tool_call(const std::string& name, const std::string& args_json,
                      const agent::ToolHostResult& result, const std::string& refusal) override {
        CompanionToolCall card;
        card.tool = QString::fromStdString(name);
        card.summary = QString::fromStdString(agent::describe_arguments(args_json));
        card.isError = result.is_error || !refusal.empty();
        if (card.isError) {
            const std::string detail = agent::first_text(result);
            card.refusal = QString::fromStdString(refusal.empty() ? detail : refusal);
            if (card.refusal.size() > 400) {
                card.refusal = card.refusal.left(400) + QStringLiteral("…");
            }
        }
        if (log_ != nullptr) {
            log_->record("tool", name + (card.isError ? " erreur" : " ok"));
            log_->record_content("tool_args", name + " " + args_json);
        }
        emit backend_->toolCalled(card);
    }
    void on_usage(const agent::Usage&, const agent::Usage& total) override {
        emit backend_->usage(total.input_tokens, total.output_tokens);
    }

private:
    ClaudeCompanionBackend* backend_;
    agent::DiagnosticLog* log_;
};

QString hintFor(const QString& code) {
    if (code == QLatin1String(agent::code::kAuth)) {
        return QObject::tr(" Vérifiez la clé dans Aide ▸ Compagnon IA…");
    }
    if (code == QLatin1String(agent::code::kQuota)) {
        return QObject::tr(" Réessayez plus tard ou vérifiez votre quota chez le fournisseur.");
    }
    if (code == QLatin1String(agent::code::kNetwork) ||
        code == QLatin1String(agent::code::kTimeout)) {
        return QObject::tr(" OpenStitch reste utilisable hors ligne.");
    }
    return {};
}

} // namespace

ClaudeCompanionBackend::ClaudeCompanionBackend(mcp::LiveService& service,
                                               agent::ISecretStore& secrets,
                                               ProviderFactory factory, QObject* parent)
    : CompanionBackend(parent), service_(service), secrets_(secrets), factory_(std::move(factory)),
      alive_(std::make_shared<bool>(true)) {
    qRegisterMetaType<CompanionToolCall>(); // signaux émis depuis le fil de travail (file
                                            // d'attente)
    if (!factory_) {
        factory_ = [](const std::string& key, const std::string& model) {
            // Le client HTTP vit autant que le fournisseur (propriété partagée).
            struct Holder final : agent::IAgentProvider {
                QtHttpClient http;
                std::unique_ptr<agent::AnthropicProvider> provider;
                [[nodiscard]] std::string name() const override { return provider->name(); }
                [[nodiscard]] agent::ProviderCapabilities capabilities() const override {
                    return provider->capabilities();
                }
                agent::ProviderResult complete(const agent::ProviderRequest& r,
                                               const agent::CancelToken& c) override {
                    return provider->complete(r, c);
                }
            };
            auto holder = std::make_shared<Holder>();
            agent::AnthropicOptions options;
            options.model = model;
            holder->provider =
                std::make_unique<agent::AnthropicProvider>(holder->http, key, options);
            return std::static_pointer_cast<agent::IAgentProvider>(holder);
        };
    }
    refresh();
}

ClaudeCompanionBackend::~ClaudeCompanionBackend() {
    *alive_ = false;
    if (token_) {
        token_->cancel();
    }
    QThreadPool::globalInstance()->waitForDone(5000);
}

QString ClaudeCompanionBackend::providerName() const {
    return tr(
        "Anthropic (Claude) — les messages et résultats d'outils sont envoyés à api.anthropic.com");
}

bool ClaudeCompanionBackend::available() const {
    return available_;
}

void ClaudeCompanionBackend::refresh() {
    available_ = !companion_settings::fullyDisabled() && companion_settings::providerEnabled() &&
                 secrets_.available() && secrets_.read(companion_settings::kSecretKey).has_value();
}

void ClaudeCompanionBackend::resetConversation() {
    cancel();
    if (!running_) {
        history_.clear();
    }
}

void ClaudeCompanionBackend::decide(bool) {
    // Les approbations passent par le dock (barrière d'approbation du pont) ; rien à faire ici.
}

void ClaudeCompanionBackend::cancel() {
    if (token_) {
        token_->cancel();
    }
}

void ClaudeCompanionBackend::send(const QString& userText) {
    if (running_.exchange(true)) {
        return;
    }
    refresh();
    const auto key = available_ ? secrets_.read(companion_settings::kSecretKey) : std::nullopt;
    if (!key) {
        running_ = false;
        emit failed(tr("Aucune clé API disponible : configurez-la dans Aide ▸ Compagnon IA…"));
        emit finished();
        return;
    }
    token_ = std::make_shared<agent::CancelToken>();
    const std::string text = userText.toStdString();
    const std::string apiKey = *key;
    const std::string model = companion_settings::model().toStdString();
    auto alive = alive_;
    auto token = token_;
    QPointer<ClaudeCompanionBackend> self(this);
    agent::DiagnosticLog* log = log_;
    if (log != nullptr) {
        log->add_secret(apiKey);
        log->record("request", "chars=" + std::to_string(text.size()) + " modele=" + model);
        log->record_content("user_message", text);
    }
    QThreadPool::globalInstance()->start([=, this] {
        RunObserver observer(this, log);
        LiveToolHost host(service_, this);
        std::shared_ptr<agent::IAgentProvider> provider = factory_(apiKey, model);
        agent::RunnerOptions options;
        options.model = model;
        options.system = kSystemPrompt;
        std::set<std::string> allowed;
        for (const auto& spec : host.tools()) {
            allowed.insert(spec.name);
        }
        agent::ToolPolicy policy(allowed, {"bash", "shell", "project_open", "project_save",
                                           "project_export", "project_new", "project_digitize"});
        agent::AgentRunner runner(*provider, host, policy, options);
        const agent::RunOutcome outcome = runner.run(history_, text, observer, *token);
        if (log != nullptr) {
            log->record(
                "finished",
                "statut=" + std::to_string(static_cast<int>(outcome.status)) +
                    " etapes=" + std::to_string(outcome.steps) +
                    " jetons_in=" + std::to_string(outcome.usage.input_tokens) +
                    " jetons_out=" + std::to_string(outcome.usage.output_tokens) +
                    (outcome.error.code.empty() ? std::string{} : " code=" + outcome.error.code));
        }
        if (!*alive) {
            return;
        }
        QString message;
        switch (outcome.status) {
        case agent::RunOutcome::Status::Failed:
        case agent::RunOutcome::Status::StepLimit:
            message = QString::fromStdString(agent::redact(outcome.error.message, {apiKey})) +
                      hintFor(QString::fromStdString(outcome.error.code));
            break;
        case agent::RunOutcome::Status::BudgetExceeded:
            message = tr("Budget de jetons atteint : la demande est arrêtée.");
            break;
        default:
            break;
        }
        if (self.isNull()) {
            return;
        }
        QMetaObject::invokeMethod(
            self.data(),
            [self, message] {
                if (self == nullptr) {
                    return;
                }
                self->running_ = false;
                if (!message.isEmpty()) {
                    emit self->failed(message);
                }
                emit self->finished();
            },
            Qt::QueuedConnection);
    });
}

} // namespace openstitch::desktop
