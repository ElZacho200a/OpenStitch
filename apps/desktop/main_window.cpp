// SPDX-License-Identifier: Apache-2.0
#include "main_window.hpp"

#include <QAction>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGridLayout>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPixmap>
#include <QStatusBar>

#include <filesystem>

#include "brightness_dialog.hpp"
#include "canvas_view.hpp"
#include "import_dialog.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/core/app_info.hpp"
#include "openstitch/document/canvas.hpp"
#include "openstitch/document/image_placement.hpp"
#include "openstitch/image/image.hpp"
#include "ruler.hpp"

namespace openstitch::desktop {

MainWindow::MainWindow() {
    setWindowTitle(QString::fromUtf8(kAppName));
    resize(1100, 800);

    scene_ = new QGraphicsScene(this);
    view_ = new CanvasView(scene_, this);

    const document::Canvas canvas;
    view_->setCanvasSizeMm(
        QSizeF(to_millimeters(canvas.width).value, to_millimeters(canvas.height).value));

    auto* central = new QWidget(this);
    auto* grid = new QGridLayout(central);
    grid->setSpacing(0);
    grid->setContentsMargins(0, 0, 0, 0);
    auto* corner = new QLabel(tr("mm"), central);
    corner->setAlignment(Qt::AlignCenter);
    grid->addWidget(corner, 0, 0);
    grid->addWidget(new Ruler(Qt::Horizontal, view_, central), 0, 1);
    grid->addWidget(new Ruler(Qt::Vertical, view_, central), 1, 0);
    grid->addWidget(view_, 1, 1);
    setCentralWidget(central);

    buildMenus();

    cursorLabel_ = new QLabel(this);
    cursorLabel_->setMinimumWidth(180);
    statusBar()->addPermanentWidget(cursorLabel_);
    connect(view_, &CanvasView::cursorMovedMm, this, [this](QPointF mm) {
        cursorLabel_->setText(
            tr("x : %1 mm   y : %2 mm").arg(mm.x(), 0, 'f', 1).arg(mm.y(), 0, 'f', 1));
    });
    connect(view_, &CanvasView::cropSelectedMm, this, &MainWindow::onCropSelected);

    statusBar()->showMessage(tr("Ouvrez une image (PNG, JPEG, BMP, TIFF) pour commencer."));
    view_->fitCanvas();
    updateActions();
}

void MainWindow::buildMenus() {
    auto* fileMenu = menuBar()->addMenu(tr("&Fichier"));
    auto* openAct = fileMenu->addAction(tr("&Ouvrir une image…"));
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MainWindow::openImage);
    fileMenu->addSeparator();
    auto* quitAct = fileMenu->addAction(tr("&Quitter"));
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, this, &QWidget::close);

    auto* editMenu = menuBar()->addMenu(tr("&Édition"));
    undoAct_ = editMenu->addAction(tr("&Annuler"));
    undoAct_->setShortcut(QKeySequence::Undo);
    connect(undoAct_, &QAction::triggered, this, &MainWindow::undo);
    redoAct_ = editMenu->addAction(tr("&Rétablir"));
    redoAct_->setShortcut(QKeySequence::Redo);
    connect(redoAct_, &QAction::triggered, this, &MainWindow::redo);

    auto* imageMenu = menuBar()->addMenu(tr("&Image"));
    const auto addOpAction = [this, imageMenu](const QString& text, image::ImageOp op) {
        auto* act = imageMenu->addAction(text);
        connect(act, &QAction::triggered, this, [this, op] { executeOp(op); });
        imageActions_.append(act);
        return act;
    };

    addOpAction(tr("Niveaux de &gris"), image::GrayscaleOp{});

    auto* bcAct = imageMenu->addAction(tr("&Luminosité/contraste…"));
    connect(bcAct, &QAction::triggered, this, &MainWindow::adjustBrightnessContrast);
    imageActions_.append(bcAct);

    addOpAction(tr("Débruitage lé&ger"), image::MedianDenoiseOp{1});
    addOpAction(tr("Débruitage &moyen"), image::MedianDenoiseOp{2});

    auto* quantAct = imageMenu->addAction(tr("&Quantifier les couleurs…"));
    connect(quantAct, &QAction::triggered, this, &MainWindow::quantizeColors);
    imageActions_.append(quantAct);

    imageMenu->addSeparator();
    addOpAction(tr("Symétrie &horizontale"), image::FlipOp{true});
    addOpAction(tr("Symétrie &verticale"), image::FlipOp{false});
    addOpAction(tr("Rotation 90° h&oraire"), image::Rotate90Op{1});
    addOpAction(tr("Rotation 90° &antihoraire"), image::Rotate90Op{3});

    imageMenu->addSeparator();
    cropAct_ = imageMenu->addAction(tr("&Recadrer (sélection)"));
    cropAct_->setCheckable(true);
    connect(cropAct_, &QAction::toggled, view_, &CanvasView::setCropMode);
    imageActions_.append(cropAct_);

    auto* viewMenu = menuBar()->addMenu(tr("&Affichage"));
    auto* zoomInAct = viewMenu->addAction(tr("Zoom &avant"));
    zoomInAct->setShortcut(QKeySequence::ZoomIn);
    connect(zoomInAct, &QAction::triggered, view_, &CanvasView::zoomIn);
    auto* zoomOutAct = viewMenu->addAction(tr("Zoom a&rrière"));
    zoomOutAct->setShortcut(QKeySequence::ZoomOut);
    connect(zoomOutAct, &QAction::triggered, view_, &CanvasView::zoomOut);
    auto* fitAct = viewMenu->addAction(tr("A&juster au canevas"));
    fitAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    connect(fitAct, &QAction::triggered, view_, &CanvasView::fitCanvas);
}

