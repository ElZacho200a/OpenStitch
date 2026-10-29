// SPDX-License-Identifier: Apache-2.0
#include "companion_dock.hpp"

#include <QCoreApplication>
#include <QEvent>
#include <QEventLoop>
#include <QGroupBox>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace openstitch::desktop {

namespace {

constexpr int kMaxMessageChars = 20000; // un message plus long est tronqué à l'affichage
constexpr int kMaxToolChars = 2000;     // idem pour un résultat d'outil volumineux
constexpr int kMaxEntries = 500;        // l'historique affiché est borné

QString clipped(const QString& text, int limit) {
    if (text.size() <= limit) {
        return text;
    }
    return text.left(limit) + QObject::tr("… (tronqué, %1 caractères au total)").arg(text.size());
}

QString html(const QString& text, int limit) {
    return clipped(text, limit).toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
}

QPixmap scaled(const QByteArray& png) {
    QPixmap pixmap;
    pixmap.loadFromData(png, "PNG");
    return pixmap.isNull()
               ? pixmap
               : pixmap.scaled(QSize(200, 220), Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

} // namespace

CompanionDock::CompanionDock(QWidget* parent) : QDockWidget(tr("Compagnon IA"), parent) {
    setObjectName(QStringLiteral("dock_companion"));
    setAccessibleName(tr("Compagnon IA"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    buildUi();
    refreshHeader();
}

void CompanionDock::buildUi() {
    auto* root = new QWidget(this);
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(6, 6, 6, 6);

    header_ = new QLabel(root);
    header_->setObjectName(QStringLiteral("label_companionHeader"));
    header_->setWordWrap(true);
    header_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(header_);

    errorBanner_ = new QLabel(root);
    errorBanner_->setObjectName(QStringLiteral("label_companionError"));
    errorBanner_->setWordWrap(true);
    errorBanner_->setAccessibleName(tr("Erreur du compagnon"));
    errorBanner_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    errorBanner_->setVisible(false);
    layout->addWidget(errorBanner_);

    stack_ = new QStackedWidget(root);
    // Page 0 : écran d'accueil (aucun fournisseur connecté).
    auto* welcomePage = new QWidget(stack_);
    auto* welcomeLayout = new QVBoxLayout(welcomePage);
    welcome_ = new QLabel(welcomePage);
    welcome_->setObjectName(QStringLiteral("label_companionWelcome"));
    welcome_->setWordWrap(true);
    welcome_->setTextFormat(Qt::PlainText);
    welcome_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    enableBridge_ = new QPushButton(welcomePage);
    enableBridge_->setObjectName(QStringLiteral("button_companionEnableBridge"));
    connect(enableBridge_, &QPushButton::clicked, this, &CompanionDock::enableLiveBridgeRequested);
    chatClaudeCode_ = new QPushButton(welcomePage);
    chatClaudeCode_->setObjectName(QStringLiteral("button_companionChatClaudeCode"));
    connect(chatClaudeCode_, &QPushButton::clicked, this,
            &CompanionDock::useClaudeCodeInDockRequested);
    openClaudeCode_ = new QPushButton(welcomePage);
    openClaudeCode_->setObjectName(QStringLiteral("button_companionOpenClaudeCode"));
    connect(openClaudeCode_, &QPushButton::clicked, this, &CompanionDock::openClaudeCodeRequested);
    configure_ = new QPushButton(welcomePage);
    configure_->setObjectName(QStringLiteral("button_companionConfigure"));
    connect(configure_, &QPushButton::clicked, this, &CompanionDock::configureProviderRequested);
    welcomeLayout->addWidget(welcome_);
    welcomeLayout->addWidget(chatClaudeCode_);
    welcomeLayout->addWidget(openClaudeCode_);
    welcomeLayout->addWidget(configure_);
    welcomeLayout->addWidget(enableBridge_);
    welcomeLayout->addStretch(1);
    stack_->addWidget(welcomePage);

    // Page 1 : conversation.
    auto* chatPage = new QWidget(stack_);
    auto* chat = new QVBoxLayout(chatPage);
    chat->setContentsMargins(0, 0, 0, 0);
    transcript_ = new QTextBrowser(chatPage);
    transcript_->setObjectName(QStringLiteral("browser_companionTranscript"));
    transcript_->setAccessibleName(tr("Conversation avec le compagnon"));
    transcript_->setOpenLinks(false);
    transcript_->setMinimumHeight(80);
    transcript_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    chat->addWidget(transcript_, 1);
    input_ = new QPlainTextEdit(chatPage);
    input_->setObjectName(QStringLiteral("edit_companionInput"));
    input_->setAccessibleName(tr("Message au compagnon"));
    input_->setTabChangesFocus(true);
    input_->setMinimumHeight(48);
    input_->setMaximumHeight(120);
    input_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    input_->installEventFilter(this);
    chat->addWidget(input_);
    auto* buttons = new QHBoxLayout;
    send_ = new QPushButton(chatPage);
    send_->setObjectName(QStringLiteral("button_companionSend"));
    cancel_ = new QPushButton(chatPage);
    cancel_->setObjectName(QStringLiteral("button_companionCancel"));
    cancel_->setEnabled(false);
    buttons->addWidget(send_);
    buttons->addWidget(cancel_);
    chat->addLayout(buttons);
    progress_ = new QProgressBar(chatPage);
    progress_->setObjectName(QStringLiteral("progress_companion"));
    progress_->setRange(0, 0);
    progress_->setTextVisible(false);
    progress_->setMaximumHeight(6);
    progress_->setVisible(false);
    chat->addWidget(progress_);
    stack_->addWidget(chatPage);
    layout->addWidget(stack_, 1);

    // Changement proposé : visible seulement quand une décision est attendue.
    proposalBox_ = new QGroupBox(root);
    proposalBox_->setObjectName(QStringLiteral("group_companionProposal"));
    proposalBox_->setVisible(false);
    auto* proposal = new QVBoxLayout(proposalBox_);
    proposalSummary_ = new QLabel(proposalBox_);
    proposalSummary_->setWordWrap(true);
    proposalSummary_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    proposal->addWidget(proposalSummary_);
    // Boutons en HAUT du panneau : toujours visibles, même avec de grands aperçus.
    auto* decision = new QHBoxLayout;
    confirm_ = new QPushButton(proposalBox_);
    confirm_->setObjectName(QStringLiteral("button_companionConfirm"));
    reject_ = new QPushButton(proposalBox_);
    reject_->setObjectName(QStringLiteral("button_companionReject"));
    decision->addWidget(confirm_);
    decision->addWidget(reject_);
    proposal->addLayout(decision);
    proposalItems_ = new QListWidget(proposalBox_);
    proposalItems_->setObjectName(QStringLiteral("list_companionProposalItems"));
    proposalItems_->setAccessibleName(tr("Objets concernés par le changement proposé"));
    proposalItems_->setMaximumHeight(140);
    proposalItems_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    connect(proposalItems_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        const quint64 id = item->data(Qt::UserRole).toULongLong();
        if (id != 0) {
            emit focusObjectRequested(id);
        }
    });
    proposal->addWidget(proposalItems_);
    auto* images = new QHBoxLayout;
    beforeImage_ = new QLabel(proposalBox_);
    beforeImage_->setObjectName(QStringLiteral("label_companionBefore"));
    beforeImage_->setAccessibleName(tr("Aperçu avant la modification"));
    beforeImage_->setToolTip(tr("Avant"));
    afterImage_ = new QLabel(proposalBox_);
    afterImage_->setObjectName(QStringLiteral("label_companionAfter"));
    afterImage_->setAccessibleName(tr("Aperçu après la modification"));
    afterImage_->setToolTip(tr("Après"));
    for (QLabel* image : {beforeImage_, afterImage_}) {
        image->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
        image->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Maximum);
    }
    images->addWidget(beforeImage_);
    images->addWidget(afterImage_);
    proposal->addLayout(images);
    proposalWarnings_ = new QLabel(proposalBox_);
    proposalWarnings_->setWordWrap(true);
    proposalWarnings_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    proposal->addWidget(proposalWarnings_);
    layout->addWidget(proposalBox_);
    connect(confirm_, &QPushButton::clicked, this, [this] { resolveProposal(true); });
    connect(reject_, &QPushButton::clicked, this, [this] { resolveProposal(false); });

    undoLast_ = new QPushButton(root);
    undoLast_->setObjectName(QStringLiteral("button_companionUndo"));
    undoLast_->setVisible(false);
    connect(undoLast_, &QPushButton::clicked, this, &CompanionDock::undoLastRequested);
    layout->addWidget(undoLast_);

    connect(send_, &QPushButton::clicked, this, &CompanionDock::submit);
    connect(cancel_, &QPushButton::clicked, this, [this] {
        if (backend_ != nullptr) {
            backend_->cancel();
        }
    });
    connect(transcript_, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        if (url.scheme() == QLatin1String("obj")) {
            emit focusObjectRequested(url.path().toULongLong());
        }
    });

    // Défilement vertical : un panneau trop haut (liste, aperçus) ne cache jamais les boutons.
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("scroll_companion"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(root);
    setWidget(scroll);
    retranslate();
}

void CompanionDock::retranslate() {
    setWindowTitle(tr("Compagnon IA"));
    setAccessibleName(tr("Compagnon IA"));
    welcome_->setText(
        tr("Aucun fournisseur d'IA n'est connecté. OpenStitch fonctionne normalement sans lui.\n\n"
           "Pour qu'un agent local (par exemple Claude Code avec « openstitch-mcp --live ») lise "
           "le projet ouvert et propose des modifications, activez le pont IA local. Aucun port "
           "réseau n'est ouvert ; chaque modification demande votre approbation et s'annule en "
           "une seule étape (Ctrl+Z)."));
    enableBridge_->setText(tr("Activer le pont IA local"));
    chatClaudeCode_->setText(tr("Discuter avec Claude Code ici (via MCP)…"));
    chatClaudeCode_->setToolTip(
        tr("Utilise la CLI Claude Code installee, sans cle API OpenStitch. "
           "Les outils MCP modifient le projet ouvert avec votre approbation."));
    openClaudeCode_->setText(tr("Ouvrir Claude Code dans un terminal…"));
    openClaudeCode_->setToolTip(
        tr("Lance votre Claude Code dans un terminal relié au projet ouvert. Aucune clé API "
           "OpenStitch nécessaire ; modifications approuvées dans le dock."));
    configure_->setText(tr("Configurer Claude…"));
    send_->setText(tr("Envoyer"));
    send_->setToolTip(tr("Envoyer le message (Entrée) — Maj+Entrée : saut de ligne"));
    cancel_->setText(tr("Interrompre"));
    input_->setPlaceholderText(tr("Demandez quelque chose sur le projet ouvert…"));
    proposalBox_->setTitle(tr("Changement proposé"));
    confirm_->setText(tr("Appliquer"));
    reject_->setText(tr("Rejeter"));
    undoLast_->setText(tr("Annuler : %1").arg(undoName_));
    refreshHeader();
    rebuildTranscript();
}

void CompanionDock::changeEvent(QEvent* event) {
    QDockWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    } else if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) {
        rebuildTranscript(); // couleurs du fil dérivées de la palette (thème clair / sombre)
    }
}

