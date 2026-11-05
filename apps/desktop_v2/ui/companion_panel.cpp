// SPDX-License-Identifier: Apache-2.0
#include "companion_panel.hpp"

#include <QClipboard>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

namespace openstitch::desktop_v2 {

namespace {

QString fromUtf8(const std::string& s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}

QLabel* renderLabel(const std::string& png, const QString& caption, QWidget* parent) {
    auto* box = new QLabel(parent);
    box->setObjectName(QStringLiteral("approvalImage"));
    QPixmap pixmap;
    if (!png.empty() && pixmap.loadFromData(reinterpret_cast<const uchar*>(png.data()),
                                            static_cast<uint>(png.size()), "PNG")) {
        box->setPixmap(pixmap.scaled(260, 260, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        box->setText(caption + QStringLiteral(" : ") + QObject::tr("aperçu indisponible"));
    }
    box->setAlignment(Qt::AlignCenter);
    box->setToolTip(caption);
    return box;
}

} // namespace

bool askAgentApproval(QWidget* parent, const mcp::ApprovalRequest& request) {
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("agentApprovalDialog"));
    dialog.setWindowTitle(QObject::tr("Modification demandée par un agent IA"));
    auto* column = new QVBoxLayout(&dialog);
    auto* title = new QLabel(
        QObject::tr("Un agent IA propose de modifier le projet ouvert : <b>%1</b>.<br>"
                    "Rien n'est appliqué tant que vous n'avez pas validé. Une fois appliquée, la "
                    "modification est <b>une seule étape</b> annulable (Ctrl+Z).")
            .arg(fromUtf8(request.label).toHtmlEscaped()),
        &dialog);
    title->setWordWrap(true);
    title->setTextFormat(Qt::RichText);
    column->addWidget(title);

    auto* items = new QListWidget(&dialog);
    items->setObjectName(QStringLiteral("approvalItems"));
    for (const auto& item : request.items) {
        items->addItem(fromUtf8(item.text));
    }
    for (const auto& metric : request.metrics) {
        items->addItem(QStringLiteral("Δ ") + fromUtf8(metric));
    }
    for (const auto& warning : request.warnings) {
        items->addItem(QStringLiteral("⚠ ") + fromUtf8(warning));
    }
    items->setMaximumHeight(160);
    column->addWidget(items);

    if (!request.before_png.empty() || !request.after_png.empty()) {
        auto* row = new QHBoxLayout;
        auto* before = new QVBoxLayout;
        before->addWidget(new QLabel(QObject::tr("<b>Avant</b>"), &dialog));
        before->addWidget(renderLabel(request.before_png, QObject::tr("Avant"), &dialog));
        auto* after = new QVBoxLayout;
        after->addWidget(new QLabel(QObject::tr("<b>Après</b>"), &dialog));
        after->addWidget(renderLabel(request.after_png, QObject::tr("Après"), &dialog));
        row->addLayout(before);
        row->addLayout(after);
        column->addLayout(row);
    }

    auto* buttons = new QDialogButtonBox(&dialog);
    auto* apply = buttons->addButton(QObject::tr("Appliquer"), QDialogButtonBox::AcceptRole);
    auto* refuse = buttons->addButton(QObject::tr("Refuser"), QDialogButtonBox::RejectRole);
    apply->setObjectName(QStringLiteral("approvalApply"));
    refuse->setObjectName(QStringLiteral("approvalRefuse"));
    refuse->setDefault(true); // par défaut, on ne modifie rien
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    column->addWidget(buttons);
    return dialog.exec() == QDialog::Accepted;
}

QString CompanionPanel::connectionCommand() {
#ifdef Q_OS_WIN
    const QString server = QStringLiteral("openstitch-mcp.exe");
#else
    const QString server = QStringLiteral("openstitch-mcp");
#endif
    const QString path =
        QDir::toNativeSeparators(QDir(QCoreApplication::applicationDirPath()).filePath(server));
    return QStringLiteral("claude mcp add openstitch -- \"%1\" --live").arg(path);
}

CompanionPanel::CompanionPanel(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("companionPanel"));
    auto* column = new QVBoxLayout(this);
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("companionStatus"));
    status_->setWordWrap(true);
    column->addWidget(status_);

