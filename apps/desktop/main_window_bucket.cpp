// SPDX-License-Identifier: Apache-2.0
// Outil Pot de peinture : un clic dans un vide fermé entre plusieurs formes vectorielles crée une
// nouvelle forme qui remplit ce vide (satin, tatami... s'y appliquent ensuite comme à toute
// forme). Le calcul est dans libs/geometry (`fill_gap_at`) via `commands::make_fill_gap_command` ;
// ici seulement le câblage : clic -> commande annulable -> sélection de la forme créée.

#include <QAction>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QToolBar>

#include "app_theme.hpp"
#include "canvas_view.hpp"
#include "main_window.hpp"
#include "openstitch/commands/vector_ops.hpp"

namespace openstitch::desktop {

namespace {

// Fentes plus fines que 2 × cette valeur refermées pour que le remplissage ne fuie pas entre
// deux formes qui ne se touchent pas tout à fait (0,1 mm : invisible une fois brodé).
constexpr std::int32_t kSealUm = 100;

Vec2um sceneToModel(QPointF sceneMm) {
    return Vec2um{to_micrometers(Millimeters{sceneMm.x()}),
                  to_micrometers(Millimeters{-sceneMm.y()})};
}

QIcon bucketIcon() {
    QPixmap pm(40, 40);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(AppTheme::instance().tokens().textSecondary);
    pen.setWidthF(2.8);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    // Un seau penché qui se vide, et la goutte.
    p.save();
    p.translate(18, 22);
    p.rotate(-25);
    p.drawRoundedRect(QRectF(-10, -9, 20, 17), 3, 3);
    p.drawLine(QPointF(-10, -2), QPointF(10, -2));
    p.restore();
    p.drawEllipse(QPointF(33, 30), 2.5, 3.5);
    p.end();
    return QIcon(pm);
}

} // namespace

void MainWindow::buildBucketTool() {
    toolBucketAct_ = new QAction(bucketIcon(), tr("Pot de peinture"), this);
    toolBucketAct_->setObjectName(QStringLiteral("tool_bucket"));
    toolBucketAct_->setCheckable(true);
    toolBucketAct_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F));
    toolBucketAct_->setToolTip(tr(
        "Pot de peinture (Maj+F)\nCliquez dans un vide fermé entre plusieurs formes : une nouvelle "
        "forme remplit ce vide, prête pour un satin, un tatami..."));
    connect(toolBucketAct_, &QAction::triggered, this, [this] { setTool(Tool::Bucket); });
}

void MainWindow::attachBucketTool() {
    if (toolPalette_ != nullptr && toolBucketAct_ != nullptr &&
        !toolPalette_->actions().contains(toolBucketAct_)) {
        toolPalette_->addAction(toolBucketAct_);
    }
}

void MainWindow::syncBucketTool(Tool tool) {
    if (toolBucketAct_ != nullptr) {
        QSignalBlocker block(toolBucketAct_);
        toolBucketAct_->setChecked(tool == Tool::Bucket);
    }
    if (tool != Tool::Bucket) {
        return;
    }
    if (toolLabel_ != nullptr) {
        toolLabel_->setText(tr("Outil : %1").arg(tr("Pot de peinture")));
    }
    statusBar()->showMessage(
        tr("Pot de peinture : cliquez dans un vide fermé entre plusieurs formes pour le remplir."));
}

void MainWindow::bucketFillAt(QPointF posMm) {
    auto fill =
        commands::make_fill_gap_command(project_, sceneToModel(posMm), Micrometers{kSealUm});
    if (!fill.command) {
        statusBar()->showMessage(tr("Pot de peinture : %1").arg(QString::fromStdString(fill.error)),
                                 6000);
        return;
    }
    undoStack_.execute(std::move(fill.command), project_);
    setSelection({.region = std::nullopt,
                  .embroidery = std::nullopt,
                  .objects = {project_.vector_objects.back().id}});
    showVectorsAct_->setChecked(true);
    refreshImage();
    updateActions();
    statusBar()->showMessage(
        tr("Vide rempli — menu Broderie pour le convertir en satin, tatami..."), 6000);
}

} // namespace openstitch::desktop
