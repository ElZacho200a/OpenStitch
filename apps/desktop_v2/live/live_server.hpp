// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include <QObject>
#include <QString>

#include "openstitch/mcp/live.hpp"

class QLocalServer;
class QLocalSocket;

namespace openstitch::desktop_v2 {

// Serveur local du pont live : un tube nommé / socket de domaine Unix accessible à l'utilisateur
// courant seul (aucun port réseau), un jeton aléatoire par instance dans un fichier de découverte
// lisible par l'utilisateur seul. Les requêtes sont servies sur le fil GUI (seul maître du
// document) ; les calculs d'outils tournent sur une copie, dans le pool de threads, et ne bloquent
// jamais l'interface. La politique (outils permis, approbation, révision) vit dans `libs/mcp`.
class LiveServer final : public QObject {
    Q_OBJECT

public:
    explicit LiveServer(mcp::LiveService& service, QObject* parent = nullptr);
    ~LiveServer() override;

    // `dir` : dossier du fichier de découverte (`mcp::live_directory()` en production).
    bool start(const std::filesystem::path& dir, QString* error = nullptr);
    void stop();
    [[nodiscard]] bool running() const { return server_ != nullptr; }
    [[nodiscard]] QString endpoint() const;
    [[nodiscard]] const std::string& token() const { return token_; }

private:
    void onNewConnection();
    void onReadyRead(QLocalSocket* socket);
    void serve(QLocalSocket* socket, const std::string& line);

    mcp::LiveService& service_;
    std::shared_ptr<mcp::LiveHost> host_;
    std::string token_;
    std::filesystem::path dir_;
    QLocalServer* server_{nullptr};
};

} // namespace openstitch::desktop_v2
