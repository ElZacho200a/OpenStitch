// SPDX-License-Identifier: Apache-2.0
// Outil Clic-pour-broder / baguette magique (HP-AUTO-003) : un clic sur une zone de couleur de
// l'image crée l'objet brodé (remplissage, satin ou contour). Le calcul est dans libs :
// `segmentation::grow_region` (croissance locale à tolérance réglable) puis
// `commands::make_click_to_stitch_command` (vectorisation + objet de broderie, UNE commande
// annulable). Ici seulement le câblage : outil, barre de réglages (tolérance, type), aperçu de la
// zone sous le curseur, clic -> commande -> sélection de l'objet créé.

#include <QAction>
#include <QComboBox>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QTransform>

#include <algorithm>
#include <array>
#include <cmath>

#include "app_theme.hpp"
#include "busy_indicator.hpp"
#include "canvas_view.hpp"
#include "main_window.hpp"
#include "openstitch/commands/click_to_stitch_commands.hpp"
#include "openstitch/segmentation/region_growing.hpp"
#include "ui_memory.hpp"

namespace openstitch::desktop {

namespace {

constexpr int kMaxTolerance = 60; // ΔE76 : au-delà, deux teintes distinctes se confondent
constexpr int kDefaultTolerance = 12;
constexpr int kPreviewDelayMs = 40;               // anti-rebond du calcul d'aperçu au survol
constexpr std::size_t kBusyThresholdPx = 200'000; // en dessous : calcul direct, sans fenêtre
constexpr std::size_t kHoleFillPx = 8;            // pixels isolés d'anti-crénelage comblés
const char* const kKindLabels[] = {QT_TR_NOOP("Automatique"), QT_TR_NOOP("Remplissage"),
                                   QT_TR_NOOP("Satin"), QT_TR_NOOP("Contour seul")};
constexpr int kKindCount = 4;

QIcon wandIcon() {
    QPixmap pm(40, 40);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(AppTheme::instance().tokens().textSecondary);
    pen.setWidthF(2.8);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    // Une baguette et ses étincelles.
    p.drawLine(QPointF(8, 33), QPointF(25, 16));
    p.drawLine(QPointF(27, 6), QPointF(27, 12));
    p.drawLine(QPointF(24, 9), QPointF(30, 9));
    p.drawLine(QPointF(33, 18), QPointF(33, 22));
    p.drawLine(QPointF(31, 20), QPointF(35, 20));
    p.end();
    return QIcon(pm);
}

// Pixel de l'image affichée sous un point de la scène (mm, Y vers le bas) : image centrée sur
// l'origine, `mmPerPx` par pixel (cf. renderBase). Faux hors de l'image.
bool pixelUnder(const image::Image& img, double mmPerPx, QPointF sceneMm, int& px, int& py) {
    if (img.empty() || !(mmPerPx > 0.0)) {
        return false;
    }
    const double wMm = img.width * mmPerPx;
    const double hMm = img.height * mmPerPx;
    px = static_cast<int>(std::floor((sceneMm.x() + wMm / 2.0) / mmPerPx));
    py = static_cast<int>(std::floor((sceneMm.y() + hMm / 2.0) / mmPerPx));
    return px >= 0 && py >= 0 && px < img.width && py < img.height;
}

segmentation::GrowOptions growOptions(int tolerance) {
    segmentation::GrowOptions options;
    options.tolerance = static_cast<double>(tolerance);
    options.fill_holes_up_to_px = kHoleFillPx;
    return options;
}

commands::ClickToStitchKind kindFromIndex(int index) {
    switch (index) {
    case 1:
        return commands::ClickToStitchKind::Fill;
    case 2:
        return commands::ClickToStitchKind::Satin;
    case 3:
        return commands::ClickToStitchKind::Contour;
    default:
        return commands::ClickToStitchKind::Auto;
    }
}

} // namespace

void MainWindow::buildClickToStitchTool() {
    clickToStitchTolerance_ = ui_memory::intValue(QStringLiteral("clickToStitch/tolerance"),
                                                  kDefaultTolerance, 0, kMaxTolerance);
    clickToStitchKind_ =
        ui_memory::intValue(QStringLiteral("clickToStitch/kind"), 0, 0, kKindCount - 1);

    toolClickToStitchAct_ = new QAction(wandIcon(), tr("Clic-pour-broder"), this);
    toolClickToStitchAct_->setObjectName(QStringLiteral("tool_clickToStitch"));
    toolClickToStitchAct_->setCheckable(true);
    toolClickToStitchAct_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_W));
    toolClickToStitchAct_->setToolTip(
        tr("Clic-pour-broder (Maj+W)\nCliquez sur une zone de couleur de l'image : elle devient un "
           "objet brodé (remplissage, satin ou contour). La tolérance règle l'étendue de la "
           "zone ; la zone qui sera brodée s'affiche au survol."));
    connect(toolClickToStitchAct_, &QAction::triggered, this,
            [this] { setTool(Tool::ClickToStitch); });

    // Aperçu différé : une croissance par mouvement de souris serait inutilement coûteuse.
    clickToStitchPreviewTimer_ = new QTimer(this);
    clickToStitchPreviewTimer_->setSingleShot(true);
    clickToStitchPreviewTimer_->setInterval(kPreviewDelayMs);
    connect(clickToStitchPreviewTimer_, &QTimer::timeout, this, [this] {
        if (currentTool_ == Tool::ClickToStitch && clickToStitchHoverMm_) {
            updateClickToStitchPreview(*clickToStitchHoverMm_);
        }
    });
    connect(view_, &CanvasView::cursorMovedMm, this, [this](QPointF physicalMm) {
        if (currentTool_ != Tool::ClickToStitch) {
            return;
        }
        clickToStitchHoverMm_ = QPointF(physicalMm.x(), -physicalMm.y());
        clickToStitchPreviewTimer_->start();
    });
    connect(view_, &CanvasView::cursorLeftViewport, this, [this] {
        clickToStitchHoverMm_.reset();
        hideClickToStitchPreview();
    });
}

