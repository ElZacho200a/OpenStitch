// SPDX-License-Identifier: Apache-2.0
// Zones de détail (menu Segmentation) : un pinceau peint un masque « détail / aplat » à la taille
// de l'image de travail. Dans les zones de détail la segmentation lisse peu et garde les petites
// formes ; ailleurs elle nettoie fortement (aplats). Le calcul est dans libs/segmentation
// (`paint_detail_disc`, `SegmentationOptions::detail_mask`) ; ici seulement le geste (le même que
// le pinceau de déformation), la surcouche, la barre de réglages et la commande annulable
// (`PaintDetailMaskCommand`, un trait = un pas d'annulation).

#include <QAction>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QGraphicsEllipseItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QTransform>

#include <algorithm>
#include <cmath>

#include "app_theme.hpp"
#include "canvas_view.hpp"
#include "main_window.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "segmentation_dialog.hpp"
#include "ui_memory.hpp"

namespace openstitch::desktop {

namespace {

constexpr double kMinRadiusMm = 0.2;
constexpr double kMaxRadiusMm = 40.0;
constexpr double kBrushHardness =
    0.5; // bord doux : le lissage passe progressivement de l'un à l'autre
constexpr int kOverlayAlpha = 150;

QIcon detailIcon() {
    QPixmap pm(40, 40);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(AppTheme::instance().tokens().textSecondary);
    pen.setWidthF(2.6);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.drawEllipse(QPointF(20, 20), 14, 14);       // la zone peinte
    p.drawLine(QPointF(13, 20), QPointF(27, 20)); // un détail fin dedans
    p.drawLine(QPointF(20, 13), QPointF(20, 27));
    p.end();
    return QIcon(pm);
}

} // namespace

void MainWindow::buildDetailZonesTool() {
    detailRadiusMm_ = std::clamp(
        ui_memory::intValue(QStringLiteral("detailZones/radiusTenthMm"), 40, 2, 400) / 10.0,
        kMinRadiusMm, kMaxRadiusMm);

    toolDetailAct_ = new QAction(detailIcon(), tr("Zones de détail"), this);
    toolDetailAct_->setObjectName(QStringLiteral("tool_detailZones"));
    toolDetailAct_->setCheckable(true);
    toolDetailAct_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_D));
    toolDetailAct_->setToolTip(
        tr("Zones de détail (Maj+D)\nPeignez les endroits où la segmentation doit garder les "
           "petites formes (yeux, texte, motifs fins). Le reste de l'image est traité en aplat : "
           "lissé et nettoyé plus fortement. Cochez Gomme pour retirer une zone."));
    connect(toolDetailAct_, &QAction::triggered, this, [this] { setTool(Tool::DetailBrush); });

    clearDetailAct_ = new QAction(tr("Effacer toutes les zones de détail"), this);
    clearDetailAct_->setObjectName(QStringLiteral("action_clearDetailZones"));
    connect(clearDetailAct_, &QAction::triggered, this, [this] { clearDetailZones(); });

    connect(view_, &CanvasView::brushPressedMm, this, [this](QPointF p) { beginDetailStroke(p); });
    connect(view_, &CanvasView::brushMovedMm, this,
            [this](QPointF p, Qt::KeyboardModifiers) { moveDetailStroke(p); });
    connect(view_, &CanvasView::brushReleasedMm, this, [this](QPointF) { endDetailStroke(); });
    connect(view_, &CanvasView::brushCancelled, this, [this] { cancelDetailStroke(); });
    connect(view_, &CanvasView::cursorMovedMm, this, [this](QPointF physMm) {
        if (currentTool_ == Tool::DetailBrush) {
            updateDetailRing(QPointF(physMm.x(), -physMm.y()));
        }
    });
}