void CompanionDock::setBackend(CompanionBackend* backend) {
    if (backend_ == backend) {
        return;
    }
    if (backend_ != nullptr) {
        backend_->disconnect(this);
    }
    backend_ = backend;
    if (backend_ != nullptr) {
        connectBackend();
    }
    setBusy(false);
    refreshHeader();
}

void CompanionDock::connectBackend() {
    connect(backend_, &CompanionBackend::assistantText, this,
            [this](const QString& text) { appendAssistantMessage(text); });
    connect(backend_, &CompanionBackend::toolCalled, this,
            [this](const CompanionToolCall& call) { appendToolCall(call); });
    connect(backend_, &CompanionBackend::proposalReady, this,
            [this](const CompanionProposal& proposal) {
                QPointer<CompanionBackend> backend(backend_);
                showProposal(proposal, [backend](bool approved) {
                    if (backend != nullptr) {
                        backend->decide(approved);
                    }
                });
            });
    connect(backend_, &CompanionBackend::failed, this,
            [this](const QString& message) { showError(message); });
    connect(backend_, &CompanionBackend::finished, this, [this] { setBusy(false); });
    connect(backend_, &CompanionBackend::usage, this,
            [this](quint64 in, quint64 out) { setUsage(in, out); });
    connect(backend_, &QObject::destroyed, this, [this] {
        backend_ = nullptr;
        setBusy(false);
        refreshHeader();
    });
}

