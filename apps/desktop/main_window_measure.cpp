// SPDX-License-Identifier: Apache-2.0
// Outil Règle (HP-VEC-010) : un clic-glisser sur le canevas affiche une règle temporaire
// (distance en mm, dx / dy, angle). Le calcul est dans libs/geometry (measure_segment) ; ici
// seulement le câblage et l'affichage. AUCUNE mutation du document : pas de commande, pas
// d'entrée dans l'historique d'annulation.

#include <QAction>
#include <QApplication>
#include <QBrush>
#include <QClipboard>
#include <QFont>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QIcon>
#include <QLabel>
#include <QLineF>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QToolBar>

#include <cmath>
#include <numbers>

#include "app_theme.hpp"
#include "canvas_view.hpp"
#include "main_window.hpp"
#include "openstitch/geometry/measure.hpp"

namespace openstitch::desktop {

namespace {

// Même convention que main_window.cpp (scène : mm, Y vers le bas ; modèle : µm, Y vers le haut).
Vec2um sceneToModel(QPointF sceneMm) {
    return Vec2um{to_micrometers(Millimeters{sceneMm.x()}),
                  to_micrometers(Millimeters{-sceneMm.y()})};
}

QString formatMm(double value) {
    return QLocale(QLocale::French).toString(value, 'f', 2);
}

QString describeMeasure(const geometry::SegmentMeasure& m) {
    double degrees = m.direction.radians * 180.0 / std::numbers::pi;
    if (degrees < 0.0) {
        degrees += 360.0; // 0 = est, sens antihoraire, [0 ; 360[
    }
    const QString angle = QLocale(QLocale::French).toString(degrees, 'f', 1);
    return QObject::tr("%1 mm  (Δx %2 mm ; Δy %3 mm ; angle %4°)")
        .arg(formatMm(m.length_mm().value), formatMm(m.dx_mm().value), formatMm(m.dy_mm().value),
             angle);
}

QIcon drawnIcon(bool ruler) {
    QPixmap pm(40, 40);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(AppTheme::instance().tokens().textSecondary);
    pen.setWidthF(2.8);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    if (ruler) {
        // Une règle graduée en biais.
        p.save();
        p.translate(20, 20);
        p.rotate(-35);
        p.drawRoundedRect(QRectF(-16, -5, 32, 10), 2, 2);
        for (int i = -10; i <= 10; i += 5) {
            p.drawLine(QPointF(i, -5), QPointF(i, i % 10 == 0 ? 1 : -1));
        }
        p.restore();
    } else {
        // Deux feuilles superposées : copier.
        p.drawRoundedRect(QRectF(7, 7, 18, 22), 2, 2);
        p.drawRoundedRect(QRectF(15, 12, 18, 22), 2, 2);
    }
    p.end();
    return QIcon(pm);
}

} // namespace

void MainWindow::buildMeasureTool() {
    toolMeasureAct_ = new QAction(drawnIcon(true), tr("Règle"), this);
    toolMeasureAct_->setObjectName(QStringLiteral("tool_measure"));
    toolMeasureAct_->setCheckable(true);
    toolMeasureAct_->setShortcut(QKeySequence(Qt::Key_Q));
    toolMeasureAct_->setToolTip(
        tr("Règle (Q)\nCliquez-glissez sur le canevas pour mesurer une distance (mm), "
           "dx / dy et l'angle. Purement visuel : le document n'est pas modifié."));
    connect(toolMeasureAct_, &QAction::triggered, this, [this] { setTool(Tool::Measure); });

    copyMeasureAct_ = new QAction(drawnIcon(false), tr("Copier la mesure"), this);
    copyMeasureAct_->setObjectName(QStringLiteral("action_measureCopy"));
    copyMeasureAct_->setToolTip(tr("Copie dans le presse-papiers la dernière mesure de la règle."));
    copyMeasureAct_->setEnabled(false);
    connect(copyMeasureAct_, &QAction::triggered, this, &MainWindow::copyLastMeasure);

    connect(view_, &CanvasView::measureDragMm, this,
            [this](QPointF a, QPointF b) { showMeasureRuler(a, b, false); });
    connect(view_, &CanvasView::measureFinishedMm, this,
            [this](QPointF a, QPointF b) { showMeasureRuler(a, b, true); });
    connect(view_, &CanvasView::measureCancelled, this, [this] {
        clearMeasureRuler();
        statusBar()->showMessage(tr("Mesure annulée."), 3000);
    });
}

void MainWindow::attachMeasureEyedropperTools() {
    if (toolPalette_ == nullptr) {
        return;
    }
    // Règle, copie de la mesure, puis Pipette ; hors du groupe exclusif de la palette (comme
    // l'outil Texte) : setTool() les synchronise.
    for (QAction* act : {toolMeasureAct_, copyMeasureAct_, toolEyedropperAct_}) {
        if (act != nullptr && !toolPalette_->actions().contains(act)) {
            toolPalette_->addAction(act);
        }
    }
}

void MainWindow::clearMeasureRuler() {
    if (measureRuler_ != nullptr) {
        if (scene_ != nullptr) {
            scene_->removeItem(measureRuler_);
        }
        delete measureRuler_; // supprime aussi les enfants (étiquette)
        measureRuler_ = nullptr;
    }
}

void MainWindow::showMeasureRuler(QPointF startMm, QPointF endMm, bool finished) {
    const auto m = geometry::measure_segment(sceneToModel(startMm), sceneToModel(endMm));
    const QString text = describeMeasure(m);

    // Reconstruit la règle (3 items) à chaque déplacement : coût négligeable.
    clearMeasureRuler();
    const QColor accent = AppTheme::instance().tokens().accent;
    auto* line = new QGraphicsLineItem(QLineF(startMm, endMm));
    QPen pen(accent);
    pen.setCosmetic(true);
    pen.setWidthF(1.6);
    pen.setStyle(Qt::DashLine);
    line->setPen(pen);
    line->setZValue(1e6);
    line->setAcceptedMouseButtons(Qt::NoButton);

    // Étiquette : fond + texte, taille d'écran constante (ignore le zoom), sous le milieu.
    auto* back = new QGraphicsRectItem(line);
    back->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    back->setAcceptedMouseButtons(Qt::NoButton);
    auto* label = new QGraphicsSimpleTextItem(text, back);
    label->setAcceptedMouseButtons(Qt::NoButton);
    QFont font = label->font();
    font.setPointSizeF(9.0);
    label->setFont(font);
    label->setBrush(QBrush(Qt::white));
    const QRectF tb = label->boundingRect();
    label->setPos(0.0, 6.0);
    back->setRect(tb.adjusted(-4, -2, 4, 2).translated(0.0, 6.0));
    back->setBrush(QBrush(QColor(20, 24, 32, 215)));
    back->setPen(Qt::NoPen);
    back->setPos(QLineF(startMm, endMm).pointAt(0.5));

    scene_->addItem(line);
    measureRuler_ = line;

    lastMeasureText_ = text;
    copyMeasureAct_->setEnabled(true);
    statusBar()->showMessage(
        finished ? tr("Mesure : %1 — « Copier la mesure » pour la copier.").arg(text)
                 : tr("Mesure : %1").arg(text),
        finished ? 20000 : 0);
}

void MainWindow::copyLastMeasure() {
    if (lastMeasureText_.isEmpty()) {
        return;
    }
    QApplication::clipboard()->setText(lastMeasureText_);
    statusBar()->showMessage(tr("Mesure copiée : %1").arg(lastMeasureText_), 6000);
}

void MainWindow::syncMeasureEyedropperTool(Tool tool) {
    const auto sync = [](QAction* act, bool on) {
        if (act == nullptr) {
            return;
        }
        QSignalBlocker block(act);
        act->setChecked(on);
    };
    sync(toolMeasureAct_, tool == Tool::Measure);
    sync(toolEyedropperAct_, tool == Tool::Eyedropper);
    if (view_ != nullptr) {
        view_->setMeasureMode(tool == Tool::Measure);
    }
    if (tool != Tool::Measure) {
        clearMeasureRuler();
    }
    if (tool != Tool::Eyedropper && eyedropperMenu_ != nullptr) {
        eyedropperMenu_->close();
    }
    if (tool == Tool::Measure) {
        if (toolLabel_ != nullptr) {
            toolLabel_->setText(tr("Outil : %1").arg(tr("Règle")));
        }
        statusBar()->showMessage(
            tr("Règle : cliquez-glissez pour mesurer (Échap : annuler). Le document n'est pas "
               "modifié."));
    } else if (tool == Tool::Eyedropper) {
        if (toolLabel_ != nullptr) {
            toolLabel_->setText(tr("Outil : %1").arg(tr("Pipette")));
        }
        statusBar()->showMessage(
            tr("Pipette : cliquez sur l'image pour échantillonner une couleur et choisir un fil "
               "proche."));
    }
}

} // namespace openstitch::desktop