void MainWindow::attachDetailZonesTool() {
    if (toolPalette_ != nullptr && toolDetailAct_ != nullptr &&
        !toolPalette_->actions().contains(toolDetailAct_)) {
        toolPalette_->addAction(toolDetailAct_);
    }
    if (segmentMenu_ != nullptr && toolDetailAct_ != nullptr) {
        segmentMenu_->addSeparator();
        segmentMenu_->addAction(toolDetailAct_);
        segmentMenu_->addAction(clearDetailAct_);
    }
    if (detailBar_ != nullptr) {
        return;
    }
    detailBar_ = new QToolBar(tr("Zones de détail"), this);
    detailBar_->setObjectName(QStringLiteral("detailZonesBar"));
    detailBar_->setMovable(false);

    detailBar_->addWidget(new QLabel(tr(" Rayon : "), detailBar_));
    detailRadiusSpin_ = new QDoubleSpinBox(detailBar_);
    detailRadiusSpin_->setObjectName(QStringLiteral("detailZonesRadius"));
    detailRadiusSpin_->setRange(kMinRadiusMm, kMaxRadiusMm);
    detailRadiusSpin_->setDecimals(1);
    detailRadiusSpin_->setSingleStep(0.5);
    detailRadiusSpin_->setSuffix(tr(" mm"));
    detailRadiusSpin_->setValue(detailRadiusMm_);
    detailBar_->addWidget(detailRadiusSpin_);

    detailEraseCheck_ = new QCheckBox(tr("Gomme"), detailBar_);
    detailEraseCheck_->setObjectName(QStringLiteral("detailZonesErase"));
    detailEraseCheck_->setToolTip(tr("Retire des zones de détail au lieu d'en ajouter."));
    detailBar_->addWidget(detailEraseCheck_);

    auto* clearButton = new QPushButton(tr("Tout effacer"), detailBar_);
    clearButton->setObjectName(QStringLiteral("detailZonesClear"));
    detailBar_->addWidget(clearButton);

    connect(detailRadiusSpin_, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { setDetailRadiusMm(value); });
    connect(clearButton, &QPushButton::clicked, this, [this] { clearDetailZones(); });

    addToolBar(Qt::TopToolBarArea, detailBar_);
    detailBar_->hide();
}

void MainWindow::setDetailRadiusMm(double radiusMm) {
    detailRadiusMm_ = std::clamp(radiusMm, kMinRadiusMm, kMaxRadiusMm);
    ui_memory::setIntValue(QStringLiteral("detailZones/radiusTenthMm"),
                           static_cast<int>(std::lround(detailRadiusMm_ * 10.0)));
    if (detailRadiusSpin_ != nullptr && detailRadiusSpin_->value() != detailRadiusMm_) {
        QSignalBlocker block(detailRadiusSpin_);
        detailRadiusSpin_->setValue(detailRadiusMm_);
    }
    if (detailRing_ != nullptr) {
        const QPointF c = detailRing_->pos();
        detailRing_->setRect(-detailRadiusMm_, -detailRadiusMm_, 2 * detailRadiusMm_,
                             2 * detailRadiusMm_);
        detailRing_->setPos(c);
    }
}

void MainWindow::syncDetailZonesTool(Tool tool) {
    const bool active = tool == Tool::DetailBrush;
    if (toolDetailAct_ != nullptr) {
        QSignalBlocker block(toolDetailAct_);
        toolDetailAct_->setChecked(active);
    }
    if (detailBar_ != nullptr) {
        detailBar_->setVisible(active);
    }
    if (view_ != nullptr && active) {
        view_->setBrushMode(true, Context::DetailBrush);
    } // sinon syncBrushTool a déjà réglé le mode (Pinceau actif ou non)
    if (!active) {
        cancelDetailStroke();
        hideDetailRing();
        refreshDetailZonesOverlay();
        return;
    }
    if (toolLabel_ != nullptr) {
        toolLabel_->setText(tr("Outil : %1").arg(tr("Zones de détail")));
    }
    statusBar()->showMessage(
        project_.hasImage()
            ? tr("Zones de détail : peignez les endroits à préserver, puis Segmentation → "
                 "Segmenter l'image (réglages « dans les détails »).")
            : tr("Zones de détail : importez d'abord une image."));
    refreshDetailZonesOverlay();
}