    toggle_ = new QPushButton(this);
    toggle_->setObjectName(QStringLiteral("companionToggle"));
    column->addWidget(toggle_);

    howTo_ = new QLabel(this);
    howTo_->setObjectName(QStringLiteral("companionHowTo"));
    howTo_->setWordWrap(true);
    howTo_->setTextFormat(Qt::RichText);
    column->addWidget(howTo_);

    copy_ = new QPushButton(tr("Copier la commande de connexion (Claude Code)"), this);
    copy_->setObjectName(QStringLiteral("companionCopy"));
    copy_->setToolTip(connectionCommand());
    column->addWidget(copy_);
    connect(copy_, &QPushButton::clicked, this,
            [] { QGuiApplication::clipboard()->setText(connectionCommand()); });

    column->addWidget(new QLabel(tr("<b>Journal de l'agent</b>"), this));
    log_ = new QListWidget(this);
    log_->setObjectName(QStringLiteral("companionLog"));
    column->addWidget(log_, 1);

    undo_ = new QPushButton(tr("Annuler la dernière modification"), this);
    undo_->setObjectName(QStringLiteral("companionUndo"));
    undo_->setEnabled(false);
    column->addWidget(undo_);

    connect(toggle_, &QPushButton::clicked, this, [this] { emit toggleRequested(!active_); });
    connect(undo_, &QPushButton::clicked, this, &CompanionPanel::undoRequested);
    setActive(false, {});
}

void CompanionPanel::setActive(bool active, const QString& endpoint) {
    active_ = active;
    if (active) {
        status_->setText(
            tr("<b>Pont actif.</b> Un agent peut lire le projet ouvert et proposer des "
               "modifications. Chacune demande votre accord."));
        toggle_->setText(tr("Désactiver le pont"));
        howTo_->setText(tr("Dans un terminal : <code>openstitch-mcp --live</code> (point d'accès "
                           "local, sans réseau)<br><small>%1</small>")
                            .arg(endpoint.toHtmlEscaped()));
    } else {
        status_->setText(tr("<b>Pont désactivé.</b> Aucun agent ne peut accéder au projet."));
        toggle_->setText(tr("Activer le pont pour un agent IA"));
        howTo_->setText(
            tr("Une fois activé, lancez <code>openstitch-mcp --live</code> depuis votre "
               "client MCP. L'agent travaille sur CE document, avec VOTRE historique "
               "d'annulation."));
    }
}

void CompanionPanel::setEvents(const std::vector<CompanionEvent>& events) {
    log_->clear();
    for (auto it = events.rbegin(); it != events.rend(); ++it) {
        QString state;
        switch (it->outcome) {
        case CompanionEvent::Outcome::Applied:
            state = tr("appliqué");
            break;
        case CompanionEvent::Outcome::Refused:
            state = tr("refusé");
            break;
        case CompanionEvent::Outcome::Pending:
            state = tr("en attente");
            break;
        }
        auto* item =
            new QListWidgetItem(QStringLiteral("[%1] %2").arg(state, fromUtf8(it->label)), log_);
        item->setToolTip(fromUtf8(it->detail));
    }
}

void CompanionPanel::setCanUndo(bool canUndo, const QString& undoName) {
    undo_->setEnabled(canUndo);
    undo_->setToolTip(canUndo ? tr("Annuler « %1 »").arg(undoName) : QString());
}

void CompanionPanel::setError(const QString& message) {
    status_->setText(tr("<b>Pont indisponible :</b> %1").arg(message.toHtmlEscaped()));
}

int CompanionPanel::eventCount() const {
    return log_->count();
}

QString CompanionPanel::statusText() const {
    return status_->text();
}

} // namespace openstitch::desktop_v2
