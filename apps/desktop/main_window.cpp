// SPDX-License-Identifier: Apache-2.0
#include "main_window.hpp"

#include <QAction>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGridLayout>
#include <QGuiApplication>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainterPath>
#include <QPixmap>
#include <QSpinBox>
#include <QStatusBar>

#include <filesystem>
#include <numbers>

#include "brightness_dialog.hpp"
#include "canvas_view.hpp"
#include "import_dialog.hpp"
#include "node_handle.hpp"
#include "openstitch/formats/dst.hpp"
#include "openstitch/stitch_generation/generate.hpp"
#include "openstitch/vectorization/vectorize.hpp"
#include <QComboBox>
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

    connect(view_, &CanvasView::canvasClickedMm, this, &MainWindow::onCanvasClicked);

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
    exportDstAct_ = fileMenu->addAction(tr("&Exporter en DST…"));
    connect(exportDstAct_, &QAction::triggered, this, &MainWindow::exportDst);
    auto* importDstAct = fileMenu->addAction(tr("&Importer un DST…"));
    connect(importDstAct, &QAction::triggered, this, &MainWindow::importDst);
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

    auto* segMenu = menuBar()->addMenu(tr("&Segmentation"));
    auto* segAct = segMenu->addAction(tr("&Segmenter l'image…"));
    connect(segAct, &QAction::triggered, this, &MainWindow::segmentImage);
    imageActions_.append(segAct);

    showSegAct_ = segMenu->addAction(tr("&Afficher la carte des régions"));
    showSegAct_->setCheckable(true);
    connect(showSegAct_, &QAction::toggled, this, [this] { displayImage(processed_); });

    segMenu->addSeparator();
    mergeAct_ = segMenu->addAction(tr("&Fusionner avec… (cliquer la région cible)"));
    mergeAct_->setCheckable(true);
    connect(mergeAct_, &QAction::toggled, this, [this](bool on) { mergeMode_ = on; });
    regionActions_.append(mergeAct_);

    auto* delRegionAct = segMenu->addAction(tr("&Supprimer la région sélectionnée"));
    delRegionAct->setShortcut(QKeySequence::Delete);
    connect(delRegionAct, &QAction::triggered, this, &MainWindow::deleteSelectedRegion);
    regionActions_.append(delRegionAct);

    auto* recolorAct = segMenu->addAction(tr("&Recolorer la région sélectionnée…"));
    connect(recolorAct, &QAction::triggered, this, &MainWindow::recolorSelectedRegion);
    regionActions_.append(recolorAct);

    segMenu->addSeparator();
    auto* vectorizeAct = segMenu->addAction(tr("Convertir la région en objet &vectoriel"));
    connect(vectorizeAct, &QAction::triggered, this, &MainWindow::vectorizeSelectedRegion);
    regionActions_.append(vectorizeAct);

    auto* embMenu = menuBar()->addMenu(tr("&Broderie"));
    createStitchAct_ = embMenu->addAction(tr("Créer un objet de &point de contour…"));
    connect(createStitchAct_, &QAction::triggered, this, &MainWindow::createRunningStitchObject);
    createTatamiAct_ = embMenu->addAction(tr("Créer un remplissage &tatami…"));
    connect(createTatamiAct_, &QAction::triggered, this, &MainWindow::createTatamiObject);
    statsAct_ = embMenu->addAction(tr("&Statistiques…"));
    connect(statsAct_, &QAction::triggered, this, &MainWindow::showStatistics);

    auto* viewMenu = menuBar()->addMenu(tr("&Affichage"));
    showStitchesAct_ = viewMenu->addAction(tr("Afficher les &points"));
    showStitchesAct_->setCheckable(true);
    showStitchesAct_->setChecked(true);
    connect(showStitchesAct_, &QAction::toggled, this, [this] { displayImage(processed_); });
    showVectorsAct_ = viewMenu->addAction(tr("Afficher les &vecteurs"));
    showVectorsAct_->setCheckable(true);
    showVectorsAct_->setChecked(true);
    connect(showVectorsAct_, &QAction::toggled, this, [this] { displayImage(processed_); });
    viewMenu->addSeparator();
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

    project_ = document::Project{};
    project_.mm_per_px = document::mm_per_pixel(*placement, loaded->width);
    project_.original = std::move(*loaded);
    undoStack_.clear();
    sequence_.reset();
    sequenceImported_ = false;
    selectedRegion_.reset();
    selectedObject_.reset();

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
        processed_ = {};
        displayImage(processed_);  // affiche quand même une séquence importée
        return;
    }
    // Une sélection qui ne correspond plus à une région ou un objet vivant
    // est annulée (undo/redo, nouvelle segmentation, suppression…).
    if (selectedRegion_ &&
        (!project_.segmentation || project_.segmentation->find(*selectedRegion_) == nullptr)) {
        selectedRegion_.reset();
    }
    if (selectedObject_ && project_.findObject(*selectedObject_) == nullptr) {
        selectedObject_.reset();
    }
    const auto result = image::apply_pipeline(project_.original, project_.ops);
    if (!result) {
        QMessageBox::warning(this, tr("Erreur"), QString::fromStdString(result.error().message));
        return;
    }
    processed_ = *result;

    // Régénération des points depuis le document (fonction pure). Une
    // séquence importée d'un DST n'est pas régénérable : elle est conservée.
    if (!sequenceImported_) {
        sequence_.reset();
        if (!project_.embroidery_objects.empty()) {
            if (auto seq = stitch_generation::generate_sequence(project_)) {
                sequence_ = std::move(*seq);
            } else {
                statusBar()->showMessage(
                    tr("Génération des points impossible : %1")
                        .arg(QString::fromStdString(seq.error().message)));
            }
        }
    }
    displayImage(processed_);
}

