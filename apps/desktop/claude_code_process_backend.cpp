// SPDX-License-Identifier: Apache-2.0
#include "claude_code_process_backend.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>

namespace openstitch::desktop {

ClaudeCodeProcessBackend::ClaudeCodeProcessBackend(QString executable, QString workspace,
                                                   QObject* parent)
    : CompanionBackend(parent), executable_(std::move(executable)),
      workspace_(std::move(workspace)) {}

ClaudeCodeProcessBackend::~ClaudeCodeProcessBackend() {
    if (process_ != nullptr) {
        process_->kill();
        process_->waitForFinished(500);
    }
}

bool ClaudeCodeProcessBackend::available() const {
    return !executable_.isEmpty() && !workspace_.isEmpty();
}

void ClaudeCodeProcessBackend::resetConversation() {
    cancel();
    sessionId_.clear();
    reportedToolIds_.clear();
}

void ClaudeCodeProcessBackend::send(const QString& userText) {
    if (!available() || userText.trimmed().isEmpty() || process_ != nullptr) {
        emit failed(tr("Claude Code n'est pas pret ou une demande est deja en cours."));
        emit finished();
        return;
    }
    cancelled_ = false;
    resultSeen_ = false;
    buffer_.clear();
    reportedToolIds_.clear();

    // QProcess execute la CLI en sous-processus ; aucun texte utilisateur ne passe dans
    // les arguments du shell. Le prompt est fourni par stdin.
    auto* proc = new QProcess(this); // security-ok: CLI installee par l'utilisateur, action explicite
    process_ = proc;
    proc->setWorkingDirectory(workspace_);
    proc->setProcessChannelMode(QProcess::SeparateChannels);
    QStringList args = {QStringLiteral("-p"), QStringLiteral("--verbose"),
                        QStringLiteral("--output-format"), QStringLiteral("stream-json"),
                        QStringLiteral("--max-turns"), QStringLiteral("12"),
                        QStringLiteral("--allowedTools"), QStringLiteral("mcp__openstitch"),
                        QStringLiteral("--disallowedTools"),
                        QStringLiteral("Bash,Edit,Write,NotebookEdit")};
    // Ne jamais transmettre au wrapper cmd un identifiant non controle provenant du JSON.
    const QRegularExpression uuid(
        QStringLiteral("^[0-9a-fA-F]{8}(-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}$"));
    if (uuid.match(sessionId_).hasMatch()) {
        args << QStringLiteral("--resume") << sessionId_;
    } else {
        sessionId_.clear();
    }
    QString program = executable_;
#ifdef _WIN32
    if (executable_.endsWith(QStringLiteral(".cmd"), Qt::CaseInsensitive)) {
        // Le parseur de cmd.exe n'a pas les memes regles que QProcess : refuser
        // les chemins contenant des caracteres interpretes par le shell Windows.
        if (executable_.contains(QRegularExpression(QStringLiteral("[&|<>^%()]")))) {
            emit failed(tr("Chemin Claude Code incompatible avec le mode integre. "
                           "Utilisez le terminal externe."));
            process_ = nullptr;
            proc->deleteLater();
            emit finished();
            return;
        }
        // Les installations npm placent parfois un wrapper .cmd au lieu d'un .exe natif.
        // AUCUNE entree utilisateur n'est envoyee a cmd.exe : stdin seulement.
        program = QStringLiteral("cmd.exe");
        args.prepend(executable_);
        args.prepend(QStringLiteral("/c"));
        args.prepend(QStringLiteral("/d"));
    }
#endif
    connect(proc, &QProcess::started, this, [this, proc, userText] {
        if (process_ != proc) {
            return;
        }
        proc->write(userText.toUtf8());
        proc->closeWriteChannel();
    });
    connect(proc, &QProcess::readyReadStandardOutput, this, [this] { handleOutput(); });
    connect(proc, &QProcess::errorOccurred, this,
            [this, proc](QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart || process_ != proc) {
                    return;
                }
                if (!cancelled_) {
                    emit failed(tr("Impossible de demarrer Claude Code. Verifiez son installation."));
                }
                process_ = nullptr;
                proc->deleteLater();
                emit finished();
            });
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc](int code, QProcess::ExitStatus status) {
                if (process_ != proc) {
                    return;
                }
                handleOutput();
                if (!buffer_.trimmed().isEmpty()) {
                    handleLine(buffer_.trimmed()); // Claude Code peut omettre le dernier LF.
                }
                buffer_.clear();
                if (!cancelled_ && !resultSeen_) {
                    if (status == QProcess::NormalExit && code == 0) {
                        emit failed(tr("Claude Code n'a pas renvoye de resultat JSON."));
                    } else {
                        emit failed(tr("Claude Code s'est termine avec une erreur (code %1).")
                                        .arg(code));
                    }
                }
                process_ = nullptr;
                proc->deleteLater();
                emit finished();
            });
    proc->start(program, args);
}

