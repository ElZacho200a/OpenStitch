// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QByteArray>
#include <QPointer>
#include <QSet>
#include <QString>

#include "companion_backend.hpp"

// security-ok: processus Claude Code de l'utilisateur, outils MCP approuves
class QProcess;

namespace openstitch::desktop {

// Fournisseur du dock via la CLI Claude Code deja installee et authentifiee.
// N'utilise pas l'API Anthropic directe ; tous les outils OpenStitch passent par MCP --live.
// Un processus headless par message, resume par session_id et toujours hors du fil GUI.
class ClaudeCodeProcessBackend final : public CompanionBackend {
    Q_OBJECT

public:
    ClaudeCodeProcessBackend(QString executable, QString workspace, QObject* parent = nullptr);
    ~ClaudeCodeProcessBackend() override;

    [[nodiscard]] QString providerName() const override {
        return QStringLiteral("Claude Code (MCP)");
    }
    [[nodiscard]] bool available() const override;
    void resetConversation();
    // Expose uniquement le decodeur d'evenements pour les tests sans executable Claude.
    void ingestEventForTest(const QByteArray& jsonLine);

public slots:
    void send(const QString& userText) override;
    void cancel() override;
    void decide(bool approved) override;

private:
    void handleOutput();
    void handleLine(const QByteArray& line);

    QString executable_;
    QString workspace_;
    QString sessionId_;
    // security-ok: processus Claude Code de l'utilisateur, outils MCP approuves
    QPointer<QProcess> process_;
    QByteArray buffer_;
    QSet<QString> reportedToolIds_;
    bool cancelled_{false};
    bool resultSeen_{false};
};

} // namespace openstitch::desktop