void MainWindow::attachClickToStitchTool() {
    if (toolPalette_ != nullptr && toolClickToStitchAct_ != nullptr &&
        !toolPalette_->actions().contains(toolClickToStitchAct_)) {
        toolPalette_->addAction(toolClickToStitchAct_);
    }
    if (clickToStitchBar_ != nullptr) {
        return;
    }
    // Barre de réglages, visible seulement quand l'outil est actif.
    clickToStitchBar_ = new QToolBar(tr("Clic-pour-broder"), this);
    clickToStitchBar_->setObjectName(QStringLiteral("clickToStitchBar"));
    clickToStitchBar_->setMovable(false);

    clickToStitchBar_->addWidget(new QLabel(tr(" Tolérance : "), clickToStitchBar_));
    clickToStitchToleranceSlider_ = new QSlider(Qt::Horizontal, clickToStitchBar_);
    clickToStitchToleranceSlider_->setObjectName(QStringLiteral("clickToStitchTolerance"));
    clickToStitchToleranceSlider_->setRange(0, kMaxTolerance);
    clickToStitchToleranceSlider_->setValue(clickToStitchTolerance_);
    clickToStitchToleranceSlider_->setFixedWidth(160);
    clickToStitchToleranceSlider_->setToolTip(
        tr("Écart de couleur accepté autour du pixel cliqué. Plus la valeur est grande, plus la "
           "zone s'étend aux teintes voisines."));
    clickToStitchBar_->addWidget(clickToStitchToleranceSlider_);
    clickToStitchToleranceSpin_ = new QSpinBox(clickToStitchBar_);
    clickToStitchToleranceSpin_->setObjectName(QStringLiteral("clickToStitchToleranceSpin"));
    clickToStitchToleranceSpin_->setRange(0, kMaxTolerance);
    clickToStitchToleranceSpin_->setValue(clickToStitchTolerance_);
    clickToStitchBar_->addWidget(clickToStitchToleranceSpin_);

    clickToStitchBar_->addWidget(new QLabel(tr("   Type : "), clickToStitchBar_));
    clickToStitchKindCombo_ = new QComboBox(clickToStitchBar_);
    clickToStitchKindCombo_->setObjectName(QStringLiteral("clickToStitchKind"));
    for (const char* label : kKindLabels) {
        clickToStitchKindCombo_->addItem(tr(label));
    }
    clickToStitchKindCombo_->setCurrentIndex(clickToStitchKind_);
    clickToStitchKindCombo_->setToolTip(
        tr("Automatique : remplissage, ou satin pour une bande étroite, ou contour pour une "
           "petite zone. « Contour seul » ne coud que le bord de la zone."));
    clickToStitchBar_->addWidget(clickToStitchKindCombo_);

    connect(clickToStitchToleranceSlider_, &QSlider::valueChanged, this,
            [this](int value) { setClickToStitchTolerance(value); });
    connect(clickToStitchToleranceSpin_, &QSpinBox::valueChanged, this,
            [this](int value) { setClickToStitchTolerance(value); });
    connect(clickToStitchKindCombo_, &QComboBox::currentIndexChanged, this,
            [this](int index) { setClickToStitchKind(index); });

    addToolBar(Qt::TopToolBarArea, clickToStitchBar_);
    clickToStitchBar_->hide();
}

