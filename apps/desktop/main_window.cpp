// SPDX-License-Identifier: Apache-2.0
#include "main_window.hpp"

#include <QAction>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QImage>
#include <QMenuBar>
#include <QMessageBox>
#include <QPixmap>
#include <QStatusBar>

#include <filesystem>

#include "openstitch/core/app_info.hpp"
#include "openstitch/image/image.hpp"

namespace openstitch::desktop {

MainWindow::MainWindow() {
    setWindowTitle(QString::fromUtf8(kAppName));
    resize(1024, 768);

    scene_ = new QGraphicsScene(this);
    view_ = new QGraphicsView(scene_, this);
    view_->setDragMode(QGraphicsView::ScrollHandDrag);
    setCentralWidget(view_);

    auto* fileMenu = menuBar()->addMenu(tr("&Fichier"));
    auto* openAct = fileMenu->addAction(tr("&Ouvrir une image…"));
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MainWindow::openImage);
    fileMenu->addSeparator();
    auto* quitAct = fileMenu->addAction(tr("&Quitter"));
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, this, &QWidget::close);

    statusBar()->showMessage(tr("Ouvrez une image (PNG, JPEG, BMP, TIFF) pour commencer."));
}

void MainWindow::openImage() {
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Ouvrir une image"), QString(),
        tr("Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff);;Tous les fichiers (*)"));
    if (file.isEmpty()) {
        return;
    }

    // Chargement par la bibliothèque cœur, pas par Qt : exerce la même
    // chaîne que la CLI et valide la séparation interface / métier.
    const auto loaded = image::load_image(std::filesystem::path(file.toStdWString()));
    if (!loaded) {
        QMessageBox::warning(this, tr("Erreur"), QString::fromStdString(loaded.error().message));
        return;
    }

    const QImage qimg(loaded->rgba.data(), loaded->width, loaded->height,
                      loaded->width * 4, QImage::Format_RGBA8888);
    // Copie profonde : le buffer de `loaded` est détruit en sortie de fonction.
    const QPixmap pixmap = QPixmap::fromImage(qimg.copy());

    scene_->clear();
    scene_->addPixmap(pixmap);
    scene_->setSceneRect(pixmap.rect());
    view_->fitInView(scene_->sceneRect(), Qt::KeepAspectRatio);

    statusBar()->showMessage(tr("%1 — %2 × %3 px%4")
                                 .arg(QFileInfo(file).fileName())
                                 .arg(loaded->width)
                                 .arg(loaded->height)
                                 .arg(loaded->source_had_alpha ? tr(", canal alpha") : QString()));
}

}  // namespace openstitch::desktop
