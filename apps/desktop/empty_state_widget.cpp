// SPDX-License-Identifier: Apache-2.0
#include "empty_state_widget.hpp"

#include <QFileInfo>
#include <QLabel>
#include <QLayoutItem>
#include <QPushButton>
#include <QVBoxLayout>

#include "app_theme.hpp"

namespace openstitch::desktop {

EmptyStateWidget::EmptyStateWidget(QWidget* parent) : QFrame(parent) {
    const Tokens& t = AppTheme::instance().tokens();
    setStyleSheet(
        QStringLiteral("QFrame { background:%1; border:1px solid %2; border-radius:%3px; }")
            .arg(t.surface.name(), t.border.name())
            .arg(t.radiusMd));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(10);

    auto* title = new QLabel(tr("Aucun document ouvert"), this);
    QFont tf = title->font();
    tf.setPointSizeF(tf.pointSizeF() + 2.0);
    tf.setBold(true);
    title->setFont(tf);
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    auto* buttons = new QVBoxLayout();
    buttons->setSpacing(6);
    auto* openImg = new QPushButton(tr("Ouvrir une image ou un SVG…"), this);
    auto* openPrj = new QPushButton(tr("Ouvrir un projet…"), this);
    auto* importDst = new QPushButton(tr("Importer un DST…"), this);
    for (QPushButton* b : {openImg, openPrj, importDst}) {
        b->setMinimumWidth(220);
        buttons->addWidget(b, 0, Qt::AlignCenter);
    }
    layout->addLayout(buttons);

    auto* hint =
        new QLabel(tr("Importez une image ou un fichier SVG pour commencer un nouveau motif\n"
                      "(un SVG évite l'étape de segmentation), ou ouvrez un projet existant."),
                   this);
    hint->setAlignment(Qt::AlignCenter);
    hint->setEnabled(false);
    layout->addWidget(hint);

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
    for (const QString& path : paths) {
        auto* button = new QPushButton(QFileInfo(path).fileName(), this);
        button->setToolTip(path);
        button->setMinimumWidth(220);
        recentLayout_->addWidget(button, 0, Qt::AlignCenter);
        connect(button, &QPushButton::clicked, this,
                [this, path] { emit openRecentRequested(path); });
    }
}

} // namespace openstitch::desktop
