// SPDX-License-Identifier: Apache-2.0
#include "claude_code_process_backend.hpp"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
// security-ok: processus Claude Code de l'utilisateur, outils MCP approuves
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

bool ClaudeCodeProcessBackend::running() const {
    return !process_.isNull();
}

bool ClaudeCodeProcessBackend::available() const {
    return !executable_.isEmpty() && !workspace_.isEmpty();
}

void ClaudeCodeProcessBackend::resetConversation() {
    cancel();
    sessionId_.clear();
    reportedToolIds_.clear();
    reportedAssistantIds_.clear();
    streamedText_ = false;
}

// Les outils natifs n'ont pas a etre exposes par le dock Claude Code. Ce refus
// explicite complete --tools "" et le catalogue MCP strict (defense en profondeur).
QStringList ClaudeCodeProcessBackend::disallowedTools() {
    return {
        QStringLiteral("Bash"),      QStringLiteral("PowerShell"),   QStringLiteral("Edit"),
        QStringLiteral("Write"),     QStringLiteral("NotebookEdit"), QStringLiteral("WebFetch"),
        QStringLiteral("WebSearch"), QStringLiteral("Task"),         QStringLiteral("Agent"),
        QStringLiteral("TodoWrite"), QStringLiteral("BashOutput"),   QStringLiteral("KillShell")};
}