void MainWindow::displayImage(const image::Image& img) {
    scene_->clear();
    if (!img.empty()) {
        const QImage qimg(img.rgba.data(), img.width, img.height, img.width * 4,
                          QImage::Format_RGBA8888);
        const QPixmap pixmap = QPixmap::fromImage(qimg.copy());
        auto* item = scene_->addPixmap(pixmap);
        const double mmPerPx = project_.mm_per_px.value;
        const double wMm = img.width * mmPerPx;
        const double hMm = img.height * mmPerPx;
        item->setTransform(QTransform::fromScale(mmPerPx, mmPerPx));
        item->setPos(-wMm / 2.0, -hMm / 2.0);
    }

    // Objets vectoriels (remplissage translucide + contour).
    if (showVectorsAct_ != nullptr && showVectorsAct_->isChecked()) {
        for (const auto& object : project_.vector_objects) {
            if (!object.visible) {
                continue;
            }
            const bool selected = selectedObject_ && object.id == *selectedObject_;
            const QColor color(object.rgb[0], object.rgb[1], object.rgb[2]);
            QPen pen(selected ? QColor(30, 90, 200) : color.darker(150));
            pen.setCosmetic(true);
            pen.setWidth(selected ? 3 : 2);
            auto* pathItem = scene_->addPath(objectPainterPath(object), pen,
                                             QBrush(QColor(color.red(), color.green(),
                                                           color.blue(), 90)));
            pathItem->setZValue(10);
        }
        // Poignées de nœuds de l'objet sélectionné.
        if (selectedObject_) {
            if (const auto* object = project_.findObject(*selectedObject_)) {
                for (std::size_t s = 0; s < object->paths.size(); ++s) {
                    const auto& set = object->paths[s];
                    const auto addHandles = [&](const geometry::Path& path, std::size_t pathIdx) {
                        for (std::size_t n = 0; n < path.nodes.size(); ++n) {
                            const Vec2um pos = path.nodes[n].pos;
                            const QPointF sceneMm(to_millimeters(pos.x).value,
                                                  -to_millimeters(pos.y).value);
                            const ObjectId objectId = object->id;
                            const document::NodeRef ref{s, pathIdx, n};
                            auto* handle = new NodeHandleItem(
                                sceneMm, [this, objectId, ref, pos](QPointF newSceneMm) {
                                    const Vec2um newPos{
                                        to_micrometers(Millimeters{newSceneMm.x()}),
                                        to_micrometers(Millimeters{-newSceneMm.y()})};
                                    if (newPos == pos) {
                                        return;
                                    }
                                    undoStack_.execute(
                                        std::make_unique<commands::MoveNodeCommand>(objectId, ref,
                                                                                    pos, newPos),
                                        project_);
                                    refreshImage();
                                    updateActions();
                                });
                            scene_->addItem(handle);
                        }
                    };
                    addHandles(set.outer, 0);
                    for (std::size_t h = 0; h < set.holes.size(); ++h) {
                        addHandles(set.holes[h], h + 1);
                    }
                }
            }
        }
    }

    // Points générés : trait continu pour la couture, pointillés pour les
    // sauts, pastilles aux pénétrations d'aiguille.
    if (showStitchesAct_ != nullptr && showStitchesAct_->isChecked() && sequence_) {
        QPainterPath sewPath;
        QPainterPath jumpPath;
        QPainterPath dots;
        bool hasPos = false;
        QPointF last;
        for (const auto& cmd : sequence_->commands) {
            const QPointF p(to_millimeters(cmd.pos.x).value, -to_millimeters(cmd.pos.y).value);
            switch (cmd.type) {
            case stitch::CommandType::Stitch:
                if (hasPos) {
                    sewPath.moveTo(last);
                    sewPath.lineTo(p);
                }
                dots.addEllipse(p, 0.15, 0.15);
                last = p;
                hasPos = true;
                break;
            case stitch::CommandType::Jump:
                if (hasPos) {
                    jumpPath.moveTo(last);
                    jumpPath.lineTo(p);
                }
                last = p;
                hasPos = true;
                break;
            default:
                break;
            }
        }
        QPen sewPen(QColor(25, 25, 45));
        sewPen.setCosmetic(true);
        sewPen.setWidth(2);
        scene_->addPath(sewPath, sewPen)->setZValue(20);

        QPen jumpPen(QColor(200, 120, 30));
        jumpPen.setCosmetic(true);
        jumpPen.setStyle(Qt::DashLine);
        scene_->addPath(jumpPath, jumpPen)->setZValue(20);

        scene_->addPath(dots, Qt::NoPen, QBrush(QColor(25, 25, 45)))->setZValue(21);
    }

    // Carte des régions par-dessus l'image (mode d'affichage segmentation).
    if (showSegAct_ != nullptr && showSegAct_->isChecked() && project_.segmentation) {
        const double mmPerPx = project_.mm_per_px.value;
        const auto map = segmentation::render_map(*project_.segmentation, selectedRegion_);
        const QImage mapImg(map.rgba.data(), map.width, map.height, map.width * 4,
                            QImage::Format_RGBA8888);
        auto* mapItem = scene_->addPixmap(QPixmap::fromImage(mapImg.copy()));
        mapItem->setTransform(QTransform::fromScale(mmPerPx, mmPerPx));
        mapItem->setPos(-map.width * mmPerPx / 2.0, -map.height * mmPerPx / 2.0);
        mapItem->setOpacity(0.9);
    }
}

