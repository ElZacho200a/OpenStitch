// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <functional>

#include <QString>
#include <QStringList>

namespace openstitch::desktop {

// Ouvre la CLI Claude Code de l'UTILISATEUR (installée par lui, jamais redistribuée) dans un
// espace de travail généré par OpenStitch : serveur MCP --live déclaré, consignes injectées par
// hook, skills. Action volontaire (menu Aide) ; aucun lancement automatique. Les dépendances
// système sont injectables pour les tests.
class ClaudeCodeLauncher {
public:
    struct Env {
        std::function<QString(const QString& name)> findExecutable;
        std::function<bool(const QString& program, const QStringList& args, const QString& workDir)>
            startDetached;
        QString appDir;  // dossier de l'exécutable OpenStitch
        QString dataDir; // dossier de données utilisateur (espace de travail dessous)
    };
    enum class Result { Started, ClaudeNotFound, McpNotFound, WorkspaceFailed, LaunchFailed };

    [[nodiscard]] static Env systemEnv();
    [[nodiscard]] static Result launch(const Env& env);
    // Prepare un workspace sans ouvrir un terminal : utilise par le chat Claude Code du dock.
    [[nodiscard]] static Result prepare(const Env& env, QString& executable, QString& workspace);
    // Dossier de l'espace de travail pour `dataDir`.
    [[nodiscard]] static QString workspaceDir(const QString& dataDir);
    // Message utilisateur pour un résultat d'échec.
    [[nodiscard]] static QString describe(Result result);
};

} // namespace openstitch::desktop
