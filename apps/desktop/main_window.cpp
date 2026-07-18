// SPDX-License-Identifier: Apache-2.0
#include "main_window.hpp"

#include <QAction>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGridLayout>
#include <QImage>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPixmap>
#include <QStatusBar>

#include <filesystem>

#include "canvas_view.hpp"
#include "import_dialog.hpp"
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
    view_->setCanvasSizeMm(QSizeF(to_millimeters(canvas.width).value,
                                  to_millimeters(canvas.height).value));

    // Disposition : coin "mm" + règles alignées sur le viewport de la vue.
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

    auto* fileMenu = menuBar()->addMenu(tr("&Fichier"));
    auto* openAct = fileMenu->addAction(tr("&Ouvrir une image…"));
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MainWindow::openImage);
    fileMenu->addSeparator();
    auto* quitAct = fileMenu->addAction(tr("&Quitter"));
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, this, &QWidget::close);

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

    cursorLabel_ = new QLabel(this);
    cursorLabel_->setMinimumWidth(180);
    statusBar()->addPermanentWidget(cursorLabel_);
    connect(view_, &CanvasView::cursorMovedMm, this, [this](QPointF mm) {
        cursorLabel_->setText(tr("x : %1 mm   y : %2 mm")
                                  .arg(mm.x(), 0, 'f', 1)
                                  .arg(mm.y(), 0, 'f', 1));
    });

    statusBar()->showMessage(tr("Ouvrez une image (PNG, JPEG, BMP, TIFF) pour commencer."));
    view_->fitCanvas();
}

void MainWindow::openImage() {
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Ouvrir une image"), QString(),
        tr("Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff);;Tous les fichiers (*)"));
    if (file.isEmpty()) {
        return;
    }

    const auto loaded = image::load_image(std::filesystem::path(file.toStdWString()));
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

    const QImage qimg(loaded->rgba.data(), loaded->width, loaded->height,
                      loaded->width * 4, QImage::Format_RGBA8888);
    const QPixmap pixmap = QPixmap::fromImage(qimg.copy());

    scene_->clear();
    auto* item = scene_->addPixmap(pixmap);
    // Échelle : la scène est en millimètres, le pixmap en pixels.
    const double mmPerPx = document::mm_per_pixel(*placement, loaded->width).value;
    const double wMm = to_millimeters(placement->width).value;
    const double hMm = to_millimeters(placement->height).value;
    item->setTransform(QTransform::fromScale(mmPerPx,
                                             hMm / loaded->height));
    item->setPos(-wMm / 2.0, -hMm / 2.0);

    view_->fitCanvas();
    statusBar()->showMessage(tr("%1 — %2 × %3 px — %4 × %5 mm")
                                 .arg(QFileInfo(file).fileName())
                                 .arg(loaded->width)
                                 .arg(loaded->height)
                                 .arg(wMm, 0, 'f', 1)
                                 .arg(hMm, 0, 'f', 1));
}

}  // namespace openstitch::desktop
