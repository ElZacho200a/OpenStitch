// SPDX-License-Identifier: Apache-2.0
// Surimpression « Sauts à vide » du panneau Ordre. Aucun calcul métier ici : les segments entre la
// fin d'un objet et le début du suivant viennent de libs/optimization (`jump_segments`), à partir
// des extrémités cousues de la séquence EFFECTIVE déjà en cache (`sequence_`). Les segments sont
// dessinés en pointillé dans la couche des éléments de points (retirés au redessin suivant, sans
// repeindre les points). Après « Optimiser l'ordre », le mode « comparer » garde les anciens sauts
// en gris et dessine les nouveaux en couleur, avec l'écart de trajet (« −X mm »). Rien ici ne
// modifie le document.
#include <QAction>
#include <QCheckBox>
#include <QDockWidget>
#include <QFont>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPainterPath>
#include <QPen>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <cmath>
#include <cstdlib>

#include "main_window.hpp"
#include "ui_memory.hpp"

namespace openstitch::desktop {

namespace {

const QString kJumpsKey = QStringLiteral("view/orderJumps");
const QString kJumpsCompareKey = QStringLiteral("view/orderJumpsCompare");
constexpr double kJumpZ = 24.0; // sous la carte de densité (25), au-dessus du rendu réaliste (20)

std::vector<optimization::SewnSpan> spansOf(const std::vector<stitch::ObjectEndpoints>& ends) {
    std::vector<optimization::SewnSpan> spans;
    spans.reserve(ends.size());
    for (const auto& e : ends) {
        spans.push_back({e.first, e.last});
    }
    return spans;
}

// Repère scène : x = mm, y = -mm (Y du document vers le haut).
QPointF scenePoint(Vec2um p) {
    return QPointF(to_millimeters(p.x).value, -to_millimeters(p.y).value);
}

QPainterPath pathOf(const std::vector<optimization::JumpSegment>& jumps) {
    QPainterPath path;
    for (const auto& j : jumps) {
        path.moveTo(scenePoint(j.from));
        path.lineTo(scenePoint(j.to));
    }
    return path;
}

} // namespace

void MainWindow::buildOrderJumpsFeature() {
    // --- Action « Affichage > Sauts à vide de l'ordre » (sans mnémonique : lettres prises) -------
    orderJumpsAct_ = new QAction(tr("Sauts à vide de l'ordre"), this);
    orderJumpsAct_->setObjectName(QStringLiteral("action_orderJumps"));
    orderJumpsAct_->setCheckable(true);
    orderJumpsAct_->setToolTip(tr("Dessine en pointillé les déplacements à vide entre la fin d'un "
                                  "objet et le début du suivant, dans l'ordre de couture. Après "
                                  "« Optimiser l'ordre », les anciens sauts restent en gris."));
    orderJumpsAct_->setChecked(ui_memory::boolValue(kJumpsKey, false));

    QMenu* viewMenu = nullptr;
    for (QMenu* menu : menuBar()->findChildren<QMenu*>()) {
        for (const QAction* action : menu->actions()) {
            if (action->menu() != nullptr && realisticAct_ != nullptr &&
                action->menu()->actions().contains(realisticAct_)) {
                viewMenu = menu;
            }
        }
    }
    if (viewMenu != nullptr) {
        viewMenu->addAction(orderJumpsAct_);
    }

    // --- Cases à cocher et résumé dans le panneau Ordre -----------------------------------------
    QWidget* parent = orderCostLabel_ != nullptr ? orderCostLabel_->parentWidget() : nullptr;
    auto* layout = parent != nullptr ? qobject_cast<QVBoxLayout*>(parent->layout()) : nullptr;
    if (layout != nullptr) {
        const int at = layout->indexOf(orderCostLabel_);
        orderJumpsCheck_ = new QCheckBox(tr("Sauts à vide sur le canevas"), parent);
        orderJumpsCheck_->setObjectName(QStringLiteral("check_orderJumps"));
        orderJumpsCheck_->setToolTip(orderJumpsAct_->toolTip());
        orderJumpsCheck_->setChecked(orderJumpsAct_->isChecked());
        orderJumpsCompareCheck_ =
            new QCheckBox(tr("Comparer avant / après l'optimisation"), parent);
        orderJumpsCompareCheck_->setObjectName(QStringLiteral("check_orderJumpsCompare"));
        orderJumpsCompareCheck_->setToolTip(
            tr("Après « Optimiser l'ordre », garde les anciens sauts en gris et affiche l'écart de "
               "trajet. La comparaison disparaît à la modification suivante."));
        orderJumpsCompareCheck_->setChecked(ui_memory::boolValue(kJumpsCompareKey, true));
        orderJumpsLabel_ = new QLabel(parent);
        orderJumpsLabel_->setObjectName(QStringLiteral("label_orderJumps"));
        orderJumpsLabel_->setWordWrap(true);
        orderJumpsLabel_->setVisible(false);
        layout->insertWidget(at, orderJumpsCheck_);
        layout->insertWidget(at + 1, orderJumpsCompareCheck_);
        layout->insertWidget(at + 2, orderJumpsLabel_);
        connect(orderJumpsCheck_, &QCheckBox::toggled, this,
                [this](bool on) { setOrderJumpsEnabled(on); });
        connect(orderJumpsCompareCheck_, &QCheckBox::toggled, this, [this](bool on) {
            ui_memory::setBoolValue(kJumpsCompareKey, on);
            renderStitches();
        });
    }
    connect(orderJumpsAct_, &QAction::toggled, this, [this](bool on) { setOrderJumpsEnabled(on); });
}

void MainWindow::setOrderJumpsEnabled(bool on) {
    ui_memory::setBoolValue(kJumpsKey, on);
    if (orderJumpsAct_ != nullptr && orderJumpsAct_->isChecked() != on) {
        const QSignalBlocker blocker(orderJumpsAct_);
        orderJumpsAct_->setChecked(on);
    }
    if (orderJumpsCheck_ != nullptr && orderJumpsCheck_->isChecked() != on) {
        const QSignalBlocker blocker(orderJumpsCheck_);
        orderJumpsCheck_->setChecked(on);
    }
    if (!on) {
        orderJumpsHasBefore_ = false; // la comparaison ne survit pas à l'extinction
    }
    renderStitches(); // pose ou retire la surimpression
}

void MainWindow::captureOrderJumpsBaseline(const std::vector<stitch::ObjectEndpoints>& endpoints) {
    orderJumpsBefore_ = optimization::jump_segments(spansOf(endpoints));
    orderJumpsHasBefore_ = !endpoints.empty();
    // Valable pour le refreshImage() qui suit l'optimisation, pas pour les suivants.
    orderJumpsBeforeRefresh_ = refreshCount_ + 1;
}

// Appelée par renderStitches() juste après le nettoyage de la couche points : les éléments
// rejoignent `stitchItems_` (donc retirés au redessin suivant) et ne captent aucun clic.
void MainWindow::refreshOrderJumpsOverlay() {
    orderJumps_.clear();
    orderJumpsDeltaText_.clear();
    if (orderJumpsAct_ == nullptr) {
        return;
    }
    if (orderJumpsHasBefore_ && orderJumpsBeforeRefresh_ != refreshCount_) {
        orderJumpsHasBefore_ = false; // un autre rafraîchissement est passé : base périmée
    }
    const bool wanted = orderJumpsAct_->isChecked() && sequence_.has_value() && !simulating();
    if (orderJumpsLabel_ != nullptr) {
        orderJumpsLabel_->setVisible(wanted);
    }
    if (!wanted) {
        return;
    }
    orderJumps_ = optimization::jump_segments(spansOf(stitch::object_endpoints(*sequence_)));
    const bool compare = orderJumpsHasBefore_ && orderJumpsCompareCheck_ != nullptr &&
                         orderJumpsCompareCheck_->isChecked();

    const auto addDashed = [this](const std::vector<optimization::JumpSegment>& jumps,
                                  const QColor& color, double width, Qt::PenStyle style, double z,
                                  const char* tag) {
        if (jumps.empty()) {
            return;
        }
        QPen pen(color, width, style);
        pen.setCosmetic(true); // épaisseur constante à l'écran quel que soit le zoom
        auto* item = scene_->addPath(pathOf(jumps), pen);
        item->setAcceptedMouseButtons(Qt::NoButton);
        item->setZValue(z);
        item->setData(0, QString::fromLatin1(tag));
        stitchItems_.append(item);
    };
    if (compare) {
        addDashed(orderJumpsBefore_, QColor(140, 140, 140), 1.5, Qt::DotLine, kJumpZ,
                  "orderJumpsOverlayOld");
    }
    // Bleu (palette accessible) : se distingue du gris même en vision des couleurs réduite.
    addDashed(orderJumps_, QColor(0, 114, 178), 2.0, Qt::DashLine, kJumpZ + 0.1,
              "orderJumpsOverlay");

    const std::int64_t newUm = optimization::jump_length_um(orderJumps_);
    QString summary =
        tr("%1 saut(s) à vide, %2 mm").arg(orderJumps_.size()).arg(newUm / 1000.0, 0, 'f', 1);
    if (compare) {
        const std::int64_t oldUm = optimization::jump_length_um(orderJumpsBefore_);
        const std::int64_t delta = newUm - oldUm;
        const QString sign =
            delta < 0 ? QStringLiteral("−") : (delta > 0 ? QStringLiteral("+") : QString());
        orderJumpsDeltaText_ = tr("%1%2 mm").arg(sign).arg(std::llabs(delta) / 1000.0, 0, 'f', 1);
        summary += tr(" (avant : %1 mm, écart %2)")
                       .arg(oldUm / 1000.0, 0, 'f', 1)
                       .arg(orderJumpsDeltaText_);
        // Petit libellé près du premier saut, à taille d'écran constante.
        const auto* anchor = !orderJumps_.empty()         ? &orderJumps_.front()
                             : !orderJumpsBefore_.empty() ? &orderJumpsBefore_.front()
                                                          : nullptr;
        if (anchor != nullptr) {
            auto* text = new QGraphicsSimpleTextItem(orderJumpsDeltaText_);
            QFont font = text->font();
            font.setBold(true);
            text->setFont(font);
            // Vert foncé si le trajet raccourcit, vermillon sinon (contraste suffisant sur blanc).
            text->setBrush(delta <= 0 ? QColor(0, 110, 70) : QColor(190, 60, 0));
            text->setFlag(QGraphicsItem::ItemIgnoresTransformations);
            text->setAcceptedMouseButtons(Qt::NoButton);
            text->setZValue(kJumpZ + 0.2);
            text->setData(0, QStringLiteral("orderJumpsDelta"));
            text->setPos((scenePoint(anchor->from) + scenePoint(anchor->to)) / 2.0);
            scene_->addItem(text);
            stitchItems_.append(text);
        }
    }
    if (orderJumpsLabel_ != nullptr) {
        orderJumpsLabel_->setText(summary);
    }
}

} // namespace openstitch::desktop
