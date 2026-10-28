// SPDX-License-Identifier: Apache-2.0
#include "companion_launcher.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <QProcess> // security-ok: lancement volontaire de la CLI de l'utilisateur (menu Aide)
#include <QStandardPaths>

#include "openstitch/agent/agent.hpp"

namespace openstitch::desktop {

QString ClaudeCodeLauncher::workspaceDir(const QString& dataDir) {
    return QDir(dataDir).filePath(
        QStringLiteral("companion/workspace-%1").arg(QCoreApplication::applicationPid()));
}

ClaudeCodeLauncher::Env ClaudeCodeLauncher::systemEnv() {
    Env env;
    env.findExecutable = [](const QString& name) { return QStandardPaths::findExecutable(name); };
    env.startDetached = [](const QString& program, const QStringList& args,
                           const QString& workDir) {
        // security-ok: action explicite de l'utilisateur, programme = terminal ou claude trouves
        return QProcess::startDetached(program, args, workDir);
    };
    env.appDir = QCoreApplication::applicationDirPath();
    env.dataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return env;
}

namespace {

QString findMcpExe(const QString& appDir) {
    const QStringList candidates = {
        QDir(appDir).filePath(QStringLiteral("mcp/openstitch-mcp.exe")), // installeur
        QDir(appDir).filePath(QStringLiteral("openstitch-mcp.exe")),
        QDir(appDir).filePath(QStringLiteral("../../mcp/Debug/openstitch-mcp.exe")), // build dev
        QDir(appDir).filePath(QStringLiteral("../../mcp/Release/openstitch-mcp.exe")),
        QDir(appDir).filePath(QStringLiteral("openstitch-mcp"))};
    for (const QString& c : candidates) {
        if (QFileInfo::exists(c)) {
            return QDir::cleanPath(QFileInfo(c).absoluteFilePath());
        }
    }
    return {};
}

} // namespace

ClaudeCodeLauncher::Result ClaudeCodeLauncher::prepare(const Env& env, QString& executable,
                                                       QString& workspace) {
    QString claude = env.findExecutable(QStringLiteral("claude"));
    if (claude.isEmpty()) {
        claude = env.findExecutable(QStringLiteral("claude.cmd"));
    }
    if (claude.isEmpty()) {
        return Result::ClaudeNotFound;
    }
    const QString mcp = findMcpExe(env.appDir);
    if (mcp.isEmpty()) {
        return Result::McpNotFound;
    }
    const QString dir = workspaceDir(env.dataDir);
    QDir().mkpath(dir);
    if (!agent::write_claude_workspace(dir.toUtf8().toStdString(), mcp.toUtf8().toStdString(),
                                      static_cast<int>(QCoreApplication::applicationPid()))) {
        return Result::WorkspaceFailed;
    }
    executable = claude;
    workspace = dir;
    return Result::Started;
}

ClaudeCodeLauncher::Result ClaudeCodeLauncher::launch(const Env& env) {
    QString claude;
    QString dir;
    const Result ready = prepare(env, claude, dir);
    if (ready != Result::Started) {
        return ready;
    }
    const QString wt = env.findExecutable(QStringLiteral("wt"));
    bool started = false;
    if (!wt.isEmpty()) {
        started = env.startDetached(wt, {QStringLiteral("-d"), dir, claude}, dir);
    } else {
#ifdef _WIN32
        started = env.startDetached(QStringLiteral("cmd.exe"),
                                    {QStringLiteral("/c"), QStringLiteral("start"),
                                     QStringLiteral("Claude Code"), QStringLiteral("/D"), dir,
                                     QStringLiteral("cmd"), QStringLiteral("/k"), claude},
                                    dir);
#else
        started = env.startDetached(QStringLiteral("x-terminal-emulator"),
                                    {QStringLiteral("-e"), claude}, dir);
#endif
    }
    return started ? Result::Started : Result::LaunchFailed;
}

QString ClaudeCodeLauncher::describe(Result result) {
    switch (result) {
    case Result::Started:
        return {};
    case Result::ClaudeNotFound:
        return QObject::tr("Claude Code n'est pas installé (commande « claude » introuvable). "
                           "Installez-le depuis claude.com/claude-code puis réessayez : "
                           "OpenStitch ne le fournit pas.");
    case Result::McpNotFound:
        return QObject::tr("openstitch-mcp est introuvable à côté de l'application.");
    case Result::WorkspaceFailed:
        return QObject::tr("Impossible de préparer l'espace de travail de Claude Code.");
    case Result::LaunchFailed:
        return QObject::tr("Le terminal n'a pas pu être lancé.");
    }
    return {};
}

} // namespace openstitch::desktop