void MainWindow::syncClickToStitchTool(Tool tool) {
    if (toolClickToStitchAct_ != nullptr) {
        QSignalBlocker block(toolClickToStitchAct_);
        toolClickToStitchAct_->setChecked(tool == Tool::ClickToStitch);
    }
    if (clickToStitchBar_ != nullptr) {
        clickToStitchBar_->setVisible(tool == Tool::ClickToStitch);
    }
    if (tool != Tool::ClickToStitch) {
        clickToStitchHoverMm_.reset();
        if (clickToStitchPreviewTimer_ != nullptr) {
            clickToStitchPreviewTimer_->stop();
        }
        hideClickToStitchPreview();
        return;
    }
    if (toolLabel_ != nullptr) {
        toolLabel_->setText(tr("Outil : %1").arg(tr("Clic-pour-broder")));
    }
    view_->viewport()->setCursor(Qt::CrossCursor);
    statusBar()->showMessage(
        tr("Clic-pour-broder : cliquez sur une zone de couleur de l'image pour la broder "
           "(tolérance et type dans la barre)."));
}

void MainWindow::setClickToStitchTolerance(int tolerance) {
    clickToStitchTolerance_ = std::clamp(tolerance, 0, kMaxTolerance);
    ui_memory::setIntValue(QStringLiteral("clickToStitch/tolerance"), clickToStitchTolerance_);
    // Les deux contrôles restent synchrones sans se rappeler l'un l'autre.
    if (clickToStitchToleranceSlider_ != nullptr &&
        clickToStitchToleranceSlider_->value() != clickToStitchTolerance_) {
        QSignalBlocker block(clickToStitchToleranceSlider_);
        clickToStitchToleranceSlider_->setValue(clickToStitchTolerance_);
    }
    if (clickToStitchToleranceSpin_ != nullptr &&
        clickToStitchToleranceSpin_->value() != clickToStitchTolerance_) {
        QSignalBlocker block(clickToStitchToleranceSpin_);
        clickToStitchToleranceSpin_->setValue(clickToStitchTolerance_);
    }
    if (currentTool_ == Tool::ClickToStitch && clickToStitchHoverMm_) {
        updateClickToStitchPreview(*clickToStitchHoverMm_); // l'aperçu suit le réglage
    }
}

void MainWindow::setClickToStitchKind(int kindIndex) {
    clickToStitchKind_ = std::clamp(kindIndex, 0, kKindCount - 1);
    ui_memory::setIntValue(QStringLiteral("clickToStitch/kind"), clickToStitchKind_);
    if (clickToStitchKindCombo_ != nullptr &&
        clickToStitchKindCombo_->currentIndex() != clickToStitchKind_) {
        QSignalBlocker block(clickToStitchKindCombo_);
        clickToStitchKindCombo_->setCurrentIndex(clickToStitchKind_);
    }
}

void MainWindow::hideClickToStitchPreview() {
    clickToStitchPreviewPixels_ = 0;
    if (clickToStitchPreviewItem_ != nullptr) {
        clickToStitchPreviewItem_->setVisible(false);
    }
}

