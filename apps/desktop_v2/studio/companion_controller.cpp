// SPDX-License-Identifier: Apache-2.0
#include "companion_controller.hpp"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QGuiApplication>

#include "../ui/live_server.hpp"

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
    }
    return *companion_;
}

desktop::ClaudeCompanionBackend& CompanionController::ensureBackend() {
    if (backend_ == nullptr) {
        auto& live = ensureCompanion();
        backend_ =
            new desktop::ClaudeCompanionBackend(live.service(), secrets(), options_.factory, this);
        using B = desktop::CompanionBackend;
        connect(backend_, &B::assistantText, this,
                [this](const QString& text) { append(QStringLiteral("assistant"), text); });
        connect(backend_, &B::toolCalled, this, [this](const desktop::CompanionToolCall& call) {
            append(call.isError ? QStringLiteral("error") : QStringLiteral("tool"),
                   call.summary + (call.refusal.isEmpty() ? QString() : " — " + call.refusal));
        });
        connect(backend_, &B::failed, this, [this](const QString& message) {
            error_ = message;
            append(QStringLiteral("error"), message);
        });
        connect(backend_, &B::usage, this, [this](quint64 in, quint64 out) {
            usage_ = tr("%1 jetons envoyés, %2 reçus").arg(in).arg(out);
            emit changed();
        });
        connect(backend_, &B::finished, this, [this] { emit changed(); });
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
    return backend_ != nullptr ? backend_->available()
                               : (!fullyDisabled() && providerEnabled() && hasApiKey());
}

QString CompanionController::providerName() const {
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

bool CompanionController::busy() const {
    return backend_ != nullptr && backend_->running();
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
    }
    if (backend_ != nullptr) {
        backend_->refresh();
    }
    emit changed();
}

QString CompanionController::setBridgeEnabled(bool on) {
    if (!on) {
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

void CompanionController::append(const QString& role, const QString& text) {
    messages_.push_back(
        QVariantMap{{QStringLiteral("role"), role}, {QStringLiteral("text"), text}});
    emit changed();
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
    auto& backend = ensureBackend();
    backend.refresh();
    if (!backend.available()) {
        error_ = tr("Aucun fournisseur prêt : activez-le et enregistrez une clé API dans les "
                    "préférences (Compagnon IA).");
        append(QStringLiteral("error"), error_);
        return false;
    }
    error_.clear();
    append(QStringLiteral("user"), trimmed);
    backend.send(trimmed);
    emit changed();
    return true;
}

void CompanionController::cancel() {
    if (backend_ != nullptr) {
        backend_->cancel();
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
    messages_.clear();
    usage_.clear();
}

bool CompanionController::askApproval(const mcp::ApprovalRequest& request) {
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

QString CompanionController::prepareClaudeCode() {
    const auto refusal = setBridgeEnabled(true);
    if (!refusal.isEmpty()) {
        return refusal;
    }
    if (auto* clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(connectionCommand());
    }
    return {};
}

} // namespace openstitch::desktop_v2::studio
