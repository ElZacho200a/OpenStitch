// SPDX-License-Identifier: Apache-2.0
#include "chat_panel.hpp"

#include <QColor>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace openstitch::desktop_v2 {

ChatPanel::ChatPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("chatPanel");
    auto* root = new QVBoxLayout(this);

    status_ = new QLabel(this);
    status_->setObjectName("chatStatus");
    status_->setWordWrap(true);
    root->addWidget(status_);

    log_ = new QListWidget(this);
    log_->setObjectName("chatLog");
    log_->setWordWrap(true);
    log_->setSelectionMode(QAbstractItemView::NoSelection);
    root->addWidget(log_, 1);

    privacy_ = new QLabel(
        tr("Ce que vous écrivez et les résultats d'outils sont envoyés au fournisseur choisi. "
           "Chaque modification de votre document demande votre accord et s'annule d'un Ctrl+Z."),
        this);
    privacy_->setObjectName("chatPrivacy");
    privacy_->setWordWrap(true);
    root->addWidget(privacy_);

    usage_ = new QLabel(this);
    usage_->setObjectName("chatUsage");
    root->addWidget(usage_);

    auto* row = new QHBoxLayout;
    input_ = new QLineEdit(this);
    input_->setObjectName("chatInput");
    input_->setPlaceholderText(tr("Demandez quelque chose à l'agent…"));
    send_ = new QPushButton(tr("Envoyer"), this);
    cancel_ = new QPushButton(tr("Annuler"), this);
    row->addWidget(input_, 1);
    row->addWidget(send_);
    row->addWidget(cancel_);
    root->addLayout(row);

    auto* bottom = new QHBoxLayout;
    configure_ = new QPushButton(tr("Configurer le fournisseur…"), this);
    reset_ = new QPushButton(tr("Nouvelle conversation"), this);
    bottom->addWidget(configure_);
    bottom->addWidget(reset_);
    bottom->addStretch(1);
    root->addLayout(bottom);

    connect(send_, &QPushButton::clicked, this, [this] {
        if (submit(input_->text())) {
            input_->clear();
        }
    });
    connect(input_, &QLineEdit::returnPressed, send_, &QPushButton::click);
    connect(cancel_, &QPushButton::clicked, this, [this] {
        if (backend_ != nullptr && busy_) {
            backend_->cancel();
        }
    });
    connect(configure_, &QPushButton::clicked, this, &ChatPanel::configureRequested);
    connect(reset_, &QPushButton::clicked, this, [this] {
        clearConversation();
        emit resetRequested();
    });
    refreshState();
}

void ChatPanel::setBackend(CompanionBackend* backend) {
    if (backend_ != nullptr) {
        disconnect(backend_, nullptr, this, nullptr);
    }
    backend_ = backend;
    busy_ = false;
    if (backend_ != nullptr) {
        connect(backend_, &CompanionBackend::assistantText, this,
                [this](const QString& text) { append(text, QStringLiteral("assistant")); });
        connect(backend_, &CompanionBackend::toolCalled, this,
                [this](const CompanionToolCall& call) {
                    append(QStringLiteral("%1 — %2").arg(call.tool, call.summary),
                           QStringLiteral("tool"), call.isError);
                    if (call.isError && !call.refusal.isEmpty()) {
                        append(call.refusal, QStringLiteral("tool"), true);
                    }
                });
        connect(backend_, &CompanionBackend::failed, this,
                [this](const QString& message) { append(message, QStringLiteral("error"), true); });
        connect(backend_, &CompanionBackend::usage, this, [this](quint64 in, quint64 out) {
            usage_->setText(tr("Jetons : %1 envoyés, %2 reçus.").arg(in).arg(out));
        });
        connect(backend_, &CompanionBackend::finished, this, [this] {
            busy_ = false;
            refreshState();
        });
    }
    refreshState();
}

void ChatPanel::clearConversation() {
    log_->clear();
    usage_->clear();
}

bool ChatPanel::submit(const QString& text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || busy_ || backend_ == nullptr || !backend_->available()) {
        return false;
    }
    append(trimmed, QStringLiteral("user"));
    busy_ = true;
    refreshState();
    backend_->send(trimmed);
    return true;
}

void ChatPanel::append(const QString& text, const QString& role, bool error) {
    const QString prefix = role == QLatin1String("user")        ? tr("Vous : ")
                           : role == QLatin1String("assistant") ? tr("Agent : ")
                           : role == QLatin1String("tool")      ? tr("Outil : ")
                                                                : tr("Erreur : ");
    auto* item = new QListWidgetItem(prefix + text);
    item->setData(Qt::UserRole, role);
    if (error) {
        item->setForeground(QColor(QStringLiteral("#B3261E")));
    }
    log_->addItem(item);
    log_->scrollToBottom();
}

void ChatPanel::refreshState() {
    const bool ready = backend_ != nullptr && backend_->available();
    if (backend_ == nullptr) {
        status_->setText(tr("Aucun fournisseur : activez le compagnon IA, puis configurez-le."));
    } else if (!ready) {
        status_->setText(tr("%1 n'est pas prêt : configurez le fournisseur (clé, activation).")
                             .arg(backend_->providerName()));
    } else if (busy_) {
        status_->setText(tr("%1 réfléchit…").arg(backend_->providerName()));
    } else {
        status_->setText(tr("Connecté à %1.").arg(backend_->providerName()));
    }
    input_->setEnabled(ready && !busy_);
    send_->setEnabled(ready && !busy_);
    cancel_->setEnabled(ready && busy_);
}

int ChatPanel::messageCount() const {
    return log_->count();
}

QString ChatPanel::messageText(int index) const {
    return index >= 0 && index < log_->count() ? log_->item(index)->text() : QString();
}

QString ChatPanel::statusText() const {
    return status_->text();
}

QString ChatPanel::usageText() const {
    return usage_->text();
}

} // namespace openstitch::desktop_v2
