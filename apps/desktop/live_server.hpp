// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <QObject>
#include <QPointer>

#include "openstitch/mcp/live.hpp"

class QLocalServer;
class QLocalSocket;
class QWidget;

namespace openstitch::desktop {

// Approbation d'une modification demandée par un agent (ADR 0001 §6) : boîte de dialogue modale
// tant que le dock compagnon n'existe pas. `setOverride` permet aux tests (QTest headless) de
// répondre sans fenêtre. Aucune logique métier ici : la politique est dans `libs/mcp`.
class DialogApprovalGate final : public mcp::IApprovalGate {
public:
    explicit DialogApprovalGate(QWidget* parent) : parent_(parent) {}
    bool approve(const mcp::ApprovalRequest& request) override;
    void setOverride(std::function<bool(const mcp::ApprovalRequest&)> fn) {
        override_ = std::move(fn);
    }
    // Interface d'approbation préférée (le dock compagnon) ; la boîte modale sert de repli.
    void setDelegate(std::function<bool(const mcp::ApprovalRequest&)> fn) {
        delegate_ = std::move(fn);
    }
    void setChoiceDelegate(std::function<int(const mcp::ChoiceRequest&)> fn) {
        choice_ = std::move(fn);
    }
    int choose(const mcp::ChoiceRequest& request) override;

private:
    QWidget* parent_;
    std::function<bool(const mcp::ApprovalRequest&)> override_;
    std::function<bool(const mcp::ApprovalRequest&)> delegate_;
    std::function<int(const mcp::ChoiceRequest&)> choice_;
};

// Serveur local du pont live : un tube nommé / socket de domaine Unix accessible à l'utilisateur
// courant seul (aucun port réseau), un jeton aléatoire par instance dans un fichier de
// découverte lisible par l'utilisateur seul. Les requêtes sont servies sur le fil GUI (seul
// maître du document) ; les calculs d'outils tournent sur une copie, dans le pool de threads, et
// ne bloquent jamais l'interface.
class LiveServer final : public QObject {
    Q_OBJECT

public:
    LiveServer(mcp::LiveService& service, QObject* parent = nullptr);
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

} // namespace openstitch::desktop