void ClaudeCodeProcessBackend::handleOutput() {
    if (process_ == nullptr) {
        return;
    }
    buffer_ += process_->readAllStandardOutput();
    if (buffer_.size() > 4 * 1024 * 1024) {
        emit failed(tr("Flux de Claude Code trop volumineux."));
        cancel();
        return;
    }
    qsizetype pos = 0;
    while ((pos = buffer_.indexOf('\n')) >= 0) {
        const QByteArray line = buffer_.left(pos).trimmed();
        buffer_.remove(0, pos + 1);
        if (!line.isEmpty()) {
            handleLine(line);
        }
    }
}

void ClaudeCodeProcessBackend::ingestEventForTest(const QByteArray& jsonLine) {
    handleLine(jsonLine);
}

void ClaudeCodeProcessBackend::handleLine(const QByteArray& line) {
    if (cancelled_) {
        return;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return; // stderr est hors protocole ; ne jamais afficher une ligne brute de stdout.
    }
    const QJsonObject event = document.object();
    const QString kind = event.value(QStringLiteral("type")).toString();
    if (kind == QLatin1String("assistant")) {
        const auto blocks = event.value(QStringLiteral("message"))
                                .toObject().value(QStringLiteral("content")).toArray();
        for (const auto& value : blocks) {
            const auto block = value.toObject();
            if (block.value(QStringLiteral("type")).toString() != QLatin1String("tool_use")) {
                continue;
            }
            const QString id = block.value(QStringLiteral("id")).toString();
            if (id.isEmpty() || reportedToolIds_.contains(id)) {
                continue;
            }
            reportedToolIds_.insert(id);
            CompanionToolCall tool;
            tool.tool = block.value(QStringLiteral("name")).toString();
            tool.summary = tr("Outil appele (validation dans OpenStitch si modification).");
            emit toolCalled(tool);
        }
        return;
    }
    if (kind != QLatin1String("result")) {
        return;
    }
    resultSeen_ = true;
    if (event.value(QStringLiteral("is_error")).toBool(false)) {
        emit failed(tr("Claude Code a signale une erreur pendant la demande."));
        return;
    }
    const QString session = event.value(QStringLiteral("session_id")).toString();
    if (!session.isEmpty()) {
        sessionId_ = session;
    }
    const QString result = event.value(QStringLiteral("result")).toString();
    if (!result.isEmpty()) {
        emit assistantText(result);
    }
    const auto usage = event.value(QStringLiteral("usage")).toObject();
    emit this->usage(static_cast<quint64>(usage.value(QStringLiteral("input_tokens")).toDouble()),
                     static_cast<quint64>(usage.value(QStringLiteral("output_tokens")).toDouble()));
}

void ClaudeCodeProcessBackend::cancel() {
    cancelled_ = true;
    if (process_ != nullptr) {
        process_->kill();
    }
}

void ClaudeCodeProcessBackend::decide(bool) {
    // L'approbation est entierement geree par le pont live dans le dock.
}

} // namespace openstitch::desktop
