// SPDX-License-Identifier: Apache-2.0
#include "companion_controller.hpp"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QUrl>

#include "../live/live_server.hpp"

namespace openstitch::desktop_v2::studio {

namespace {
QString qs(const std::string& text) {
    return QString::fromStdString(text);
}

QString dataUrl(const std::string& png) {
    if (png.empty()) {
        return {};
    }
    return QStringLiteral("data:image/png;base64,") +
           QString::fromLatin1(
               QByteArray::fromRawData(png.data(), static_cast<qsizetype>(png.size())).toBase64());
}
} // namespace

CompanionController::CompanionController(StudioController& studio)
    : CompanionController(studio, Options{}) {}

CompanionController::CompanionController(StudioController& studio, Options options, QObject* parent)
    : QObject(parent), studio_(studio), options_(std::move(options)) {
    token_ = studio_.session().subscribe([this](ChangeKind kind) {
        if (kind == ChangeKind::Replaced) {
            onDocumentReplaced();
        }
        emit changed();
    });
}

CompanionController::~CompanionController() {
    studio_.session().unsubscribe(token_);
    if (loop_ != nullptr) { // une demande attend : rejet, rien n'est appliqué
        decision_ = false;
        loop_->quit();
    }
    if (backend_ != nullptr) {
        backend_->cancel();
    }
    if (companion_) {
        companion_->close();
    }
    server_.reset();
}

agent::ISecretStore& CompanionController::secrets() {
    if (options_.secrets != nullptr) {
        return *options_.secrets;
    }
    if (!ownedSecrets_) {
        ownedSecrets_ = desktop::makeSystemSecretStore();
    }
    return *ownedSecrets_;
}

LiveCompanion& CompanionController::ensureCompanion() {
    if (!companion_) {
        companion_ = std::make_unique<LiveCompanion>(
            studio_.session(),
            [this](const mcp::ApprovalRequest& request) { return askApproval(request); },
            [this] {
                const auto title = studio_.title().toStdString();
                return title.empty() ? std::string("(sans titre)") : title;
            });
        companion_->setOnChanged([this] { emit changed(); });
        // Fichiers : l'agent passe par les mêmes chemins que le menu Fichier (état, récents).
        mcp::LiveHostActions host;
        host.current_path = [this] { return studio_.path().toStdString(); };
        host.open_project = [this](const std::string& path, std::string&) {
            studio_.openProject(QUrl::fromLocalFile(QString::fromStdString(path)));
            return true; // asynchrone : l'agent relit live_status
        };
        host.save_project = [this](const std::string& path, std::string& error) {
            const QString target = QString::fromStdString(path);
            const bool ok = QDir::cleanPath(target) == QDir::cleanPath(studio_.path())
                                ? studio_.saveProject()
                                : studio_.saveProjectAs(QUrl::fromLocalFile(target));
            if (!ok) {
                error = studio_.status().toStdString();
            }
            return ok;
        };
        companion_->setHostActions(std::move(host));
        // Même règle que askApproval : en mode automatique, lots et numérisation passent sans
        // carte, donc sans aperçu avant/après (quatre passes complètes du moteur évitées).
        companion_->setOnStage([this](StageId stage) {
            emit stageRequested(QString::fromLatin1(stageIdName(stage).data()));
        });
        companion_->setPreviewWanted([this](const std::string& tool) {
            const bool undoable = tool == "project_apply" || tool == "project_digitize";
            return !(undoable && autoApply() && !fullyDisabled());
        });
    }
    return *companion_;
}

void CompanionController::wire(desktop::CompanionBackend* backend) {
    using B = desktop::CompanionBackend;
    connect(backend, &B::assistantText, this,
            [this](const QString& text) { append(QStringLiteral("assistant"), text); });
    connect(backend, &B::toolCalled, this, [this](const desktop::CompanionToolCall& call) {
        // Le NOM de l'outil est toujours montré (titre), le résumé dit sur quoi il porte.
        append(call.isError ? QStringLiteral("error") : QStringLiteral("tool"),
               call.summary + (call.refusal.isEmpty() ? QString() : " — " + call.refusal),
               call.tool.startsWith(QStringLiteral("mcp__openstitch__"))
                   ? call.tool.mid(QStringLiteral("mcp__openstitch__").size())
                   : call.tool);
    });
    connect(backend, &B::thinking, this,
            [this](const QString& text) { append(QStringLiteral("thinking"), text); });
    connect(backend, &B::activity, this, [this](const QString& label) {
        activity_ = label;
        emit changed();
    });
    connect(backend, &B::failed, this, [this](const QString& message) {
        error_ = message;
        append(QStringLiteral("error"), message);
    });
    connect(backend, &B::usage, this, [this](quint64 in, quint64 out) {
        usage_ = tr("%1 jetons envoyés, %2 reçus").arg(in).arg(out);
        emit changed();
    });
    connect(backend, &B::finished, this, [this] {
        activity_.clear();
        emit changed();
    });
}

desktop::ClaudeCompanionBackend& CompanionController::ensureBackend() {
    if (backend_ == nullptr) {
        auto& live = ensureCompanion();
        backend_ =
            new desktop::ClaudeCompanionBackend(live.service(), secrets(), options_.factory, this);
        wire(backend_);
    }
    return *backend_;
}

bool CompanionController::bridgeActive() const {
    return server_ != nullptr && server_->running();
}

QString CompanionController::endpoint() const {
    return server_ ? server_->endpoint() : QString();
}

QString CompanionController::connectionCommand() {
#ifdef Q_OS_WIN
    const QString server = QStringLiteral("openstitch-mcp.exe");
#else
    const QString server = QStringLiteral("openstitch-mcp");
#endif
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString installed = appDir.filePath(QStringLiteral("mcp/") + server);
    const QString path = QDir::toNativeSeparators(
        QFileInfo::exists(installed) ? installed : appDir.filePath(server));
    return QStringLiteral("claude mcp add openstitch -- \"%1\" --live").arg(path);
}

QVariantList CompanionController::events() const {
    QVariantList list;
    if (!companion_) {
        return list;
    }
    for (const auto& event : companion_->events()) {
        const char* outcome = event.outcome == CompanionEvent::Outcome::Applied   ? "applied"
                              : event.outcome == CompanionEvent::Outcome::Refused ? "refused"
                                                                                  : "pending";
        list.push_back(QVariantMap{{QStringLiteral("tool"), qs(event.tool)},
                                   {QStringLiteral("label"), qs(event.label)},
                                   {QStringLiteral("detail"), qs(event.detail)},
                                   {QStringLiteral("outcome"), QString::fromLatin1(outcome)}});
    }
    return list;
}

bool CompanionController::providerReady() const {
    if (codeBackend_ != nullptr) {
        return codeBackend_->available();
    }
    return backend_ != nullptr ? backend_->available()
                               : (!fullyDisabled() && providerEnabled() && hasApiKey());
}

QString CompanionController::providerName() const {
    if (codeBackend_ != nullptr) {
        return tr("Claude Code installé sur cet ordinateur (votre compte) : les échanges passent "
                  "par votre CLI ; il n'agit que par les outils MCP d'OpenStitch.");
    }
    return tr("Anthropic (Claude) : les messages et résultats d'outils sont envoyés à "
              "api.anthropic.com");
}

bool CompanionController::hasApiKey() const {
    auto* self = const_cast<CompanionController*>(this);
    auto& store = self->secrets();
    return store.available() && store.read(desktop::companion_settings::kSecretKey).has_value();
}

bool CompanionController::secretStoreAvailable() const {
    return const_cast<CompanionController*>(this)->secrets().available();
}

QString CompanionController::model() const {
    return desktop::companion_settings::model();
}

bool CompanionController::providerEnabled() const {
    return desktop::companion_settings::providerEnabled();
}

bool CompanionController::fullyDisabled() const {
    return desktop::companion_settings::fullyDisabled();
}

bool CompanionController::autoApply() const {
    return desktop::companion_settings::autoApply();
}

void CompanionController::setAutoApply(bool on) {
    desktop::companion_settings::setAutoApply(on);
    emit changed();
}

bool CompanionController::busy() const {
    return (backend_ != nullptr && backend_->running()) ||
           (codeBackend_ != nullptr && codeBackend_->running());
}

void CompanionController::setModel(const QString& model) {
    desktop::companion_settings::setModel(model);
    emit changed();
}

void CompanionController::setProviderEnabled(bool on) {
    desktop::companion_settings::setProviderEnabled(on);
    if (backend_ != nullptr) {
        backend_->refresh();
    }
    emit changed();
}

void CompanionController::setFullyDisabled(bool off) {
    desktop::companion_settings::setFullyDisabled(off);
    if (off) {
        (void)setBridgeEnabled(false);
        cancel();
        dropClaudeCode();
    }
    if (backend_ != nullptr) {
        backend_->refresh();
    }
    emit changed();
}

QString CompanionController::setBridgeEnabled(bool on) {
    if (!on) {
        dropClaudeCode(); // sans pont, l'agent intégré n'a plus de document à qui parler
        if (server_) {
            // Invalide aussi les calculs déjà en cours : plus rien ne peut être appliqué.
            studio_.session().liveBridge().reset();
            server_.reset();
        }
        if (loop_ != nullptr) {
            reject();
        }
        emit changed();
        return {};
    }
    if (fullyDisabled()) {
        return tr("Le compagnon IA est désactivé dans les préférences.");
    }
    if (bridgeActive()) {
        return {};
    }
    auto server = std::make_unique<LiveServer>(ensureCompanion().service(), this);
    QString why;
    const auto dir = options_.liveDir.empty() ? mcp::live_directory() : options_.liveDir;
    if (!server->start(dir, &why)) {
        error_ = why;
        emit changed();
        return why.isEmpty() ? tr("Le pont n'a pas pu démarrer.") : why;
    }
    server_ = std::move(server);
    error_.clear();
    emit changed();
    return {};
}

void CompanionController::append(const QString& role, const QString& text, const QString& title) {
    messages_.push_back(QVariantMap{{QStringLiteral("role"), role},
                                    {QStringLiteral("text"), text},
                                    {QStringLiteral("title"), title}});
    emit changed();
}

QString CompanionController::grantFolder(const QString& path) {
    const QFileInfo info(path);
    const QString dir = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    if (!ensureCompanion().service().grant_read_dir(
            QDir::fromNativeSeparators(dir).toStdString())) {
        return tr("Ce dossier ne peut pas être confié à l'agent (introuvable ou sensible).");
    }
    return {};
}

bool CompanionController::send(const QString& text) {
    const auto trimmed = text.trimmed();
    if (trimmed.isEmpty() || busy()) {
        return false;
    }
    if (fullyDisabled()) {
        append(QStringLiteral("error"), tr("Le compagnon IA est désactivé."));
        return false;
    }
    desktop::CompanionBackend* active = codeBackend_;
    if (active == nullptr) {
        auto& api = ensureBackend();
        api.refresh();
        active = &api;
    }
    auto& backend = *active;
    if (!backend.available()) {
        error_ = tr("Aucun fournisseur prêt : activez-le et enregistrez une clé API dans les "
                    "préférences (Compagnon IA).");
        append(QStringLiteral("error"), error_);
        return false;
    }
    error_.clear();
    activity_ = tr("Envoi à Claude…");
    append(QStringLiteral("user"), trimmed);
    // Un dossier nommé dans SA demande vaut accord de lecture (le texte vient de l'utilisateur,
    // jamais d'un fichier ni d'un outil) : l'agent peut alors lire le pack sans rien demander.
    static const QRegularExpression pathPattern(
        QStringLiteral(R"((?:[A-Za-z]:[\\/]|/)[^\s"'<>|*?]+)"));
    auto matches = pathPattern.globalMatch(trimmed);
    while (matches.hasNext()) {
        QString candidate = matches.next().captured(0);
        while (candidate.endsWith(QLatin1Char('.')) || candidate.endsWith(QLatin1Char(',')) ||
               candidate.endsWith(QLatin1Char(';')) || candidate.endsWith(QLatin1Char(')'))) {
            candidate.chop(1);
        }
        if (QFileInfo::exists(candidate) && grantFolder(candidate).isEmpty()) {
            const QFileInfo info(candidate);
            append(QStringLiteral("tool"),
                   tr("Accès en lecture accordé au dossier : %1")
                       .arg(QDir::toNativeSeparators(info.isDir() ? info.absoluteFilePath()
                                                                  : info.absolutePath())),
                   tr("Accès en lecture"));
        }
    }
    backend.send(trimmed);
    emit changed();
    return true;
}

void CompanionController::cancel() {
    if (backend_ != nullptr) {
        backend_->cancel();
    }
    if (codeBackend_ != nullptr) {
        codeBackend_->cancel();
    }
    if (loop_ != nullptr) {
        reject();
    }
    emit changed();
}

void CompanionController::resetConversation() {
    if (loop_ != nullptr) {
        reject();
    }
    if (backend_ != nullptr) {
        backend_->resetConversation();
    }
    if (codeBackend_ != nullptr) {
        codeBackend_->resetConversation();
    }
    messages_.clear();
    usage_.clear();
    error_.clear();
    emit changed();
}

void CompanionController::onDocumentReplaced() {
    // Nouveau document : l'ancienne conversation et toute proposition en attente parlent d'un
    // autre motif (le pont a déjà ouvert une nouvelle session côté `DocumentSession`).
    if (loop_ != nullptr) {
        decision_ = false;
        loop_->quit();
    }
    if (backend_ != nullptr) {
        backend_->resetConversation();
    }
    if (codeBackend_ != nullptr) {
        codeBackend_->resetConversation();
    }
    messages_.clear();
    usage_.clear();
}

bool CompanionController::askApproval(const mcp::ApprovalRequest& request) {
    // Mode automatique : l'agent travaille en direct, sans carte de validation. Les modifications
    // du document (lot, numérisation) sont annulables pas à pas ; les actions de fichier ne le
    // sont pas : elles ne passent seules que si rien n'est écrasé ni perdu (aucune réserve).
    if (autoApply() && !fullyDisabled()) {
        const bool undoable = request.tool == "project_apply" || request.tool == "project_digitize";
        if (undoable || request.warnings.empty()) {
            return true; // le journal d'activité garde la trace de l'application
        }
    }
    if (loop_ != nullptr) { // une proposition attend déjà : refus, jamais d'empilement
        return false;
    }
    QVariantList items;
    for (const auto& item : request.items) {
        items.push_back(
            QVariantMap{{QStringLiteral("object"), static_cast<qulonglong>(item.object_id)},
                        {QStringLiteral("text"), qs(item.text)}});
    }
    QStringList warnings;
    for (const auto& warning : request.warnings) {
        warnings << qs(warning);
    }
    QStringList metrics;
    for (const auto& metric : request.metrics) {
        metrics << qs(metric);
    }
    proposal_ = QVariantMap{{QStringLiteral("tool"), qs(request.tool)},
                            {QStringLiteral("title"), qs(request.label)},
                            {QStringLiteral("items"), items},
                            {QStringLiteral("warnings"), warnings},
                            {QStringLiteral("metrics"), metrics},
                            {QStringLiteral("before"), dataUrl(request.before_png)},
                            {QStringLiteral("after"), dataUrl(request.after_png)}};
    QEventLoop loop;
    loop_ = &loop;
    decision_ = false;
    emit proposalChanged();
    emit changed();
    loop.exec();
    loop_ = nullptr;
    proposal_.clear();
    emit proposalChanged();
    emit changed();
    return decision_;
}

void CompanionController::decide(bool approved) {
    if (loop_ == nullptr) {
        return;
    }
    decision_ = approved;
    loop_->quit();
}

void CompanionController::approve() {
    decide(true);
}

void CompanionController::reject() {
    decide(false);
}

void CompanionController::undoLast() {
    studio_.undo();
    emit changed();
}

bool CompanionController::setApiKey(const QString& key) {
    const auto trimmed = key.trimmed();
    auto& store = secrets();
    if (trimmed.isEmpty() || !store.available()) {
        return false;
    }
    const bool ok = store.write(desktop::companion_settings::kSecretKey, trimmed.toStdString());
    if (backend_ != nullptr) {
        backend_->refresh();
    }
    emit changed();
    return ok;
}

void CompanionController::clearApiKey() {
    (void)secrets().remove(desktop::companion_settings::kSecretKey);
    if (backend_ != nullptr) {
        backend_->refresh();
    }
    emit changed();
}

void CompanionController::dropClaudeCode() {
    if (codeBackend_ == nullptr) {
        return;
    }
    codeBackend_->resetConversation();
    codeBackend_->deleteLater();
    codeBackend_ = nullptr;
    emit changed();
}

QString CompanionController::prepareClaudeCode() {
    const auto refusal = setBridgeEnabled(true);
    if (!refusal.isEmpty()) {
        return refusal;
    }
    const auto env =
        options_.launcher ? *options_.launcher : desktop::ClaudeCodeLauncher::systemEnv();
    QString executable;
    QString workspace;
    const auto ready = desktop::ClaudeCodeLauncher::prepare(env, executable, workspace);
    if (ready != desktop::ClaudeCodeLauncher::Result::Started) {
        return desktop::ClaudeCodeLauncher::describe(ready);
    }
    if (codeBackend_ != nullptr) { // un seul processus Claude Code par conversation
        codeBackend_->resetConversation();
        codeBackend_->deleteLater();
    }
    codeBackend_ = new desktop::ClaudeCodeProcessBackend(executable, workspace, this);
    wire(codeBackend_);
    error_.clear();
    emit changed();
    return {};
}

QString CompanionController::openClaudeCodeTerminal() {
    const auto refusal = setBridgeEnabled(true);
    if (!refusal.isEmpty()) {
        return refusal;
    }
    if (auto* clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(connectionCommand());
    }
    const auto env =
        options_.launcher ? *options_.launcher : desktop::ClaudeCodeLauncher::systemEnv();
    const auto result = desktop::ClaudeCodeLauncher::launch(env);
    if (result != desktop::ClaudeCodeLauncher::Result::Started) {
        return desktop::ClaudeCodeLauncher::describe(result) + QLatin1Char(' ') +
               tr("La commande de branchement manuel est dans le presse-papiers.");
    }
    return {};
}

} // namespace openstitch::desktop_v2::studio