void MainWindow::updateDetailRing(QPointF sceneMm) {
    if (currentTool_ != Tool::DetailBrush) {
        return;
    }
    if (detailRing_ == nullptr) {
        auto* ring = new QGraphicsEllipseItem(-detailRadiusMm_, -detailRadiusMm_,
                                              2 * detailRadiusMm_, 2 * detailRadiusMm_);
        QPen pen(AppTheme::instance().tokens().accent, 1.5, Qt::DashLine);
        pen.setCosmetic(true);
        ring->setPen(pen);
        ring->setBrush(Qt::NoBrush);
        ring->setZValue(99);
        ring->setAcceptedMouseButtons(Qt::NoButton);
        scene_->addItem(ring);
        baseItems_.append(ring);
        detailRing_ = ring;
    }
    detailRing_->setPos(sceneMm);
}

void MainWindow::hideDetailRing() {
    if (detailRing_ != nullptr) {
        baseItems_.removeAll(detailRing_);
        delete detailRing_;
        detailRing_ = nullptr;
    }
}

void MainWindow::refreshDetailZonesOverlay() {
    const segmentation::DetailMask& mask = detailStroke_ ? detailLive_ : project_.detail_mask;
    const bool wanted = (currentTool_ == Tool::DetailBrush || segmentDialog_ != nullptr) &&
                        !processed_.empty() && mask.matches(processed_.width, processed_.height) &&
                        mask.any_detail();
    if (!wanted) {
        if (detailOverlayItem_ != nullptr) {
            detailOverlayItem_->setVisible(false);
        }
        return;
    }
    const QColor accent = AppTheme::instance().tokens().accent;
    QImage image(mask.width, mask.height, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < mask.height; ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < mask.width; ++x) {
            const int w =
                mask.weights[static_cast<std::size_t>(y) * static_cast<std::size_t>(mask.width) +
                             static_cast<std::size_t>(x)];
            const int alpha = w * kOverlayAlpha / 255;
            line[x] = qPremultiply(qRgba(accent.red(), accent.green(), accent.blue(), alpha));
        }
    }
    if (detailOverlayItem_ == nullptr) {
        detailOverlayItem_ = new QGraphicsPixmapItem();
        detailOverlayItem_->setZValue(11.0);
        detailOverlayItem_->setAcceptedMouseButtons(Qt::NoButton);
        scene_->addItem(detailOverlayItem_);
    }
    const double mmPerPx = project_.mm_per_px.value;
    detailOverlayItem_->setPixmap(QPixmap::fromImage(image));
    detailOverlayItem_->setTransform(QTransform::fromScale(mmPerPx, mmPerPx));
    detailOverlayItem_->setPos(-mask.width / 2.0 * mmPerPx, -mask.height / 2.0 * mmPerPx);
    detailOverlayItem_->setVisible(true);
}

namespace {

// Pixel de l'image de travail sous un point de la scène (mm, Y vers le bas), image centrée.
bool pixelAt(const image::Image& img, double mmPerPx, QPointF sceneMm, int& px, int& py) {
    if (img.empty() || !(mmPerPx > 0.0)) {
        return false;
    }
    px = static_cast<int>(std::floor((sceneMm.x() + img.width * mmPerPx / 2.0) / mmPerPx));
    py = static_cast<int>(std::floor((sceneMm.y() + img.height * mmPerPx / 2.0) / mmPerPx));
    return true; // hors image aussi : le disque peut mordre sur le bord
}

int radiusPx(double radiusMm, double mmPerPx) {
    return std::max(1, static_cast<int>(std::lround(radiusMm / mmPerPx)));
}

} // namespace