void CompanionDock::setContext(const QString& documentName, int selectionCount, bool hasDocument) {
    documentName_ = documentName;
    selectionCount_ = selectionCount;
    hasDocument_ = hasDocument;
    refreshHeader();
}

void CompanionDock::setLiveBridgeActive(bool active) {
    liveBridgeActive_ = active;
    refreshHeader();
}

void CompanionDock::setUndoAvailable(bool available, const QString& undoName) {
    undoName_ = undoName;
    // Seuls les pas d'annulation posés par l'IA ont un bouton dédié (« IA : … ») ; l'historique
    // complet reste dans Édition ▸ Annuler.
    undoLast_->setText(tr("Annuler : %1").arg(undoName));
    undoLast_->setVisible(available && undoName.startsWith(QStringLiteral("IA")));
}

void CompanionDock::refreshHeader() {
    const bool connected = backend_ != nullptr && backend_->available();
    stack_->setCurrentIndex(connected ? 1 : 0);
    enableBridge_->setVisible(!liveBridgeActive_ && !fullyDisabled_);
    openClaudeCode_->setEnabled(!fullyDisabled_);
    chatClaudeCode_->setEnabled(!fullyDisabled_);
    configure_->setVisible(true);
    if (fullyDisabled_) {
        welcome_->setText(tr("Le compagnon IA est désactivé (pont local et fournisseur). "
                             "OpenStitch fonctionne normalement. Vous pouvez le réactiver dans "
                             "Aide ▸ Compagnon IA…"));
    }
    QString text;
    if (!hasDocument_) {
        text = tr("Aucun projet actif.");
    } else {
        text = tr("Projet actif : %1 · %n objet(s) sélectionné(s)", "", selectionCount_)
                   .arg(documentName_);
    }
    text += QLatin1Char('\n') +
            (liveBridgeActive_ ? tr("Pont IA local : actif.") : tr("Pont IA local : inactif."));
    if (connected) {
        text += QLatin1Char('\n') + tr("Fournisseur : %1").arg(backend_->providerName());
        if (usageIn_ + usageOut_ > 0) {
            text += QLatin1Char('\n') +
                    tr("Utilisation : %1 jetons en entrée, %2 en sortie (coût : voir votre "
                       "compte chez le fournisseur)")
                        .arg(usageIn_)
                        .arg(usageOut_);
        }
    }
    header_->setText(text);
    send_->setEnabled(connected && !busy_ && hasDocument_);
    input_->setEnabled(connected);
}