void MainWindow::updateClickToStitchPreview(QPointF sceneMm) {
    int px = 0;
    int py = 0;
    if (!pixelUnder(processed_, project_.mm_per_px.value, sceneMm, px, py)) {
        hideClickToStitchPreview();
        return;
    }
    const auto grown =
        segmentation::grow_region(processed_, px, py, growOptions(clickToStitchTolerance_));
    if (!grown || grown->empty()) {
        hideClickToStitchPreview();
        return;
    }
    const QColor accent = AppTheme::instance().tokens().accent;
    const auto tint = std::array<std::uint8_t, 3>{static_cast<std::uint8_t>(accent.red()),
                                                  static_cast<std::uint8_t>(accent.green()),
                                                  static_cast<std::uint8_t>(accent.blue())};
    const image::Image overlay = segmentation::render_grown_preview(*grown, tint);
    const QImage qimg(overlay.rgba.data(), overlay.width, overlay.height, overlay.width * 4,
                      QImage::Format_RGBA8888);
    if (clickToStitchPreviewItem_ == nullptr) {
        clickToStitchPreviewItem_ = new QGraphicsPixmapItem();
        clickToStitchPreviewItem_->setZValue(12.0);
        clickToStitchPreviewItem_->setAcceptedMouseButtons(Qt::NoButton);
        scene_->addItem(clickToStitchPreviewItem_);
    }
    const double mmPerPx = project_.mm_per_px.value;
    clickToStitchPreviewItem_->setPixmap(QPixmap::fromImage(qimg.copy()));
    clickToStitchPreviewItem_->setTransform(QTransform::fromScale(mmPerPx, mmPerPx));
    clickToStitchPreviewItem_->setPos((grown->x0 - processed_.width / 2.0) * mmPerPx,
                                      (grown->y0 - processed_.height / 2.0) * mmPerPx);
    clickToStitchPreviewItem_->setVisible(true);
    clickToStitchPreviewPixels_ = grown->pixel_count;
}

void MainWindow::clickToStitchAt(QPointF sceneMm) {
    int px = 0;
    int py = 0;
    if (processed_.empty()) {
        statusBar()->showMessage(tr("Clic-pour-broder : aucune image (importez-en une d'abord)."),
                                 5000);
        return;
    }
    if (!pixelUnder(processed_, project_.mm_per_px.value, sceneMm, px, py)) {
        statusBar()->showMessage(tr("Clic-pour-broder : cliquez sur l'image."), 4000);
        return;
    }
    const auto grown =
        segmentation::grow_region(processed_, px, py, growOptions(clickToStitchTolerance_));
    if (!grown) {
        statusBar()->showMessage(
            tr("Clic-pour-broder : %1").arg(QString::fromStdString(grown.error().message)), 6000);
        return;
    }

    commands::ClickToStitchOptions options;
    options.mm_per_px = project_.mm_per_px;
    options.kind = kindFromIndex(clickToStitchKind_);
    // Les identifiants sont tirés d'une COPIE du générateur : le calcul peut tourner hors du fil
    // d'interface (grande zone) sans toucher au document, puis le compteur est reporté.
    IdGenerator<ObjectId> ids = project_.object_ids;
    commands::ClickToStitchResult result;
    if (grown->pixel_count > kBusyThresholdPx) {
        BusyIndicator busy(this, tr("Vectorisation de la zone…"));
        result =
            busy.run([&] { return commands::make_click_to_stitch_command(ids, *grown, options); });
    } else {
        result = commands::make_click_to_stitch_command(ids, *grown, options);
    }
    if (!result.command) {
        statusBar()->showMessage(
            tr("Clic-pour-broder : %1").arg(QString::fromStdString(result.error)), 6000);
        return;
    }
    project_.object_ids = ids;
    undoStack_.execute(std::move(result.command), project_);
    hideClickToStitchPreview();
    setSelection({.region = std::nullopt,
                  .embroidery = result.embroidery_id,
                  .objects = {},
                  .extraRegions = {}});
    showStitchesAct_->setChecked(true);
    refreshImage();
    updateActions();
    statusBar()->showMessage(
        tr("Zone brodée (%1 px) — Ctrl+Z pour annuler, ou ajustez la tolérance et recliquez.")
            .arg(grown->pixel_count),
        6000);
}

} // namespace openstitch::desktop