void MainWindow::beginDetailStroke(QPointF sceneMm) {
    detailStroke_.reset();
    if (currentTool_ != Tool::DetailBrush) {
        return;
    }
    if (processed_.empty()) {
        statusBar()->showMessage(tr("Zones de détail : importez d'abord une image."), 4000);
        return;
    }
    detailLive_ = project_.detail_mask.matches(processed_.width, processed_.height)
                      ? project_.detail_mask
                      : segmentation::DetailMask{};
    detailStroke_ =
        DetailStroke{{}, sceneMm, detailEraseCheck_ != nullptr && detailEraseCheck_->isChecked()};
    moveDetailStroke(sceneMm);
}

void MainWindow::moveDetailStroke(QPointF sceneMm) {
    if (currentTool_ != Tool::DetailBrush) {
        return;
    }
    updateDetailRing(sceneMm);
    if (!detailStroke_) {
        return;
    }
    const double mmPerPx = project_.mm_per_px.value;
    const QPointF step = sceneMm - detailStroke_->lastSceneMm;
    // Disques espacés d'au moins un tiers de rayon (sauf le premier) : borne le coût du geste.
    if (!detailStroke_->centers.empty() && std::hypot(step.x(), step.y()) < detailRadiusMm_ / 3.0) {
        return;
    }
    int px = 0;
    int py = 0;
    if (!pixelAt(processed_, mmPerPx, sceneMm, px, py)) {
        return;
    }
    detailStroke_->centers.emplace_back(px, py);
    detailStroke_->lastSceneMm = sceneMm;
    segmentation::paint_detail_disc(detailLive_, processed_.width, processed_.height, px, py,
                                    radiusPx(detailRadiusMm_, mmPerPx),
                                    detailStroke_->erase ? 0 : 255, kBrushHardness);
    refreshDetailZonesOverlay();
}

void MainWindow::cancelDetailStroke() {
    if (!detailStroke_) {
        return;
    }
    detailStroke_.reset();
    detailLive_ = {};
    refreshDetailZonesOverlay();
}

void MainWindow::endDetailStroke() {
    if (!detailStroke_ || processed_.empty()) {
        detailStroke_.reset();
        return;
    }
    const DetailStroke stroke = std::move(*detailStroke_);
    detailStroke_.reset();
    detailLive_ = {};
    const int width = processed_.width;
    const int height = processed_.height;
    const int radius = radiusPx(detailRadiusMm_, project_.mm_per_px.value);
    const std::uint64_t generation = documentGeneration_;
    // Différé : refreshImage() reconstruit la scène pendant l'évènement souris.
    QTimer::singleShot(0, this, [this, stroke, width, height, radius, generation] {
        if (generation != documentGeneration_ || stroke.centers.empty()) {
            refreshDetailZonesOverlay();
            return;
        }
        if (!project_.detail_mask.empty() && !project_.detail_mask.matches(width, height)) {
            // Masque d'une autre taille (image recadrée depuis) : repart de zéro, annulable.
            undoStack_.execute(std::make_unique<commands::ClearDetailMaskCommand>(), project_);
        }
        undoStack_.execute(std::make_unique<commands::PaintDetailMaskCommand>(
                               width, height, stroke.centers, radius, stroke.erase, kBrushHardness),
                           project_);
        refreshImage();
        updateActions();
        if (segmentDialog_ != nullptr) {
            requestSegmentationPreview(segmentDialog_->options()); // l'aperçu suit le masque
        }
    });
}

void MainWindow::clearDetailZones() {
    if (!project_.detail_mask.any_detail()) {
        statusBar()->showMessage(tr("Aucune zone de détail à effacer."), 3000);
        return;
    }
    undoStack_.execute(std::make_unique<commands::ClearDetailMaskCommand>(), project_);
    refreshImage();
    updateActions();
    if (segmentDialog_ != nullptr) {
        requestSegmentationPreview(segmentDialog_->options());
    }
    statusBar()->showMessage(tr("Zones de détail effacées (Ctrl+Z pour annuler)."), 4000);
}

} // namespace openstitch::desktop