void MainWindow::openImage() {
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Ouvrir une image"), QString(),
        tr("Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff);;Tous les fichiers (*)"));
    if (file.isEmpty()) {
        return;
    }

    auto loaded = image::load_image(std::filesystem::path(file.toStdWString()));
    if (!loaded) {
        QMessageBox::warning(this, tr("Erreur"), QString::fromStdString(loaded.error().message));
        return;
    }

    ImportDialog dialog(loaded->width, loaded->height, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const auto placement = dialog.placement();
    if (!placement) {
        QMessageBox::warning(this, tr("Erreur"), tr("Taille physique invalide."));
        return;
    }

    project_.mm_per_px = document::mm_per_pixel(*placement, loaded->width);
    project_.original = std::move(*loaded);
    project_.ops.clear();
    undoStack_.clear();

    refreshImage();
    view_->fitCanvas();
    statusBar()->showMessage(tr("%1 — %2 × %3 px")
                                 .arg(QFileInfo(file).fileName())
                                 .arg(project_.original.width)
                                 .arg(project_.original.height));
    updateActions();
}

void MainWindow::executeOp(image::ImageOp op) {
    if (!project_.hasImage()) {
        return;
    }
    // Validation avant mutation : une opération invalide n'entre pas dans la pile.
    auto preview = image::apply_op(processed_, op);
    if (!preview) {
        QMessageBox::warning(this, tr("Opération impossible"),
                             QString::fromStdString(preview.error().message));
        return;
    }
    undoStack_.execute(std::make_unique<commands::AppendImageOpCommand>(std::move(op)), project_);
    refreshImage();
    updateActions();
}

void MainWindow::undo() {
    if (undoStack_.undo(project_)) {
        refreshImage();
        updateActions();
    }
}

void MainWindow::redo() {
    if (undoStack_.redo(project_)) {
        refreshImage();
        updateActions();
    }
}

void MainWindow::adjustBrightnessContrast() {
    if (!project_.hasImage()) {
        return;
    }
    BrightnessDialog dialog(this);
    connect(&dialog, &BrightnessDialog::previewRequested, this, [this](double b, double c) {
        if (const auto img = image::apply_op(processed_, image::BrightnessContrastOp{b, c})) {
            displayImage(*img);
        }
    });
    if (dialog.exec() == QDialog::Accepted &&
        (dialog.brightness() != 0.0 || dialog.contrast() != 0.0)) {
        executeOp(image::BrightnessContrastOp{dialog.brightness(), dialog.contrast()});
    } else {
        displayImage(processed_);  // retire l'aperçu
    }
}

void MainWindow::quantizeColors() {
    if (!project_.hasImage()) {
        return;
    }
    bool ok = false;
    const int colors = QInputDialog::getInt(this, tr("Quantifier les couleurs"),
                                            tr("Nombre maximal de couleurs :"), 8, 2, 64, 1, &ok);
    if (ok) {
        executeOp(image::QuantizeOp{colors});
    }
}

void MainWindow::onCropSelected(QRectF rectMm) {
    cropAct_->setChecked(false);
    if (!project_.hasImage() || processed_.empty()) {
        return;
    }
    const double mmPerPx = project_.mm_per_px.value;
    const double wMm = processed_.width * mmPerPx;
    const double hMm = processed_.height * mmPerPx;
    // Coin haut-gauche de l'image dans la scène (image centrée sur le canevas).
    const double left = -wMm / 2.0;
    const double top = -hMm / 2.0;

    image::CropOp op;
    op.x = static_cast<int>(std::floor((rectMm.left() - left) / mmPerPx));
    op.y = static_cast<int>(std::floor((rectMm.top() - top) / mmPerPx));
    op.width = static_cast<int>(std::lround(rectMm.width() / mmPerPx));
    op.height = static_cast<int>(std::lround(rectMm.height() / mmPerPx));
    executeOp(op);
}

void MainWindow::refreshImage() {
    if (!project_.hasImage()) {
        scene_->clear();
        processed_ = {};
        return;
    }
    const auto result = image::apply_pipeline(project_.original, project_.ops);
    if (!result) {
        QMessageBox::warning(this, tr("Erreur"), QString::fromStdString(result.error().message));
        return;
    }
    processed_ = *result;
    displayImage(processed_);
}

void MainWindow::displayImage(const image::Image& img) {
    if (img.empty()) {
        return;
    }
    const QImage qimg(img.rgba.data(), img.width, img.height, img.width * 4,
                      QImage::Format_RGBA8888);
    const QPixmap pixmap = QPixmap::fromImage(qimg.copy());

    scene_->clear();
    auto* item = scene_->addPixmap(pixmap);
    const double mmPerPx = project_.mm_per_px.value;
    const double wMm = img.width * mmPerPx;
    const double hMm = img.height * mmPerPx;
    item->setTransform(QTransform::fromScale(mmPerPx, mmPerPx));
    item->setPos(-wMm / 2.0, -hMm / 2.0);
}

void MainWindow::updateActions() {
    const bool hasImage = project_.hasImage();
    for (QAction* act : imageActions_) {
        act->setEnabled(hasImage);
    }
    undoAct_->setEnabled(undoStack_.canUndo());
    redoAct_->setEnabled(undoStack_.canRedo());
    undoAct_->setText(undoStack_.canUndo()
                          ? tr("&Annuler %1").arg(QString::fromStdString(undoStack_.undoName()))
                          : tr("&Annuler"));
    redoAct_->setText(undoStack_.canRedo()
                          ? tr("&Rétablir %1").arg(QString::fromStdString(undoStack_.redoName()))
                          : tr("&Rétablir"));
}

}  // namespace openstitch::desktop