void CompanionDock::setFullyDisabled(bool disabled) {
    fullyDisabled_ = disabled;
    if (!disabled) {
        retranslate(); // restaure le texte d'accueil standard
    }
    refreshHeader();
}

void CompanionDock::setUsage(quint64 inputTokens, quint64 outputTokens) {
    usageIn_ = inputTokens;
    usageOut_ = outputTokens;
    refreshHeader();
}

void CompanionDock::documentReplaced() {
    if (pendingDecision_) {
        resolveProposal(false);
    }
    if (!entries_.empty()) {
        Entry note;
        note.kind = Entry::Kind::Note;
        note.text = tr("Le projet actif a changé : la conversation précédente concerne l'ancien "
                       "document.");
        addEntry(std::move(note));
    }
    undoLast_->setVisible(false);
}

void CompanionDock::showProposal(const CompanionProposal& proposal,
                                 std::function<void(bool)> decision) {
    if (pendingDecision_) {
        resolveProposal(false); // une seule décision en attente à la fois
    }
    proposalBox_->setTitle(proposal.title.isEmpty() ? tr("Changement proposé") : proposal.title);
    proposalSummary_->setText(proposal.summary);
    proposalItems_->clear();
    for (const auto& item : proposal.items) {
        auto* row = new QListWidgetItem(item.text, proposalItems_);
        row->setData(Qt::UserRole, item.objectId);
        if (item.objectId != 0) {
            row->setToolTip(tr("Entrée : afficher l'objet sur le canevas"));
        }
    }
    proposalItems_->setVisible(!proposal.items.isEmpty());
    proposalWarnings_->setText(proposal.warnings.isEmpty()
                                   ? QString()
                                   : tr("Avertissements :\n• ") +
                                         proposal.warnings.join(QStringLiteral("\n• ")));
    proposalWarnings_->setVisible(!proposal.warnings.isEmpty());
    beforeImage_->setPixmap(scaled(proposal.beforePng));
    afterImage_->setPixmap(scaled(proposal.afterPng));
    beforeImage_->setVisible(!proposal.beforePng.isEmpty());
    afterImage_->setVisible(!proposal.afterPng.isEmpty());
    confirm_->setText(proposal.confirmText.isEmpty() ? tr("Appliquer") : proposal.confirmText);
    reject_->setText(proposal.rejectText.isEmpty() ? tr("Rejeter") : proposal.rejectText);
    pendingDecision_ = std::move(decision);
    proposalBox_->setVisible(true);
    confirm_->setFocus();
}