std::optional<QPoint> MainWindow::mmToImagePixel(QPointF mm) const {
    if (processed_.empty()) {
        return std::nullopt;
    }
    const double mmPerPx = project_.mm_per_px.value;
    const double left = -processed_.width * mmPerPx / 2.0;
    const double top = -processed_.height * mmPerPx / 2.0;
    const int x = static_cast<int>(std::floor((mm.x() - left) / mmPerPx));
    const int y = static_cast<int>(std::floor((mm.y() - top) / mmPerPx));
    if (x < 0 || y < 0 || x >= processed_.width || y >= processed_.height) {
        return std::nullopt;
    }
    return QPoint(x, y);
}

void MainWindow::segmentImage() {
    if (!project_.hasImage() || processed_.empty()) {
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Segmenter l'image"));
    auto* layout = new QFormLayout(&dialog);
    auto* colorsSpin = new QSpinBox(&dialog);
    colorsSpin->setRange(2, 64);
    colorsSpin->setValue(8);
    auto* minSizeSpin = new QSpinBox(&dialog);
    minSizeSpin->setRange(1, 100'000);
    minSizeSpin->setValue(16);
    minSizeSpin->setSuffix(tr(" px"));
    layout->addRow(tr("Nombre maximal de couleurs :"), colorsSpin);
    layout->addRow(tr("Taille minimale de région :"), minSizeSpin);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    // Calcul synchrone (curseur d'attente) : le passage en tâche de fond est
    // prévu quand les images de travail deviendront grandes.
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    auto seg = segmentation::segment(
        processed_, {.max_colors = colorsSpin->value(), .min_region_px = minSizeSpin->value()});
    QGuiApplication::restoreOverrideCursor();
    if (!seg) {
        QMessageBox::warning(this, tr("Segmentation impossible"),
                             QString::fromStdString(seg.error().message));
        return;
    }
    const auto regionCount = seg->region_count();
    undoStack_.execute(std::make_unique<commands::SetSegmentationCommand>(std::move(*seg)),
                       project_);
    selectedRegion_.reset();
    showSegAct_->setChecked(true);
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("Segmentation : %1 régions").arg(regionCount));
}

QPainterPath MainWindow::objectPainterPath(const document::VectorObject& object) {
    QPainterPath painterPath;
    painterPath.setFillRule(Qt::OddEvenFill);
    const auto addPath = [&painterPath](const geometry::Path& path) {
        if (path.nodes.empty()) {
            return;
        }
        // Scène en mm, Y vers le bas : inversion du repère physique.
        painterPath.moveTo(to_millimeters(path.nodes[0].pos.x).value,
                           -to_millimeters(path.nodes[0].pos.y).value);
        for (std::size_t i = 1; i < path.nodes.size(); ++i) {
            painterPath.lineTo(to_millimeters(path.nodes[i].pos.x).value,
                               -to_millimeters(path.nodes[i].pos.y).value);
        }
        painterPath.closeSubpath();
    };
    for (const auto& set : object.paths) {
        addPath(set.outer);
        for (const auto& hole : set.holes) {
            addPath(hole);
        }
    }
    return painterPath;
}

void MainWindow::vectorizeSelectedRegion() {
    if (!selectedRegion_ || !project_.segmentation) {
        return;
    }
    const auto* region = project_.segmentation->find(*selectedRegion_);
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    auto sets = vectorization::vectorize_region(
        *project_.segmentation, *selectedRegion_,
        {.mm_per_px = project_.mm_per_px, .simplify_tolerance = Micrometers{200}});
    QGuiApplication::restoreOverrideCursor();
    if (!sets) {
        QMessageBox::warning(this, tr("Vectorisation impossible"),
                             QString::fromStdString(sets.error().message));
        return;
    }

    document::VectorObject object;
    object.id = project_.object_ids.next();
    object.name = tr("Région %1").arg(selectedRegion_->value).toStdString();
    object.source_region = *selectedRegion_;
    object.rgb = region->rgb;
    object.paths = std::move(*sets);

    undoStack_.execute(std::make_unique<commands::AddVectorObjectCommand>(std::move(object)),
                       project_);
    selectedObject_ = project_.vector_objects.back().id;
    showVectorsAct_->setChecked(true);
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("Objet vectoriel créé — cliquez-le pour éditer ses nœuds"));
}

