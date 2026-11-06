// SPDX-License-Identifier: Apache-2.0
#include "floating_action_bar.hpp"

#include <QHBoxLayout>
#include <QHash>
#include <QToolButton>

namespace openstitch::desktop_v2 {

namespace {

// Libellés courts pour la barre ; le libellé complet reste dans l'infobulle.
QString shortLabel(const std::string& id, const QString& full) {
    static const QHash<QString, QString> kShort = {
        {"edit.duplicate", QObject::tr("Dupliquer")},
        {"transform.rotate.cw", QObject::tr("Pivoter 90°")},
        {"transform.mirror.h", QObject::tr("Miroir")},
        {"order.front", QObject::tr("Premier plan")},
        {"edit.delete", QObject::tr("Supprimer")},
        {"text.edit", QObject::tr("Texte…")},
        {"text.remove", QObject::tr("Retirer le texte")},
        {"satin.rung.insert", QObject::tr("+ Barreau")},
        {"satin.rung.remove", QObject::tr("− Barreau")},
        {"satin.diagnose", QObject::tr("Diagnostic")},
        {"node.smooth", QObject::tr("Lisse")},
        {"node.corner", QObject::tr("Anguleux")},
        {"node.hole.remove", QObject::tr("Retirer le trou")},
    };
    return kShort.value(QString::fromStdString(id), full);
}

} // namespace

FloatingActionBar::FloatingActionBar(QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("floatingActionBar"));
    setAccessibleName(tr("Actions rapides de la sélection"));
    setFrameShape(QFrame::StyledPanel);
    setAutoFillBackground(true);
    row_ = new QHBoxLayout(this);
    row_->setContentsMargins(6, 4, 6, 4);
    row_->setSpacing(2);
    hide();
}

void FloatingActionBar::setEntries(const std::vector<CommandEntry>& entries) {
    while (auto* item = row_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (const auto& e : entries) {
        const QString id = QString::fromStdString(e.id);
        const QString label = QString::fromStdString(e.label);
        auto* b = new QToolButton(this);
        b->setObjectName(QStringLiteral("floatingAction_") + id);
        b->setText(shortLabel(e.id, label));
        b->setToolTip(e.shortcut.empty()
                          ? label
                          : label + QStringLiteral(" (") + QString::fromStdString(e.shortcut) +
                                QLatin1Char(')'));
        b->setAccessibleName(label);
        b->setAutoRaise(true);
        b->setEnabled(e.enabled);
        connect(b, &QToolButton::clicked, this, [this, id] { emit chosen(id); });
        row_->addWidget(b);
    }
    setVisible(!entries.empty());
    adjustSize();
    reposition();
}

int FloatingActionBar::buttonCount() const {
    return row_->count();
}

void FloatingActionBar::reposition() {
    if (parentWidget() == nullptr) {
        return;
    }
    move((parentWidget()->width() - width()) / 2, 8);
    raise();
}

} // namespace openstitch::desktop_v2