bool CompanionDock::askApproval(const CompanionProposal& proposal) {
    QEventLoop loop;
    bool result = false;
    QPointer<CompanionDock> self(this);
    showProposal(proposal, [&](bool approved) {
        result = approved;
        loop.quit();
    });
    if (pendingDecision_) {
        loop.exec();
    }
    return self != nullptr ? result : false;
}

int CompanionDock::askChoice(const QString& question, const QStringList& labels,
                             const QList<quint64>& focusIds) {
    CompanionProposal proposal;
    proposal.title = tr("Question de l'agent");
    proposal.summary = question;
    for (int i = 0; i < labels.size(); ++i) {
        proposal.items.push_back({i < focusIds.size() ? focusIds.at(i) : 0, labels.at(i)});
    }
    proposal.confirmText = tr("Choisir");
    proposal.rejectText = tr("Aucun");
    int chosen = -1;
    QEventLoop loop;
    QPointer<CompanionDock> self(this);
    showProposal(proposal, [&](bool approved) {
        if (approved && self != nullptr) {
            chosen = proposalItems_->currentRow();
        }
        loop.quit();
    });
    if (pendingDecision_) {
        proposalItems_->setCurrentRow(0);
        loop.exec();
    }
    return self != nullptr ? chosen : -1;
}

void CompanionDock::resolveProposal(bool approved) {
    auto decision = std::move(pendingDecision_);
    pendingDecision_ = nullptr;
    proposalBox_->setVisible(false);
    if (decision) {
        decision(approved);
    }
}

void CompanionDock::appendUserMessage(const QString& text) {
    Entry e;
    e.kind = Entry::Kind::User;
    e.text = text;
    addEntry(std::move(e));
}

void CompanionDock::appendAssistantMessage(const QString& text) {
    Entry e;
    e.kind = Entry::Kind::Assistant;
    e.text = text;
    addEntry(std::move(e));
}

void CompanionDock::appendToolCall(const CompanionToolCall& call) {
    Entry e;
    e.kind = Entry::Kind::Tool;
    e.tool = call;
    addEntry(std::move(e));
}

void CompanionDock::showError(const QString& message) {
    errorBanner_->setText(clipped(message, kMaxToolChars));
    errorBanner_->setVisible(true);
    Entry e;
    e.kind = Entry::Kind::Error;
    e.text = message;
    addEntry(std::move(e));
}

void CompanionDock::clearConversation() {
    entries_.clear();
    errorBanner_->setVisible(false);
    rebuildTranscript();
}

