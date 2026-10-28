// SPDX-License-Identifier: Apache-2.0
#include "live_server.hpp"

#include <QCoreApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QPushButton>
#include <QThreadPool>

namespace openstitch::desktop {

namespace {

constexpr qsizetype kMaxLineBytes = 8 * 1024 * 1024; // une requête plus grosse est un abus

} // namespace

bool DialogApprovalGate::approve(const mcp::ApprovalRequest& request) {
    if (override_) {
        return override_(request);
    }
    if (delegate_) {
        return delegate_(request);
    }
    QMessageBox box(QMessageBox::Question, QObject::tr("Modification demandée par un agent IA"),
                    QObject::tr("Un agent IA demande de modifier le projet ouvert (« %1 »).\n"
                                "L'opération sera annulable en une seule étape (Ctrl+Z).")
                        .arg(QString::fromStdString(request.label)),
                    QMessageBox::NoButton, parent_);
    QString details = QString::fromStdString(request.report_json);
    if (details.size() > 4000) {
        details = details.left(4000) + QStringLiteral("…");
    }
    box.setDetailedText(details);
    auto* apply = box.addButton(QObject::tr("Appliquer"), QMessageBox::AcceptRole);
    auto* refuse = box.addButton(QObject::tr("Refuser"), QMessageBox::RejectRole);
    box.setDefaultButton(refuse);
    box.setEscapeButton(refuse);
    box.exec();
    return box.clickedButton() == apply;
}

int DialogApprovalGate::choose(const mcp::ChoiceRequest& request) {
    if (choice_) {
        return choice_(request);
    }
    return -1; // sans interface de choix, l'agent n'obtient aucune réponse : il ne modifie rien
}

LiveServer::LiveServer(mcp::LiveService& service, QObject* parent)
    : QObject(parent), service_(service) {}

LiveServer::~LiveServer() {
    stop();
}

QString LiveServer::endpoint() const {
    return server_ != nullptr ? server_->fullServerName() : QString();
}

bool LiveServer::start(const std::filesystem::path& dir, QString* error) {
    if (server_ != nullptr) {
        return true;
    }
    token_ = mcp::make_live_token();
    host_ = std::make_shared<mcp::LiveHost>(service_, token_);
    const QString name = QStringLiteral("openstitch-live-%1-%2")
                             .arg(QCoreApplication::applicationPid())
                             .arg(QString::fromStdString(token_.substr(0, 8)));
    auto* server = new QLocalServer(this);
    // Tube / socket accessible à l'utilisateur courant seul : jamais d'autre compte.
    server->setSocketOptions(QLocalServer::UserAccessOption);
    QLocalServer::removeServer(name);
    if (!server->listen(name)) {
        if (error != nullptr) {
            *error = server->errorString();
        }
        delete server;
        host_.reset();
        return false;
    }
    mcp::LiveEndpoint info;
    info.pid = static_cast<int>(QCoreApplication::applicationPid());
    info.endpoint = server->fullServerName().toStdString();
    info.token = token_;
    if (!mcp::write_live_endpoint(dir, info)) {
        if (error != nullptr) {
            *error = tr("Impossible d'écrire le fichier de découverte dans %1")
                         .arg(QString::fromStdString(dir.string()));
        }
        server->close();
        delete server;
        host_.reset();
        return false;
    }
    dir_ = dir;
    server_ = server;
    connect(server_, &QLocalServer::newConnection, this, &LiveServer::onNewConnection);
    return true;
}

void LiveServer::stop() {
    if (server_ == nullptr) {
        return;
    }
    mcp::remove_live_endpoint(dir_, static_cast<int>(QCoreApplication::applicationPid()));
    server_->close();
    delete server_;
    server_ = nullptr;
    host_.reset(); // les calculs en vol finissent sur leur copie ; leur réponse est abandonnée
}

void LiveServer::onNewConnection() {
    while (server_ != nullptr && server_->hasPendingConnections()) {
        QLocalSocket* socket = server_->nextPendingConnection();
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
    }
}

void LiveServer::onReadyRead(QLocalSocket* socket) {
    QByteArray buffer = socket->property("buf").toByteArray() + socket->readAll();
    if (buffer.size() > kMaxLineBytes && !buffer.contains('\n')) {
        socket->disconnectFromServer();
        return;
    }
    qsizetype nl = 0;
    while ((nl = buffer.indexOf('\n')) >= 0 && host_) {
        const std::string line = buffer.left(nl).toStdString();
        buffer.remove(0, nl + 1);
        serve(socket, line);
    }
    socket->setProperty("buf", buffer);
}

void LiveServer::serve(QLocalSocket* socket, const std::string& line) {
    auto reply = std::make_shared<mcp::LiveHost::Reply>(host_->begin(line));
    const auto send = [](QPointer<QLocalSocket> target, const std::string& out) {
        if (target != nullptr) {
            target->write(QByteArray::fromStdString(out) + '\n');
            target->flush();
        }
    };
    if (!reply->call) {
        send(socket, reply->immediate);
        return;
    }
    // Calcul sur la copie figée hors du fil GUI ; approbation et commit reviennent sur le fil GUI.
    QPointer<LiveServer> self(this);
    QPointer<QLocalSocket> target(socket);
    // L'approbation peut faire fermer le pont depuis une boucle d'evenements imbriquee.
    // Conserver l'hote jusqu'a la fin du retour GUI pour eviter un use-after-free.
    const auto host = host_;
    QThreadPool::globalInstance()->start([self, target, reply, send, host] {
        reply->call->run();
        QMetaObject::invokeMethod(
            qApp,
            [self, target, reply, send, host] {
                if (self == nullptr || self->host_ != host) {
                    return; // serveur arrêté ou application en fermeture : réponse abandonnée
                }
                send(target, host->finish(*reply));
            },
            Qt::QueuedConnection);
    });
}

} // namespace openstitch::desktop
