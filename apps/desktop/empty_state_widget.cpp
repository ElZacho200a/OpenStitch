// SPDX-License-Identifier: Apache-2.0
#include "empty_state_widget.hpp"

#include <QFileInfo>
#include <QLabel>
#include <QLayoutItem>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "app_theme.hpp"

namespace openstitch::desktop {

EmptyStateWidget::EmptyStateWidget(QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("emptyState"));
    // Fond, bord et rayon suivent le thème (reconstruits au changement de thème).
    const auto applyFrameStyle = [this] {
        const Tokens& t = AppTheme::instance().tokens();
        setStyleSheet(QStringLiteral("#emptyState { background:%1; border:1px solid %2; "
                                     "border-radius:%3px; }")
                          .arg(t.surfaceRaised.name(), t.border.name())
                          .arg(t.radiusLg));
    };
    applyFrameStyle();
    connect(&AppTheme::instance(), &AppTheme::changed, this, applyFrameStyle);

    // Contenu dans une zone défilante transparente (HP-UX-025) : dans un canevas bas, toutes les
    // actions restent atteignables au lieu d'être tronquées par le cadre.
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("emptyStateScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->viewport()->setAutoFillBackground(false);
    auto* content = new QWidget(scroll);
    content->setObjectName(QStringLiteral("emptyStateContent"));
    content->setAutoFillBackground(false);
    scroll->setWidget(content);
    outer->addWidget(scroll);

    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(32, 28, 32, 28);
    layout->setSpacing(12);

    auto* title = new QLabel(tr("Aucun document ouvert"), this);
    QFont tf = title->font();
    tf.setPointSizeF(tf.pointSizeF() + 3.0);
    tf.setBold(true);
    title->setFont(tf);
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    auto* hint =
        new QLabel(tr("Importez une image ou un fichier SVG pour commencer un nouveau motif\n"
                      "(un SVG évite l'étape de segmentation), ou ouvrez un projet existant."),
                   this);
    hint->setAlignment(Qt::AlignCenter);
    markSecondaryText(hint);
    layout->addWidget(hint);

    auto* buttons = new QVBoxLayout();
    buttons->setSpacing(8);
    auto* openImg = new QPushButton(tr("Ouvrir une image ou un SVG…"), this);
    auto* openPrj = new QPushButton(tr("Ouvrir un projet…"), this);
    auto* importDst = new QPushButton(tr("Importer une broderie machine…"), this);
    openImg->setProperty("primary", true); // l'action la plus probable, mise en avant
    for (QPushButton* b : {openImg, openPrj, importDst}) {
        b->setMinimumWidth(260);
        buttons->addWidget(b, 0, Qt::AlignCenter);
    }
    layout->addLayout(buttons);

    // Liste des récents (HP-FILE-003) : vide au départ, remplie par le
    // premier appel à setRecentFiles (MainWindow::refreshRecentFilesUi).
    recentLayout_ = new QVBoxLayout();
    recentLayout_->setSpacing(6);
    layout->addLayout(recentLayout_);

    connect(openImg, &QPushButton::clicked, this, &EmptyStateWidget::openImageRequested);
    connect(openPrj, &QPushButton::clicked, this, &EmptyStateWidget::openProjectRequested);
    connect(importDst, &QPushButton::clicked, this, &EmptyStateWidget::importDstRequested);
}

void EmptyStateWidget::setRecentFiles(const QStringList& paths) {
    while (QLayoutItem* item = recentLayout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    // Cinq récents au plus ici (le menu Fichier en garde dix) : l'écran d'accueil doit tenir dans
    // un canevas bas sans être tronqué.
    constexpr qsizetype kMaxShownRecents = 5;
    for (const QString& path : paths.mid(0, kMaxShownRecents)) {
        auto* button = new QPushButton(QFileInfo(path).fileName(), this);
        button->setToolTip(path);
        button->setMinimumWidth(220);
        recentLayout_->addWidget(button, 0, Qt::AlignCenter);
        connect(button, &QPushButton::clicked, this,
                [this, path] { emit openRecentRequested(path); });
    }
}

} // namespace openstitch::desktop