void CompanionDock::addEntry(Entry entry) {
    entries_.push_back(std::move(entry));
    if (entries_.size() > kMaxEntries) {
        entries_.erase(entries_.begin(), entries_.begin() + static_cast<std::ptrdiff_t>(
                                                                entries_.size() - kMaxEntries));
    }
    rebuildTranscript();
}

void CompanionDock::rebuildTranscript() {
    if (transcript_ == nullptr) {
        return;
    }
    const QPalette pal = transcript_->palette();
    const bool dark = pal.color(QPalette::Base).lightness() < 128;
    const QString accent = pal.color(QPalette::Highlight).name();
    const QString muted = pal.color(QPalette::Mid).name();
    const QString danger = dark ? QStringLiteral("#ef9a9a") : QStringLiteral("#b3261e");
    QString body;
    for (const Entry& e : entries_) {
        switch (e.kind) {
        case Entry::Kind::User:
            body += QStringLiteral("<p style=\"border-left:3px solid %1; padding-left:6px\"><b>%2"
                                   "</b><br>%3</p>")
                        .arg(accent, tr("Vous").toHtmlEscaped(), html(e.text, kMaxMessageChars));
            break;
        case Entry::Kind::Assistant:
            body += QStringLiteral("<p><b>%1</b><br>%2</p>")
                        .arg(tr("Compagnon").toHtmlEscaped(), html(e.text, kMaxMessageChars));
            break;
        case Entry::Kind::Tool: {
            const QString colour = e.tool.isError ? danger : muted;
            QString card =
                QStringLiteral("<p style=\"border:1px solid %1; padding:4px\"><b>%2</b> "
                               "— %3")
                    .arg(colour, e.tool.tool.toHtmlEscaped(), html(e.tool.summary, kMaxToolChars));
            if (!e.tool.refusal.isEmpty()) {
                card += QStringLiteral("<br><span style=\"color:%1\"><b>%2</b> %3</span>")
                            .arg(danger, tr("Refusé :").toHtmlEscaped(),
                                 html(e.tool.refusal, kMaxToolChars));
            }
            body += card + QStringLiteral("</p>");
            break;
        }
        case Entry::Kind::Error:
            body += QStringLiteral("<p style=\"color:%1\"><b>%2</b> %3</p>")
                        .arg(danger, tr("Erreur :").toHtmlEscaped(), html(e.text, kMaxToolChars));
            break;
        case Entry::Kind::Note:
            body += QStringLiteral("<p style=\"color:%1\"><i>%2</i></p>")
                        .arg(muted, html(e.text, kMaxToolChars));
            break;
        }
    }
    transcript_->setHtml(body);
    transcript_->moveCursor(QTextCursor::End);
}

void CompanionDock::submit() {
    if (backend_ == nullptr || !backend_->available() || busy_) {
        return;
    }
    const QString text = input_->toPlainText().trimmed();
    if (text.isEmpty()) {
        return;
    }
    input_->clear();
    errorBanner_->setVisible(false);
    appendUserMessage(text);
    setBusy(true);
    backend_->send(text);
}

bool CompanionDock::sendProgrammatic(const QString& displayText, const QString& prompt) {
    if (backend_ == nullptr || !backend_->available() || busy_ || prompt.trimmed().isEmpty()) {
        return false;
    }
    errorBanner_->setVisible(false);
    appendUserMessage(displayText);
    setBusy(true);
    backend_->send(prompt);
    return true;
}

void CompanionDock::setBusy(bool busy) {
    busy_ = busy;
    progress_->setVisible(busy);
    cancel_->setEnabled(busy);
    send_->setEnabled(!busy && backend_ != nullptr && backend_->available() && hasDocument_);
}

bool CompanionDock::eventFilter(QObject* watched, QEvent* event) {
    if (watched == input_ && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        const bool enter = key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter;
        if (enter && !(key->modifiers() & Qt::ShiftModifier)) {
            submit(); // Entrée envoie ; Maj+Entrée insère un saut de ligne (comportement natif)
            return true;
        }
    }
    return QDockWidget::eventFilter(watched, event);
}

} // namespace openstitch::desktop
