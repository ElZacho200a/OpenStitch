// SPDX-License-Identifier: Apache-2.0
#include "order_list_widget.hpp"

#include <cmath>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace openstitch::desktop {

OrderListWidget::OrderListWidget(QWidget* parent) : QListWidget(parent) {
    setSelectionMode(QAbstractItemView::SingleSelection);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    // Le déplacement est exécuté par une commande : Qt ne doit pas déplacer les lignes lui-même.
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::IgnoreAction);
}

void OrderListWidget::dropEvent(QDropEvent* event) {
    if (event->source() != this) {
        event->ignore();
        return;
    }
    const int from = currentRow();
    const QModelIndex under = indexAt(event->position().toPoint());
    int to = under.isValid() ? under.row() : count() - 1;
    if (from < 0 || to < 0 || to >= count()) {
        event->ignore();
        return;
    }
    // Aucune modification de lignes ici (ni acceptation « move » qui supprimerait la source).
    event->setDropAction(Qt::IgnoreAction);
    event->ignore();
    if (from != to) {
        emit moveRequested(from, to);
    }
}

namespace {

QDoubleSpinBox* makeMmSpin(QWidget* parent, const QString& name, const QString& label) {
    auto* spin = new QDoubleSpinBox(parent);
    spin->setObjectName(name);
    spin->setRange(-1000.0, 1000.0);
    spin->setDecimals(1);
    spin->setSingleStep(1.0);
    spin->setKeyboardTracking(false); // un pas d'annulation par saisie validée
    spin->setSuffix(QStringLiteral(" mm"));
    spin->setAccessibleName(label);
    return spin;
}

} // namespace

OrderOptionsWidget::OrderOptionsWidget(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    const auto addPoint = [&](const QString& title, const QString& id, QCheckBox*& check,
                              QDoubleSpinBox*& sx, QDoubleSpinBox*& sy) {
        auto* row = new QHBoxLayout();
        check = new QCheckBox(title, this);
        check->setObjectName(QStringLiteral("check_%1").arg(id));
        sx = makeMmSpin(this, QStringLiteral("spin_%1X").arg(id), tr("%1 : X").arg(title));
        sy = makeMmSpin(this, QStringLiteral("spin_%1Y").arg(id), tr("%1 : Y").arg(title));
        sx->setEnabled(false);
        sy->setEnabled(false);
        row->addWidget(check);
        row->addWidget(new QLabel(QStringLiteral("X"), this));
        row->addWidget(sx);
        row->addWidget(new QLabel(QStringLiteral("Y"), this));
        row->addWidget(sy);
        layout->addLayout(row);
        connect(check, &QCheckBox::toggled, this, [this, sx, sy](bool on) {
            sx->setEnabled(on);
            sy->setEnabled(on);
            emitChanged();
        });
        connect(sx, &QDoubleSpinBox::valueChanged, this, [this] { emitChanged(); });
        connect(sy, &QDoubleSpinBox::valueChanged, this, [this] { emitChanged(); });
    };
    addPoint(tr("Départ"), QStringLiteral("designStart"), startCheck_, startX_, startY_);
    addPoint(tr("Arrivée"), QStringLiteral("designEnd"), endCheck_, endX_, endY_);
    startCheck_->setToolTip(tr("Point de départ du design (mm, Y vers le haut) : l'optimisation "
                               "de l'ordre commence au plus près de ce point."));
    endCheck_->setToolTip(tr("Point d'arrivée du design (mm, Y vers le haut) : le trajet vers ce "
                             "point compte dans l'optimisation de l'ordre."));

    improveCheck_ = new QCheckBox(tr("Affiner (2-opt / Or-opt)"), this);
    improveCheck_->setObjectName(QStringLiteral("check_orderImprove"));
    improveCheck_->setChecked(true);
    improveCheck_->setToolTip(tr("Après la stratégie, améliore localement l'ordre et le sens de "
                                 "couture (déterministe, borné en nombre d'essais)."));
    layout->addWidget(improveCheck_);
}

void OrderOptionsWidget::setEndpoints(const std::optional<Vec2um>& start,
                                      const std::optional<Vec2um>& end) {
    const QSignalBlocker b1(startCheck_);
    const QSignalBlocker b2(startX_);
    const QSignalBlocker b3(startY_);
    const QSignalBlocker b4(endCheck_);
    const QSignalBlocker b5(endX_);
    const QSignalBlocker b6(endY_);
    startCheck_->setChecked(start.has_value());
    startX_->setEnabled(start.has_value());
    startY_->setEnabled(start.has_value());
    if (start) {
        startX_->setValue(start->x.value / 1000.0);
        startY_->setValue(start->y.value / 1000.0);
    }
    endCheck_->setChecked(end.has_value());
    endX_->setEnabled(end.has_value());
    endY_->setEnabled(end.has_value());
    if (end) {
        endX_->setValue(end->x.value / 1000.0);
        endY_->setValue(end->y.value / 1000.0);
    }
}

namespace {

std::optional<Vec2um> pointOf(const QCheckBox* check, const QDoubleSpinBox* x,
                              const QDoubleSpinBox* y) {
    if (!check->isChecked()) {
        return std::nullopt;
    }
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(x->value() * 1000.0))},
                  Micrometers{static_cast<std::int32_t>(std::lround(y->value() * 1000.0))}};
}

} // namespace

std::optional<Vec2um> OrderOptionsWidget::start() const {
    return pointOf(startCheck_, startX_, startY_);
}

std::optional<Vec2um> OrderOptionsWidget::end() const {
    return pointOf(endCheck_, endX_, endY_);
}

bool OrderOptionsWidget::improveEnabled() const {
    return improveCheck_->isChecked();
}

void OrderOptionsWidget::emitChanged() {
    emit endpointsChanged();
}

} // namespace openstitch::desktop
