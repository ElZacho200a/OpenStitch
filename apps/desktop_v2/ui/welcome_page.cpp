// SPDX-License-Identifier: Apache-2.0
#include "welcome_page.hpp"

#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace openstitch::desktop_v2 {

WelcomePage::WelcomePage(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("welcomePage"));
    auto* outer = new QVBoxLayout(this);
    outer->addStretch(1);
    auto* column = new QWidget(this);
    column->setMaximumWidth(560);
    auto* layout = new QVBoxLayout(column);
    layout->setSpacing(12);
    outer->addWidget(column, 0, Qt::AlignHCenter);
    outer->addStretch(2);

    auto* title = new QLabel(tr("<h1>OpenStitch Studio</h1>"), column);
    title->setTextFormat(Qt::RichText);
    layout->addWidget(title);
    auto* steps = new QLabel(tr("De l'image au fichier de broderie en quatre étapes :<br>"
                                "<b>1. Préparer</b> (image, couleurs, régions) · "
                                "<b>2. Numériser</b> (objets et points) · "
                                "<b>3. Contrôler</b> (diagnostic, simulation, ordre) · "
                                "<b>4. Produire</b> (machine, cadre, export). Vous pouvez "
                                "revenir en arrière à tout moment."),
                             column);
    steps->setWordWrap(true);
    steps->setTextFormat(Qt::RichText);
    layout->addWidget(steps);

    auto* open = new QPushButton(tr("Ouvrir une image ou un projet…"), column);
    open->setObjectName(QStringLiteral("welcomeOpen"));
    open->setDefault(true);
    open->setMinimumHeight(44);
    layout->addWidget(open);
    auto* blank = new QPushButton(tr("Nouveau projet vierge (dessiner des formes)"), column);
    blank->setObjectName(QStringLiteral("welcomeNew"));
    blank->setMinimumHeight(36);
    layout->addWidget(blank);

    recentBox_ = new QWidget(column);
    auto* recentLayout = new QVBoxLayout(recentBox_);
    recentLayout->setContentsMargins(0, 8, 0, 0);
    recentLayout->addWidget(new QLabel(tr("<b>Projets récents</b>"), recentBox_));
    recent_ = new QListWidget(recentBox_);
    recent_->setObjectName(QStringLiteral("welcomeRecent"));
    recent_->setMaximumHeight(150);
    recentLayout->addWidget(recent_);
    layout->addWidget(recentBox_);
    recentBox_->setVisible(false);

    auto* tip = new QLabel(tr("Astuce : Ctrl+K ouvre la palette de commandes, F1 l'aide."), column);
    tip->setEnabled(false);
    layout->addWidget(tip);

    connect(open, &QPushButton::clicked, this, &WelcomePage::openRequested);
    connect(blank, &QPushButton::clicked, this, &WelcomePage::newRequested);
    connect(recent_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        emit recentRequested(item->data(Qt::UserRole).toString());
    });
    connect(recent_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        emit recentRequested(item->data(Qt::UserRole).toString());
    });
}

void WelcomePage::setRecent(const QStringList& paths) {
    recent_->clear();
    for (const auto& path : paths) {
        auto* item = new QListWidgetItem(QFileInfo(path).fileName(), recent_);
        item->setToolTip(path);
        item->setData(Qt::UserRole, path);
    }
    recentBox_->setVisible(!paths.isEmpty());
}

int WelcomePage::recentCount() const {
    return recent_->count();
}

void WelcomePage::activateRecent(int row) {
    if (auto* item = recent_->item(row)) {
        emit recentRequested(item->data(Qt::UserRole).toString());
    }
}

} // namespace openstitch::desktop_v2