void MainWindow::createRunningStitchObject() {
    if (!selectedObject_) {
        return;
    }
    const auto* source = project_.findObject(*selectedObject_);
    if (source == nullptr) {
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Objet de point de contour"));
    auto* layout = new QFormLayout(&dialog);
    auto* lengthSpin = new QDoubleSpinBox(&dialog);
    lengthSpin->setRange(0.5, 12.0);
    lengthSpin->setValue(3.0);
    lengthSpin->setDecimals(1);
    lengthSpin->setSuffix(tr(" mm"));
    auto* typeCombo = new QComboBox(&dialog);
    typeCombo->addItem(tr("Point simple"), 1);
    typeCombo->addItem(tr("Aller-retour (double)"), 2);
    typeCombo->addItem(tr("Point triple"), 3);
    layout->addRow(tr("Longueur de point :"), lengthSpin);
    layout->addRow(tr("Type :"), typeCombo);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    document::RunningStitchParams params;
    params.stitch_length = to_micrometers(Millimeters{lengthSpin->value()});
    params.repeats = typeCombo->currentData().toInt();

    document::EmbroideryObject object;
    object.id = project_.object_ids.next();
    object.name = tr("Contour de %1").arg(QString::fromStdString(source->name)).toStdString();
    object.source_vector = source->id;
    object.rgb = source->rgb;
    object.params = params;

    undoStack_.execute(std::make_unique<commands::AddEmbroideryObjectCommand>(std::move(object)),
                       project_);
    showStitchesAct_->setChecked(true);
    refreshImage();
    updateActions();
    if (sequence_) {
        const auto stats = stitch::compute_stats(*sequence_);
        statusBar()->showMessage(tr("Points générés : %1 points, %2 saut(s)")
                                     .arg(stats.stitches)
                                     .arg(stats.jumps));
    }
}

void MainWindow::createTatamiObject() {
    if (!selectedObject_) {
        return;
    }
    const auto* source = project_.findObject(*selectedObject_);
    if (source == nullptr) {
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Remplissage tatami"));
    auto* layout = new QFormLayout(&dialog);
    auto* densitySpin = new QDoubleSpinBox(&dialog);
    densitySpin->setRange(0.1, 2.0);
    densitySpin->setValue(0.4);
    densitySpin->setDecimals(2);
    densitySpin->setSuffix(tr(" mm"));
    auto* lengthSpin = new QDoubleSpinBox(&dialog);
    lengthSpin->setRange(1.0, 8.0);
    lengthSpin->setValue(3.0);
    lengthSpin->setDecimals(1);
    lengthSpin->setSuffix(tr(" mm"));
    auto* angleSpin = new QSpinBox(&dialog);
    angleSpin->setRange(0, 179);
    angleSpin->setValue(0);
    angleSpin->setSuffix(tr(" °"));
    layout->addRow(tr("Espacement des rangées :"), densitySpin);
    layout->addRow(tr("Longueur de point :"), lengthSpin);
    layout->addRow(tr("Angle :"), angleSpin);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    document::TatamiParams params;
    params.row_spacing = to_micrometers(Millimeters{densitySpin->value()});
    params.stitch_length = to_micrometers(Millimeters{lengthSpin->value()});
    params.angle = Angle{angleSpin->value() * std::numbers::pi / 180.0};

    document::EmbroideryObject object;
    object.id = project_.object_ids.next();
    object.name = tr("Remplissage de %1").arg(QString::fromStdString(source->name)).toStdString();
    object.source_vector = source->id;
    object.rgb = source->rgb;
    object.params = params;

    undoStack_.execute(std::make_unique<commands::AddEmbroideryObjectCommand>(std::move(object)),
                       project_);
    showStitchesAct_->setChecked(true);
    refreshImage();
    updateActions();
    if (sequence_) {
        const auto stats = stitch::compute_stats(*sequence_);
        statusBar()->showMessage(tr("Remplissage généré : %1 points").arg(stats.stitches));
    }
}

void MainWindow::showStatistics() {
    if (!sequence_) {
        return;
    }
    const auto stats = stitch::compute_stats(*sequence_);
    const double wMm = to_millimeters(stats.bounds.max.x - stats.bounds.min.x).value;
    const double hMm = to_millimeters(stats.bounds.max.y - stats.bounds.min.y).value;
    QMessageBox::information(
        this, tr("Statistiques de broderie"),
        tr("Points : %1\nSauts : %2\nCoupes : %3\nChangements de couleur : %4\n"
           "Dimensions : %5 × %6 mm\nFil cousu estimé : %7 m")
            .arg(stats.stitches)
            .arg(stats.jumps)
            .arg(stats.trims)
            .arg(stats.color_changes)
            .arg(wMm, 0, 'f', 1)
            .arg(hMm, 0, 'f', 1)
            .arg(stats.thread_length_um / 1e9, 0, 'f', 2));
}

void MainWindow::exportDst() {
    if (!sequence_) {
        return;
    }
    const QString file = QFileDialog::getSaveFileName(this, tr("Exporter en DST"), QString(),
                                                      tr("Broderie Tajima (*.dst)"));
    if (file.isEmpty()) {
        return;
    }
    // Rappel honnête (§17) : le DST ne conserve ni objets ni couleurs réelles.
    const auto written = formats::write_dst_file(std::filesystem::path(file.toStdWString()),
                                                 *sequence_);
    if (!written) {
        QMessageBox::warning(this, tr("Export impossible"),
                             QString::fromStdString(written.error().message));
        return;
    }
    const auto stats = stitch::compute_stats(*sequence_);
    statusBar()->showMessage(
        tr("DST exporté : %1 (%2 points). Le DST ne conserve pas les objets éditables — "
           "gardez aussi le projet.")
            .arg(QFileInfo(file).fileName())
            .arg(stats.stitches));
}

void MainWindow::importDst() {
    const QString file = QFileDialog::getOpenFileName(this, tr("Importer un DST"), QString(),
                                                      tr("Broderie Tajima (*.dst)"));
    if (file.isEmpty()) {
        return;
    }
    if (project_.hasImage() || sequence_) {
        const auto answer = QMessageBox::question(
            this, tr("Importer un DST"),
            tr("L'import remplace le document en cours. Continuer ?"));
        if (answer != QMessageBox::Yes) {
            return;
        }
    }
    auto seq = formats::read_dst_file(std::filesystem::path(file.toStdWString()));
    if (!seq) {
        QMessageBox::warning(this, tr("Import impossible"),
                             QString::fromStdString(seq.error().message));
        return;
    }

    project_ = document::Project{};
    undoStack_.clear();
    processed_ = {};
    selectedRegion_.reset();
    selectedObject_.reset();
    sequence_ = std::move(*seq);
    sequenceImported_ = true;
    showStitchesAct_->setChecked(true);
    displayImage(processed_);
    view_->fitCanvas();
    updateActions();

    const auto stats = stitch::compute_stats(*sequence_);
    statusBar()->showMessage(tr("%1 — %2 points, %3 saut(s), %4 changement(s) de fil")
                                 .arg(QFileInfo(file).fileName())
                                 .arg(stats.stitches)
                                 .arg(stats.jumps)
                                 .arg(stats.color_changes));
}

void MainWindow::onCanvasClicked(QPointF posMm) {
    // Priorité aux objets vectoriels lorsqu'ils sont affichés (hors fusion).
    if (showVectorsAct_->isChecked() && !mergeMode_) {
        std::optional<ObjectId> hit;
        for (const auto& object : project_.vector_objects) {
            if (object.visible && objectPainterPath(object).contains(posMm)) {
                hit = object.id;  // le dernier dessiné (au-dessus) gagne
            }
        }
        if (hit) {
            selectedObject_ = hit;
            selectedRegion_.reset();
            const auto* object = project_.findObject(*hit);
            statusBar()->showMessage(tr("Objet « %1 » — %2 morceau(x), nœuds déplaçables")
                                         .arg(QString::fromStdString(object->name))
                                         .arg(object->paths.size()));
            displayImage(processed_);
            updateActions();
            return;
        }
        if (selectedObject_) {
            selectedObject_.reset();
            displayImage(processed_);
        }
    }

    if (!showSegAct_->isChecked() || !project_.segmentation) {
        return;
    }
    const auto px = mmToImagePixel(posMm);
    const auto clicked =
        px ? segmentation::region_at(*project_.segmentation, px->x(), px->y()) : std::nullopt;

    if (mergeMode_ && selectedRegion_ && clicked && *clicked != *selectedRegion_) {
        undoStack_.execute(
            std::make_unique<commands::MergeRegionsCommand>(*selectedRegion_, *clicked), project_);
        mergeAct_->setChecked(false);
        refreshImage();
        updateActions();
        return;
    }

    selectedRegion_ = clicked;
    if (clicked) {
        const auto* region = project_.segmentation->find(*clicked);
        const double mm2 = static_cast<double>(region->pixel_count) * project_.mm_per_px.value *
                           project_.mm_per_px.value;
        statusBar()->showMessage(tr("Région %1 — %2 px (%3 mm²) — RGB(%4, %5, %6)")
                                     .arg(region->id.value)
                                     .arg(region->pixel_count)
                                     .arg(mm2, 0, 'f', 1)
                                     .arg(region->rgb[0])
                                     .arg(region->rgb[1])
                                     .arg(region->rgb[2]));
    }
    displayImage(processed_);
    updateActions();
}

void MainWindow::deleteSelectedRegion() {
    if (!selectedRegion_ || !project_.segmentation) {
        return;
    }
    undoStack_.execute(std::make_unique<commands::RemoveRegionCommand>(*selectedRegion_), project_);
    selectedRegion_.reset();
    refreshImage();
    updateActions();
}

void MainWindow::recolorSelectedRegion() {
    if (!selectedRegion_ || !project_.segmentation) {
        return;
    }
    const auto* region = project_.segmentation->find(*selectedRegion_);
    const QColor initial(region->rgb[0], region->rgb[1], region->rgb[2]);
    const QColor color = QColorDialog::getColor(initial, this, tr("Couleur de la région"));
    if (!color.isValid()) {
        return;
    }
    undoStack_.execute(
        std::make_unique<commands::RecolorRegionCommand>(
            *selectedRegion_, std::array<std::uint8_t, 3>{static_cast<std::uint8_t>(color.red()),
                                                          static_cast<std::uint8_t>(color.green()),
                                                          static_cast<std::uint8_t>(color.blue())}),
        project_);
    refreshImage();
    updateActions();
}

void MainWindow::updateActions() {
    const bool hasImage = project_.hasImage();
    for (QAction* act : imageActions_) {
        act->setEnabled(hasImage);
    }
    showSegAct_->setEnabled(project_.segmentation.has_value());
    showVectorsAct_->setEnabled(!project_.vector_objects.empty());
    showStitchesAct_->setEnabled(!project_.embroidery_objects.empty());
    createStitchAct_->setEnabled(selectedObject_.has_value());
    createTatamiAct_->setEnabled(selectedObject_.has_value());
    statsAct_->setEnabled(sequence_.has_value());
    exportDstAct_->setEnabled(sequence_.has_value());
    const bool hasSelection = selectedRegion_.has_value() && project_.segmentation.has_value();
    for (QAction* act : regionActions_) {
        act->setEnabled(hasSelection);
    }
    if (!mergeAct_->isEnabled()) {
        mergeAct_->setChecked(false);
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