QStringList ClaudeCodeProcessBackend::restrictedArguments(const QString& workspace,
                                                          const QString& sessionId) {
    // Lecture seule du disque (Read, Glob, Grep) : chaque accès hors de l'espace de travail passe
    // par l'outil de permission du pont (`live_permission`), qui demande l'accord de
    // l'utilisateur dans OpenStitch ; les chemins de secrets sont refusés d'office. Aucun outil
    // d'écriture ni d'exécution. --strict-mcp-config limite les serveurs MCP à celui du workspace.
    QStringList args = {QStringLiteral("-p"),
                        QStringLiteral("--verbose"),
                        QStringLiteral("--output-format"),
                        QStringLiteral("stream-json"),
                        QStringLiteral("--include-partial-messages"),
                        QStringLiteral("--max-turns"),
                        QStringLiteral("40"),
                        QStringLiteral("--tools"),
                        QStringLiteral("Read,Glob,Grep"),
                        QStringLiteral("--mcp-config"),
                        QDir(workspace).filePath(QStringLiteral(".mcp.json")),
                        QStringLiteral("--strict-mcp-config"),
                        QStringLiteral("--permission-mode"),
                        QStringLiteral("default"),
                        QStringLiteral("--permission-prompt-tool"),
                        QStringLiteral("mcp__openstitch__live_permission"),
                        QStringLiteral("--allowedTools"),
                        QStringLiteral("mcp__openstitch__*"),
                        QStringLiteral("--disallowedTools"),
                        disallowedTools().join(QLatin1Char(','))};
    const QRegularExpression uuid(
        QStringLiteral("^[0-9a-fA-F]{8}(-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}$"));
    if (uuid.match(sessionId).hasMatch()) {
        args << QStringLiteral("--resume") << sessionId;
    }
    return args;
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
    reportedAssistantIds_.clear();
    streamedText_ = false;

    // QProcess execute la CLI en sous-processus ; aucun texte utilisateur ne passe dans
    // les arguments du shell. Le prompt est fourni par stdin.
    // security-ok: processus Claude Code de l'utilisateur, outils MCP approuves
    auto* proc = new QProcess(this);
    process_ = proc;
    proc->setWorkingDirectory(workspace_);
    // security-ok: processus Claude Code de l'utilisateur, outils MCP approuves
    proc->setProcessChannelMode(QProcess::SeparateChannels);
    QStringList args = restrictedArguments(workspace_, sessionId_);
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
    // security-ok: processus Claude Code de l'utilisateur, outils MCP approuves
    connect(proc, &QProcess::started, this, [this, proc, userText] {
        if (process_ != proc) {
            return;
        }
        proc->write(userText.toUtf8());
        proc->closeWriteChannel();
    });
    // security-ok: processus Claude Code de l'utilisateur, outils MCP approuves
    connect(proc, &QProcess::readyReadStandardOutput, this, [this] { handleOutput(); });
    // security-ok: processus Claude Code de l'utilisateur, outils MCP approuves
    connect(proc, &QProcess::errorOccurred, this,
            // security-ok: processus Claude Code de l'utilisateur, outils MCP approuves
            [this, proc](QProcess::ProcessError error) {
                // security-ok: processus Claude Code de l'utilisateur, outils MCP approuves
                if (error != QProcess::FailedToStart || process_ != proc) {
                    return;
                }
                if (!cancelled_) {
                    emit failed(
                        tr("Impossible de demarrer Claude Code. Verifiez son installation."));
                }
                process_ = nullptr;
                proc->deleteLater();
                emit finished();
            });
    // security-ok: processus Claude Code de l'utilisateur, outils MCP approuves
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            // security-ok: processus Claude Code de l'utilisateur, outils MCP approuves
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
                    // security-ok: processus Claude Code de l'utilisateur, outils MCP approuves
                    if (status == QProcess::NormalExit && code == 0) {
                        emit failed(tr("Claude Code n'a pas renvoye de resultat JSON."));
                    } else {
                        // La fin de stderr dit pourquoi (outil MCP inconnu, option refusee...).
                        const QString why =
                            QString::fromUtf8(proc->readAllStandardError()).trimmed().right(400);
                        emit failed(
                            tr("Claude Code s'est termine avec une erreur (code %1).").arg(code) +
                            (why.isEmpty() ? QString() : QStringLiteral(" ") + why));
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
    consumeBuffer();
}

// Une ligne COMPLETE peut etre tres grosse (les evenements « user » rejouent les resultats
// d'outils, donc les rendus PNG en base64) : seule une ligne INCOMPLETE demesuree est une erreur.
void ClaudeCodeProcessBackend::consumeBuffer() {
    constexpr qsizetype kMaxPendingLine = 64 * 1024 * 1024;
    qsizetype pos = 0;
    while ((pos = buffer_.indexOf('\n')) >= 0) {
        const QByteArray line = buffer_.left(pos).trimmed();
        buffer_.remove(0, pos + 1);
        if (line.isEmpty()) {
            continue;
        }
        // Les echos de resultats d'outils (type « user ») ne servent a rien ici : pas d'analyse
        // JSON d'un contenu de plusieurs Mo.
        if (line.startsWith("{\"type\":\"user\"")) {
            continue;
        }
        handleLine(line);
    }
    if (buffer_.size() > kMaxPendingLine) {
        emit failed(tr("Flux de Claude Code trop volumineux."));
        cancel();
    }
}

void ClaudeCodeProcessBackend::ingestChunkForTest(const QByteArray& chunk) {
    buffer_ += chunk;
    consumeBuffer();
}

void ClaudeCodeProcessBackend::ingestEventForTest(const QByteArray& jsonLine) {
    handleLine(jsonLine);
}

// Message actionnable pour un evenement `result` en erreur : sous-type (limite de tours,
// execution…) puis debut du texte renvoye par Claude Code (jamais le flux brut entier).
QString ClaudeCodeProcessBackend::describeError(const QJsonObject& event) {
    const QString subtype = event.value(QStringLiteral("subtype")).toString();
    QString detail = event.value(QStringLiteral("result")).toString().simplified();
    if (detail.size() > 300) {
        detail = detail.left(300) + QStringLiteral("…");
    }
    QString message;
    if (subtype == QLatin1String("error_max_turns")) {
        message = tr("Claude Code a atteint la limite de tours de la demande : reformulez en "
                     "plusieurs demandes plus courtes.");
    } else {
        message = tr("Claude Code a signale une erreur pendant la demande");
        if (!subtype.isEmpty()) {
            message += tr(" (%1)").arg(subtype);
        }
        message += QLatin1Char('.');
    }
    if (!detail.isEmpty()) {
        message += QLatin1Char(' ') + detail;
    }
    return message;
}

// « mcp__openstitch__project_check » -> « project_check » ; outils natifs inchangés.
QString ClaudeCodeProcessBackend::friendlyToolName(const QString& raw) {
    static const QString prefix = QStringLiteral("mcp__openstitch__");
    return raw.startsWith(prefix) ? raw.mid(prefix.size()) : raw;
}

// Paramètres utiles d'un appel d'outil, courts et sans contenu volumineux : chemins, motifs,
// nombres et petits textes ; les tableaux deviennent « n éléments ».
QString ClaudeCodeProcessBackend::compactInput(const QJsonObject& input) {
    QStringList parts;
    for (auto it = input.begin(); it != input.end() && parts.size() < 4; ++it) {
        const QJsonValue& v = it.value();
        QString shown;
        if (v.isString()) {
            shown = v.toString().simplified();
            if (shown.size() > 70) {
                shown = shown.left(67) + QStringLiteral("…");
            }
        } else if (v.isDouble() || v.isBool()) {
            shown = v.isBool() ? (v.toBool() ? QStringLiteral("oui") : QStringLiteral("non"))
                               : QString::number(v.toDouble());
        } else if (v.isArray()) {
            shown = tr("%n élément(s)", "", v.toArray().size());
        } else if (v.isObject()) {
            shown = tr("%n réglage(s)", "", v.toObject().size());
        }
        if (!shown.isEmpty()) {
            parts << it.key() + QStringLiteral(" = ") + shown;
        }
    }
    return parts.join(QStringLiteral(", "));
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
        const QJsonObject message = event.value(QStringLiteral("message")).toObject();
        const QString messageId = message.value(QStringLiteral("id")).toString();
        // La CLI émet un événement par bloc sous le MÊME id de message (texte puis tool_use),
        // et peut réémettre un bloc : la déduplication se fait par bloc, jamais par message,
        // sinon l'appel d'outil qui suit un texte disparaît.
        const auto firstTime = [this, &messageId](const QString& kindTag, const QString& body) {
            if (messageId.isEmpty()) {
                return true;
            }
            const QString key = messageId + QLatin1Char('|') + kindTag + QLatin1Char('|') + body;
            if (reportedAssistantIds_.contains(key)) {
                return false;
            }
            reportedAssistantIds_.insert(key);
            return true;
        };
        const auto blocks = message.value(QStringLiteral("content")).toArray();
        for (const auto& value : blocks) {
            const auto block = value.toObject();
            const QString blockType = block.value(QStringLiteral("type")).toString();
            if (blockType == QLatin1String("text")) {
                const QString text = block.value(QStringLiteral("text")).toString();
                if (!text.trimmed().isEmpty() && firstTime(blockType, text)) {
                    streamedText_ = true;
                    // Afficher au fil de l'eau, avant l'evenement result.
                    emit assistantText(text);
                }
                continue;
            }
            if (blockType == QLatin1String("thinking")) {
                const QString thought =
                    block.value(QStringLiteral("thinking")).toString().trimmed();
                if (!thought.isEmpty() && firstTime(blockType, thought)) {
                    emit thinking(thought);
                }
                continue;
            }
            if (blockType != QLatin1String("tool_use")) {
                continue;
            }
            const QString id = block.value(QStringLiteral("id")).toString();
            if (id.isEmpty() || reportedToolIds_.contains(id)) {
                continue;
            }
            reportedToolIds_.insert(id);
            CompanionToolCall tool;
            tool.tool = block.value(QStringLiteral("name")).toString();
            const QJsonObject input = block.value(QStringLiteral("input")).toObject();
            tool.summary = compactInput(input);
            if (input.value(QStringLiteral("query")).isString()) {
                const QString requested = input.value(QStringLiteral("query")).toString().left(120);
                tool.summary = tr("Cible : %1").arg(requested);
            } else if (input.value(QStringLiteral("objects")).isArray()) {
                const int count = input.value(QStringLiteral("objects")).toArray().size();
                tool.summary = tr("%n objet(s) concernés", "", count);
            } else if (input.value(QStringLiteral("operations")).isArray()) {
                const int count = input.value(QStringLiteral("operations")).toArray().size();
                const QString action = tr("%n opération(s) proposées", "", count);
                tool.summary = tr("%1 (approbation dans OpenStitch)").arg(action);
            }
            emit toolCalled(tool);
        }
        return;
    }
    if (kind == QLatin1String("stream_event")) {
        // Flux partiel : seulement de quoi dire ce que fait l'agent, sans dupliquer le contenu
        // (les blocs complets arrivent dans les messages « assistant »).
        const QJsonObject inner = event.value(QStringLiteral("event")).toObject();
        if (inner.value(QStringLiteral("type")).toString() ==
            QLatin1String("content_block_start")) {
            const QJsonObject block = inner.value(QStringLiteral("content_block")).toObject();
            const QString blockType = block.value(QStringLiteral("type")).toString();
            if (blockType == QLatin1String("thinking")) {
                emit activity(tr("Réflexion…"));
            } else if (blockType == QLatin1String("text")) {
                emit activity(tr("Rédaction de la réponse…"));
            } else if (blockType == QLatin1String("tool_use")) {
                emit activity(
                    tr("Appel de %1…")
                        .arg(friendlyToolName(block.value(QStringLiteral("name")).toString())));
            }
        }
        return;
    }
    if (kind != QLatin1String("result")) {
        return;
    }
    resultSeen_ = true;
    if (event.value(QStringLiteral("is_error")).toBool(false)) {
        emit failed(describeError(event));
        return;
    }
    const QString session = event.value(QStringLiteral("session_id")).toString();
    if (!session.isEmpty()) {
        sessionId_ = session;
    }
    const QString result = event.value(QStringLiteral("result")).toString();
    // Le resultat final repete souvent les textes deja recus dans les messages assistant.
    if (!streamedText_ && !result.isEmpty()) {
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
