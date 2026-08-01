// SPDX-License-Identifier: Apache-2.0
#include "main_window.hpp"

#include <QAction>
#include <QActionGroup>
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
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainterPath>
#include <QPixmap>
#include <QSpinBox>
#include <QStatusBar>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <numbers>
#include <unordered_map>
#include <variant>

#include "app_theme.hpp"
#include "brightness_dialog.hpp"
#include "canvas_view.hpp"
#include "document_panel.hpp"
#include "empty_state_widget.hpp"
#include "import_dialog.hpp"
#include <QCloseEvent>
#include "node_handle.hpp"
#include "satin_guide_item.hpp"
#include "properties_panel.hpp"
#include "ui_icons.hpp"
#include "workflow_panel.hpp"
#include <QSettings>
#include <QShortcut>
#include <QToolBar>
#include <QToolButton>
#include "openstitch/auto_satin/satin_column.hpp"
#include "openstitch/autodigitize/autodigitize.hpp"
#include "openstitch/formats/dst.hpp"
#include "openstitch/optimization/order.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/stitch_analysis/analyze.hpp"
#include "openstitch/stitch_generation/generate.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "openstitch/stitch_generation/satin_guides.hpp"
#include "openstitch/stitch_generation/satin.hpp"
#include "openstitch/vectorization/vectorize.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QIcon>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/core/app_info.hpp"
#include "openstitch/document/canvas.hpp"
#include "openstitch/document/image_placement.hpp"
#include "openstitch/image/image.hpp"
#include "ruler.hpp"

namespace openstitch::desktop {

// Mode d'édition des points générés (Lot 8.2) : au-delà de ce nombre de
// points déplaçables (is_movable_point), aucune poignée n'est proposée --
// un GraphicsItem par poignée coûte trop cher sur un motif dense. Partagé
// entre renderBase (rendu) et updateActions (gating/sortie de mode + message).
constexpr std::size_t kMaxEditableStitchHandles = 2000;

MainWindow::MainWindow() {
    setWindowTitle(QStringLiteral("%1 [*]").arg(QString::fromUtf8(kAppName)));
    resize(1100, 800);

    scene_ = new QGraphicsScene(this);
    view_ = new CanvasView(scene_, this);

    applyCanvasToView();

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
    buildPropertiesPanel();
    buildAnalysisPanel();
    buildOrderPanel();
    buildDocumentPanel();
    buildWorkflowPanel();
    buildFilterPanel();
    buildMainToolbar();
    addToolBarBreak();  // la barre contextuelle sur sa propre rangée
    buildContextToolbar();
    buildToolPalette();
    addToolBarBreak();  // la barre de simulation occupe sa propre rangée
    buildSimulationToolbar();

    buildHelpMenu();

    // Noms accessibles (lecteurs d'écran) sur les grandes zones.
    view_->setAccessibleName(tr("Canevas"));
    view_->setAccessibleDescription(tr("Zone de travail du motif, en millimètres."));
    for (auto* d : {documentDock_, propertiesDock_, workflowDock_, orderDock_, filterDock_,
                    analysisDock_}) {
        if (d != nullptr) {
            d->setAccessibleName(d->windowTitle());
        }
    }

    // Restaure la disposition de l'interface (préférences UI, pas de données
    // métier — celles-ci restent dans le .osp). Les panneaux vides seront
    // masqués ensuite par les refresh.
    {
        // QSettings() par défaut : lit l'organisation/application déjà posées sur
        // QCoreApplication (main.cpp). Un test peut ainsi rediriger vers un fichier
        // temporaire (QCoreApplication::setOrganizationName + QSettings::setPath)
        // sans jamais toucher au registre réel de l'utilisateur.
        QSettings s;
        if (s.contains(QStringLiteral("ui/geometry"))) {
            restoreGeometry(s.value(QStringLiteral("ui/geometry")).toByteArray());
        }
        if (s.contains(QStringLiteral("ui/windowState"))) {
            restoreState(s.value(QStringLiteral("ui/windowState")).toByteArray());
        }
    }

    toolLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(toolLabel_);
    cursorLabel_ = new QLabel(this);
    cursorLabel_->setMinimumWidth(180);
    statusBar()->addPermanentWidget(cursorLabel_);
    setTool(Tool::Select);
    connect(view_, &CanvasView::cursorMovedMm, this, [this](QPointF mm) {
        cursorLabel_->setText(
            tr("x : %1 mm   y : %2 mm").arg(mm.x(), 0, 'f', 1).arg(mm.y(), 0, 'f', 1));
    });
    connect(view_, &CanvasView::cropSelectedMm, this, &MainWindow::onCropSelected);

    connect(view_, &CanvasView::canvasClickedMm, this, &MainWindow::onCanvasClicked);
    connect(view_, &CanvasView::canvasContextMenu, this, &MainWindow::onCanvasContextMenu);
    // Un changement de thème redessine les couches (couleurs des points, repères).
    connect(&AppTheme::instance(), &AppTheme::changed, this, [this] { displayImage(processed_); });

    // État d'accueil, flottant au centre du canevas tant qu'aucun document.
    emptyState_ = new EmptyStateWidget(view_);
    connect(emptyState_, &EmptyStateWidget::openImageRequested, this, &MainWindow::openImage);
    connect(emptyState_, &EmptyStateWidget::openProjectRequested, this, &MainWindow::loadProject);
    connect(emptyState_, &EmptyStateWidget::importDstRequested, this, &MainWindow::importDst);
    connect(view_, &CanvasView::viewChanged, this, &MainWindow::positionEmptyState);

    statusBar()->showMessage(tr("Ouvrez une image (PNG, JPEG, BMP, TIFF) pour commencer."));
    view_->fitCanvas();
    updateActions();
}

MainWindow::~MainWindow() {
    // Les actions et la scène sont des enfants QObject détruits après la
    // partie MainWindow. Aucune de leurs notifications tardives ne doit
    // rappeler un slot du type dérivé dont le destructeur a déjà commencé.
    for (QObject* child : findChildren<QObject*>()) {
        QObject::disconnect(child, nullptr, this, nullptr);
    }
}

void MainWindow::buildMenus() {
    auto* fileMenu = menuBar()->addMenu(tr("&Fichier"));
    auto* openAct = fileMenu->addAction(tr("&Ouvrir une image…"));
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MainWindow::openImage);
    fileMenu->addSeparator();
    auto* saveProjectAct = fileMenu->addAction(tr("&Enregistrer le projet…"));
    saveProjectAct->setShortcut(QKeySequence::Save);
    connect(saveProjectAct, &QAction::triggered, this, &MainWindow::saveProject);
    auto* loadProjectAct = fileMenu->addAction(tr("Ou&vrir un projet…"));
    connect(loadProjectAct, &QAction::triggered, this, &MainWindow::loadProject);
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
    undoAct_->setObjectName(QStringLiteral("action_undo"));
    undoAct_->setShortcut(QKeySequence::Undo);
    connect(undoAct_, &QAction::triggered, this, &MainWindow::undo);
    redoAct_ = editMenu->addAction(tr("&Rétablir"));
    redoAct_->setObjectName(QStringLiteral("action_redo"));
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
    connect(cropAct_, &QAction::toggled, this,
            [this](bool on) { setTool(on ? Tool::Rect : Tool::Select); });
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
    delRegionAct->setObjectName(QStringLiteral("action_deleteRegion"));
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
    auto* autoAct = embMenu->addAction(tr("Numérisation &automatique"));
    connect(autoAct, &QAction::triggered, this, &MainWindow::autoDigitize);
    embMenu->addSeparator();
    createStitchAct_ = embMenu->addAction(tr("Créer un objet de &point de contour…"));
    createStitchAct_->setObjectName(QStringLiteral("action_createStitch"));
    connect(createStitchAct_, &QAction::triggered, this, &MainWindow::createRunningStitchObject);
    createTatamiAct_ = embMenu->addAction(tr("Créer un remplissage &tatami…"));
    connect(createTatamiAct_, &QAction::triggered, this, &MainWindow::createTatamiObject);
    createSatinAct_ = embMenu->addAction(tr("Créer une colonne &satin…"));
    connect(createSatinAct_, &QAction::triggered, this, &MainWindow::createSatinObject);
    autoSatinAct_ = embMenu->addAction(tr("Convertir automatiquement en satin…"));
    autoSatinAct_->setToolTip(
        tr("Construit des colonnes satin (rails + barreaux) depuis le squelette de la forme."));
    connect(autoSatinAct_, &QAction::triggered, this, &MainWindow::autoConvertToSatin);
    embMenu->addSeparator();
    fillAngleAct_ = embMenu->addAction(tr("&Orientation du remplissage…"));
    fillAngleAct_->setToolTip(
        tr("Change l'angle des fils du remplissage tatami sélectionné (clic sur la forme "
           "ou dans l'ordre de couture)."));
    connect(fillAngleAct_, &QAction::triggered, this, &MainWindow::changeFillAngle);
    convertSatinAct_ = embMenu->addAction(tr("Convertir les satins auto en &tatami"));
    convertSatinAct_->setToolTip(
        tr("Remplace les colonnes satin automatiques (qui débordent sur les formes "
           "concaves) par des remplissages tatami découpés sur la région."));
    connect(convertSatinAct_, &QAction::triggered, this, &MainWindow::convertSatinsToTatami);
    embMenu->addSeparator();
    // Mode d'édition des points générés (Lot 8.2). Mode exclusif : activable
    // seulement dans un contexte valide (objet de broderie sélectionné, non
    // Dirty -- cf. updateActions), désactivé/décoché sinon.
    stitchEditModeAct_ = embMenu->addAction(icons::editPoints(), tr("&Éditer les points…"));
    stitchEditModeAct_->setObjectName(QStringLiteral("action_stitchEditMode"));
    stitchEditModeAct_->setCheckable(true);
    stitchEditModeAct_->setShortcut(QKeySequence(Qt::Key_E));
    stitchEditModeAct_->setToolTip(
        tr("Déplacer un à un les points de couture générés de l'objet sélectionné (E). "
           "Distinct de l'édition des nœuds vectoriels : ici, seul le point cousu bouge, "
           "pas la forme source."));
    connect(stitchEditModeAct_, &QAction::toggled, this, &MainWindow::onStitchEditModeToggled);
    satinGuideModeAct_ = embMenu->addAction(tr("Éditer les &guides satin…"));
    satinGuideModeAct_->setObjectName(QStringLiteral("action_satinGuideMode"));
    satinGuideModeAct_->setCheckable(true);
    satinGuideModeAct_->setShortcut(QKeySequence(Qt::Key_G));
    satinGuideModeAct_->setToolTip(
        tr("Afficher les guides transversaux du satin et glisser leurs extrémités le long "
           "des rails (G). Plusieurs guides peuvent orienter une même colonne."));
    connect(satinGuideModeAct_, &QAction::toggled, this, &MainWindow::onSatinGuideModeToggled);
    addSatinGuideAct_ = embMenu->addAction(tr("Ajouter un guide satin"));
    addSatinGuideAct_->setObjectName(QStringLiteral("action_addSatinGuide"));
    addSatinGuideAct_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_G));
    connect(addSatinGuideAct_, &QAction::triggered, this, &MainWindow::addSatinGuide);
    removeSatinGuideAct_ = embMenu->addAction(tr("Supprimer le guide satin sélectionné"));
    removeSatinGuideAct_->setObjectName(QStringLiteral("action_removeSatinGuide"));
    connect(removeSatinGuideAct_, &QAction::triggered, this,
            &MainWindow::removeSelectedSatinGuide);
    embMenu->addSeparator();
    statsAct_ = embMenu->addAction(tr("&Statistiques…"));
    connect(statsAct_, &QAction::triggered, this, &MainWindow::showStatistics);

    auto* viewMenu = menuBar()->addMenu(tr("&Affichage"));
    auto* layersMenu = viewMenu->addMenu(tr("&Calques"));
    showImageAct_ = layersMenu->addAction(tr("&Image"));
    showImageAct_->setCheckable(true);
    showImageAct_->setChecked(true);
    connect(showImageAct_, &QAction::toggled, this, [this] { displayImage(processed_); });
    // La carte des régions (segmentation) : action partagée avec le menu Segmentation.
    layersMenu->addAction(showSegAct_);
    showVectorsAct_ = layersMenu->addAction(tr("&Vecteurs"));
    showVectorsAct_->setCheckable(true);
    showVectorsAct_->setChecked(true);
    connect(showVectorsAct_, &QAction::toggled, this, [this] { displayImage(processed_); });
    showStitchesAct_ = layersMenu->addAction(tr("&Broderie (points)"));
    showStitchesAct_->setCheckable(true);
    showStitchesAct_->setChecked(true);
    connect(showStitchesAct_, &QAction::toggled, this, [this] { displayImage(processed_); });
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

    viewMenu->addSeparator();
    auto* hoopAct = viewMenu->addAction(tr("Taille du &cadre…"));
    hoopAct->setToolTip(tr("Définit la zone physique de broderie (cadre)."));
    connect(hoopAct, &QAction::triggered, this, &MainWindow::setHoopSize);

    viewMenu->addSeparator();
    auto* themeMenu = viewMenu->addMenu(tr("&Thème"));
    auto* themeGroup = new QActionGroup(this);
    const auto addThemeAct = [&](const QString& label, ThemeMode m) {
        auto* act = themeMenu->addAction(label);
        act->setCheckable(true);
        act->setChecked(AppTheme::instance().mode() == m);
        themeGroup->addAction(act);
        connect(act, &QAction::triggered, this, [m] { AppTheme::instance().setMode(m); });
    };
    addThemeAct(tr("Clair"), ThemeMode::Light);
    addThemeAct(tr("Sombre"), ThemeMode::Dark);

    auto* densityMenu = viewMenu->addMenu(tr("&Densité"));
    auto* densityGroup = new QActionGroup(this);
    const auto addDensityAct = [&](const QString& label, Density d) {
        auto* act = densityMenu->addAction(label);
        act->setCheckable(true);
        act->setChecked(AppTheme::instance().density() == d);
        densityGroup->addAction(act);
        connect(act, &QAction::triggered, this, [d] { AppTheme::instance().setDensity(d); });
    };
    addDensityAct(tr("Confortable"), Density::Comfortable);
    addDensityAct(tr("Compact"), Density::Compact);

    viewMenu->addSeparator();
    auto* hidePanelsAct = viewMenu->addAction(tr("&Masquer les panneaux"));
    hidePanelsAct->setCheckable(true);
    hidePanelsAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P));
    hidePanelsAct->setToolTip(tr("Mode canevas : masque tous les panneaux."));
    connect(hidePanelsAct, &QAction::toggled, this, [this](bool hide) {
        const std::vector<QDockWidget*> docks{documentDock_,  propertiesDock_, workflowDock_,
                                              orderDock_,     filterDock_,     analysisDock_};
        if (hide) {
            panelsToRestore_.clear();
            for (auto* d : docks) {
                if (d != nullptr && d->isVisible()) {
                    panelsToRestore_.push_back(d);
                    d->hide();
                }
            }
        } else {
            for (auto* d : panelsToRestore_) {
                if (d != nullptr) {
                    d->show();
                }
            }
            panelsToRestore_.clear();
        }
    });
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

    const QImage preview(loaded->rgba.data(), loaded->width, loaded->height, loaded->width * 4,
                         QImage::Format_RGBA8888);
    const QSizeF hoop(to_millimeters(project_.canvas.width).value,
                      to_millimeters(project_.canvas.height).value);
    ImportDialog dialog(loaded->width, loaded->height, preview.copy(), hoop, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const auto placement = dialog.placement();
    if (!placement) {
        QMessageBox::warning(this, tr("Erreur"), tr("Taille physique invalide."));
        return;
    }

    project_ = document::Project{};
    ++documentGeneration_;  // nouveau document : invalide les commandes différées en vol
    project_.mm_per_px = document::mm_per_pixel(*placement, loaded->width);
    project_.original = std::move(*loaded);
    undoStack_.clear();
    sequence_.reset();
    sequenceImported_ = false;
    selectedRegion_.reset();
    selectedObject_.reset();
    // Nouveau document : sortie propre du mode d'édition des points (Lot 8.2),
    // sans passer par onStitchEditModeToggled (project_ n'est pas encore
    // rafraîchi -- displayImage() y serait prématuré, refreshImage() ci-dessous
    // s'en charge).
    if (stitchEditModeAct_ != nullptr) {
        QSignalBlocker block(stitchEditModeAct_);
        stitchEditModeAct_->setChecked(false);
    }
    stitchEditTarget_.reset();
    stitchEditView_.reset();

    refreshImage();
    view_->fitCanvas();
    statusBar()->showMessage(tr("%1 — %2 × %3 px")
                                 .arg(QFileInfo(file).fileName())
                                 .arg(project_.original.width)
                                 .arg(project_.original.height));
    updateActions();
    setWindowModified(false);  // nouveau document propre
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

// Entrée/sortie du mode d'édition des points (Lot 8.2). L'action n'est
// activable (cf. updateActions -> stitchEditModeAct_->setEnabled) que pour un
// objet de broderie sélectionné et non Dirty ; ce garde-fou reste néanmoins
// revérifié ici (défense en profondeur : un déclenchement programmatique ou
// une course sélection/activation ne doit jamais laisser le mode actif sans
// cible valide).
void MainWindow::onStitchEditModeToggled(bool on) {
    stitchEditTarget_.reset();
    stitchEditView_.reset();
    if (on) {
        if (satinGuideModeAct_ != nullptr && satinGuideModeAct_->isChecked()) {
            satinGuideModeAct_->setChecked(false);
        }
        if (const auto* emb = resolveSelectedEmbroidery()) {
            if (auto view = stitch_generation::edit_view(project_, emb->id);
                view && view->state != stitch_generation::ObjectEditState::Dirty) {
                stitchEditTarget_ = emb->id;
                stitchEditView_ = std::move(*view);
            }
        }
        if (!stitchEditTarget_) {
            // Contexte devenu invalide entre le clic et ce traitement (perte de
            // sélection, objet Dirty) : on annule silencieusement l'activation.
            QSignalBlocker block(stitchEditModeAct_);
            stitchEditModeAct_->setChecked(false);
        } else {
            statusBar()->showMessage(
                tr("Édition des points : glissez un point pour le déplacer. Échap pour quitter."));
        }
    }
    displayImage(processed_);  // fait apparaître/disparaître les poignées
    updateActions();
}

void MainWindow::onSatinGuideModeToggled(bool on) {
    satinGuideTarget_.reset();
    selectedSatinGuide_.reset();
    if (on) {
        if (stitchEditModeAct_ != nullptr && stitchEditModeAct_->isChecked()) {
            stitchEditModeAct_->setChecked(false);
        }
        if (const auto* emb = resolveSelectedEmbroidery()) {
            if (const auto* satin = std::get_if<document::SatinParams>(&emb->params);
                satin != nullptr && satin->rungs.size() >= 2) {
                satinGuideTarget_ = emb->id;
            }
        }
        if (!satinGuideTarget_) {
            QSignalBlocker block(satinGuideModeAct_);
            satinGuideModeAct_->setChecked(false);
        } else {
            statusBar()->showMessage(
                tr("Guides satin : glissez une extrémité le long de son rail. Échap pour quitter."));
        }
    }
    displayImage(processed_);
    updateActions();
}

void MainWindow::addSatinGuide() {
    if (!satinGuideTarget_) {
        return;
    }
    auto* obj = project_.findEmbroidery(*satinGuideTarget_);
    auto* satin = obj != nullptr ? std::get_if<document::SatinParams>(&obj->params) : nullptr;
    if (satin == nullptr) {
        return;
    }
    const auto insertion = stitch_generation::make_satin_guide_in_largest_gap(*satin);
    if (!insertion) {
        statusBar()->showMessage(
            tr("Impossible d'ajouter un guide : aucun intervalle assez large."));
        return;
    }
    const std::size_t selected = insertion->index;
    undoStack_.execute(std::make_unique<commands::AddSatinGuideCommand>(
                           obj->id, insertion->guide, insertion->index),
                       project_);
    selectedSatinGuide_ = selected;
    refreshImage();
    updateActions();
}

void MainWindow::removeSelectedSatinGuide() {
    if (!satinGuideTarget_ || !selectedSatinGuide_) {
        return;
    }
    auto* obj = project_.findEmbroidery(*satinGuideTarget_);
    auto* satin = obj != nullptr ? std::get_if<document::SatinParams>(&obj->params) : nullptr;
    if (satin == nullptr || satin->rungs.size() <= 2 || *selectedSatinGuide_ >= satin->rungs.size()) {
        return;
    }
    undoStack_.execute(std::make_unique<commands::RemoveSatinGuideCommand>(
                           obj->id, *selectedSatinGuide_),
                       project_);
    selectedSatinGuide_.reset();
    refreshImage();
    updateActions();
}

void MainWindow::discardOverrides(ObjectId id) {
    const auto* obj = project_.findEmbroidery(id);
    if (obj == nullptr) {
        return;
    }
    const auto answer = QMessageBox::question(
        this, tr("Abandonner les retouches"),
        tr("Abandonner définitivement les retouches manuelles de « %1 » ? Les points "
           "reviendront à la géométrie générée. Cette action reste annulable (Ctrl+Z).")
            .arg(QString::fromStdString(obj->name)));
    if (answer != QMessageBox::Yes) {
        return;
    }
    undoStack_.execute(std::make_unique<commands::DiscardOverridesCommand>(id), project_);
    refreshImage();
    updateActions();
}

void MainWindow::applyCanvasToView() {
    const QSizeF want(to_millimeters(project_.canvas.width).value,
                      to_millimeters(project_.canvas.height).value);
    if (view_->canvasSizeMm() != want) {
        view_->setCanvasSizeMm(want);
    }
}

void MainWindow::positionEmptyState() {
    if (emptyState_ == nullptr || !emptyState_->isVisible()) {
        return;
    }
    emptyState_->adjustSize();
    const QSize s = emptyState_->size();
    const QRect vr = view_->rect();
    emptyState_->move(vr.center().x() - s.width() / 2, vr.center().y() - s.height() / 2);
}

void MainWindow::updateEmptyState() {
    if (emptyState_ == nullptr) {
        return;
    }
    emptyState_->setVisible(!project_.hasImage());
    positionEmptyState();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // Sauve les préférences d'interface (géométrie + disposition des panneaux).
    {
        QSettings s;
        s.setValue(QStringLiteral("ui/geometry"), saveGeometry());
        s.setValue(QStringLiteral("ui/windowState"), saveState());
    }
    if (!isWindowModified()) {
        event->accept();
        return;
    }
    const auto answer = QMessageBox::warning(
        this, tr("Modifications non enregistrées"),
        tr("Le projet a été modifié. Enregistrer avant de quitter ?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Save) {
        saveProject();
        event->setAccepted(!isWindowModified());  // annulé dans le dialogue -> reste ouvert
    } else if (answer == QMessageBox::Discard) {
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::setHoopSize() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Taille du cadre de broderie"));
    auto* layout = new QFormLayout(&dialog);
    layout->addRow(new QLabel(
        tr("Zone physique dans laquelle le motif doit tenir.\n"
           "L'analyse signale les points hors de ce cadre."),
        &dialog));
    const auto makeSpin = [&](double valueMm) {
        auto* spin = new QDoubleSpinBox(&dialog);
        spin->setRange(10.0, 500.0);
        spin->setDecimals(1);
        spin->setSuffix(tr(" mm"));
        spin->setValue(valueMm);
        return spin;
    };
    auto* w = makeSpin(to_millimeters(project_.canvas.width).value);
    auto* h = makeSpin(to_millimeters(project_.canvas.height).value);
    layout->addRow(tr("Largeur :"), w);
    layout->addRow(tr("Hauteur :"), h);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    document::Canvas c;
    c.width = to_micrometers(Millimeters{w->value()});
    c.height = to_micrometers(Millimeters{h->value()});
    undoStack_.execute(std::make_unique<commands::SetCanvasCommand>(c), project_);
    applyCanvasToView();
    refreshImage();
    updateActions();
    statusBar()->showMessage(
        tr("Cadre : %1 × %2 mm").arg(w->value(), 0, 'f', 1).arg(h->value(), 0, 'f', 1));
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
    applyCanvasToView();  // taille du cadre (peut avoir changé : réglage, undo, chargement)
    setWindowModified(true);  // toute régénération suit une mutation du document
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
    // État Clean/ManuallyEdited/Dirty par objet retouché (Lot 8.2) et vue
    // brute de l'objet en cours d'édition (Lot 8.2, source unique pour le
    // placement des poignées dans renderBase et la construction des
    // commandes) : tenus à jour ici. Ni displayImage() ni updateActions() ne
    // les recalculent : ils lisent editStates_/stitchEditView_.
    editStates_.clear();
    if (!sequenceImported_) {
        sequence_.reset();
    }
    stitchEditView_.reset();
    if (!sequenceImported_ && !project_.embroidery_objects.empty()) {
        // refresh_context (pas effective_sequence + classify_all_edit_states +
        // edit_view séparément) : un seul appel interne à generate_sequence
        // produit à la fois la séquence effective -- retouches appliquées,
        // contenu identique à effective_sequence(project), seul point d'entree
        // autorise pour l'affichage/export (cf.
        // tests/check_no_raw_sequence_bypass.cmake) --, les états par objet et
        // la vue d'édition de l'objet ciblé, au lieu de jusqu'à trois
        // régénérations indépendantes après chaque glisser sur un motif dense.
        // stitchEditTarget_ ne peut être défini que sur un objet du document
        // (cf. onStitchEditModeToggled/resolveSelectedEmbroidery) : inutile de
        // rien calculer ici quand embroidery_objects est vide ou qu'une
        // séquence importée est affichée -- editStates_/stitchEditView_
        // resteraient de toute façon vides/absents dans ces cas.
        if (auto ctx = stitch_generation::refresh_context(project_, stitchEditTarget_)) {
            sequence_ = std::move(ctx->effective);
            editStates_ = std::move(ctx->edit_states);
            if (ctx->target_view &&
                ctx->target_view->state != stitch_generation::ObjectEditState::Dirty) {
                stitchEditView_ = std::move(*ctx->target_view);
            }
        } else {
            statusBar()->showMessage(
                tr("Génération des points impossible : %1")
                    .arg(QString::fromStdString(ctx.error().message)));
        }
    }
    if (simToolbar_ != nullptr) {
        updateSimulationRange();
    }
    if (orderDock_ != nullptr) {
        refreshOrderPanel();
    }
    if (filterDock_ != nullptr) {
        refreshFilterPanel();
    }
    refreshDocumentPanel();
    displayImage(processed_);
}

void MainWindow::displayImage(const image::Image& img) {
    renderBase(img);
    renderStitches();
}

void MainWindow::renderBase(const image::Image& img) {
    // Retire uniquement la couche base ; la couche points est gérée à part.
    for (QGraphicsItem* it : baseItems_) {
        scene_->removeItem(it);
        delete it;
    }
    baseItems_.clear();

    if (!img.empty() && (showImageAct_ == nullptr || showImageAct_->isChecked())) {
        const QImage qimg(img.rgba.data(), img.width, img.height, img.width * 4,
                          QImage::Format_RGBA8888);
        const QPixmap pixmap = QPixmap::fromImage(qimg.copy());
        auto* item = scene_->addPixmap(pixmap);
        const double mmPerPx = project_.mm_per_px.value;
        const double wMm = img.width * mmPerPx;
        const double hMm = img.height * mmPerPx;
        item->setTransform(QTransform::fromScale(mmPerPx, mmPerPx));
        item->setPos(-wMm / 2.0, -hMm / 2.0);
        baseItems_.append(item);
    }

    // Objets vectoriels (remplissage translucide + contour).
    if (showVectorsAct_ != nullptr && showVectorsAct_->isChecked()) {
        for (const auto& object : project_.vector_objects) {
            if (!object.visible) {
                continue;
            }
            const bool selected = selectedObject_ && object.id == *selectedObject_;
            const QColor color(object.rgb[0], object.rgb[1], object.rgb[2]);
            const QPainterPath outline = objectPainterPath(object);
            // Sélection à DOUBLE contraste : un halo clair sous un trait d'accent,
            // lisible quel que soit le fond du canevas.
            if (selected) {
                QPen halo(AppTheme::instance().tokens().canvasSelectionHalo);
                halo.setCosmetic(true);
                halo.setWidth(6);
                auto* haloItem = scene_->addPath(outline, halo, Qt::NoBrush);
                haloItem->setZValue(9);
                baseItems_.append(haloItem);
            }
            QPen pen(selected ? AppTheme::instance().tokens().canvasSelectionLine
                              : color.darker(150));
            pen.setCosmetic(true);
            pen.setWidth(selected ? 3 : 2);
            auto* pathItem = scene_->addPath(outline, pen,
                                             QBrush(QColor(color.red(), color.green(),
                                                           color.blue(), 90)));
            pathItem->setZValue(10);
            baseItems_.append(pathItem);
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
                                    // Diffère : refreshImage() détruirait cette poignée
                                    // pendant son propre événement souris (crash).
                                    QTimer::singleShot(0, this, [this, objectId, ref, pos, newPos] {
                                        undoStack_.execute(
                                            std::make_unique<commands::MoveNodeCommand>(
                                                objectId, ref, pos, newPos),
                                            project_);
                                        refreshImage();
                                        updateActions();
                                    });
                                });
                            scene_->addItem(handle);
                            baseItems_.append(handle);
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

    // Poignée de rotation du remplissage tatami sélectionné : un axe montrant
    // l'orientation des fils, avec un bouton à faire glisser. Le nouvel angle est
    // validé au relâchement (commande annulable) — pas de recalcul par pixel.
    if (auto* fill = currentFillObject()) {
        const auto& tatami = std::get<document::TatamiParams>(fill->params);
        const Vec2um cUm = embroideryCentroid(*fill);
        const QPointF c(to_millimeters(cUm.x).value, -to_millimeters(cUm.y).value);
        double radius = 12.0;  // mm
        if (const auto* vec = project_.findObject(fill->source_vector)) {
            const QRectF bb = objectPainterPath(*vec).boundingRect();
            radius = std::clamp(0.45 * std::min(bb.width(), bb.height()), 6.0, 60.0);
        }
        const double theta = tatami.angle.radians;  // repère physique (Y haut)
        const double dx = radius * std::cos(theta);
        const double dy = -radius * std::sin(theta);  // scène : Y vers le bas
        QPen axisPen(AppTheme::instance().tokens().canvasHandle);
        axisPen.setCosmetic(true);
        axisPen.setWidth(2);
        auto* axis = scene_->addLine(QLineF(c.x() - dx, c.y() - dy, c.x() + dx, c.y() + dy), axisPen);
        axis->setZValue(98);
        baseItems_.append(axis);

        const ObjectId fillId = fill->id;
        auto* knob = new NodeHandleItem(
            QPointF(c.x() + dx, c.y() + dy), [this, fillId, c](QPointF released) {
                double angle = std::atan2(-(released.y() - c.y()), released.x() - c.x());
                angle = std::fmod(angle, std::numbers::pi);
                if (angle < 0.0) {
                    angle += std::numbers::pi;
                }
                // Diffère la commande : refreshImage() reconstruit la scène et
                // détruirait cette poignée pendant son propre événement souris
                // (usage après libération → crash).
                QTimer::singleShot(0, this, [this, fillId, angle] {
                    undoStack_.execute(
                        std::make_unique<commands::SetFillAngleCommand>(fillId, Angle{angle}),
                        project_);
                    refreshImage();
                    updateActions();
                });
            },
            [this, c](QPointF moved) {  // aperçu : angle affiché pendant le glisser
                double angle = std::atan2(-(moved.y() - c.y()), moved.x() - c.x());
                angle = std::fmod(angle, std::numbers::pi);
                if (angle < 0.0) {
                    angle += std::numbers::pi;
                }
                statusBar()->showMessage(
                    tr("Orientation : %1°").arg(angle * 180.0 / std::numbers::pi, 0, 'f', 0));
            });
        knob->setPen(QPen(AppTheme::instance().tokens().canvasHandle, 1.5));
        knob->setBrush(QBrush(AppTheme::instance().tokens().canvasHandle.lighter(160)));
        knob->setCursor(Qt::CrossCursor);
        knob->setToolTip(tr("Glisser pour tourner l'orientation des fils"));
        scene_->addItem(knob);
        baseItems_.append(knob);
    }

    // Guides satin paramétriques. Chaque extrémité est indépendante mais reste
    // contrainte à son rail ; la validation monotone est partagée avec les
    // tests du cœur, donc un geste accepté ne disparaît pas à la génération.
    if (satinGuideModeAct_ != nullptr && satinGuideModeAct_->isChecked() &&
        satinGuideTarget_) {
        if (const auto* obj = project_.findEmbroidery(*satinGuideTarget_)) {
            if (const auto* satin = std::get_if<document::SatinParams>(&obj->params)) {
                const ObjectId targetId = obj->id;
                const std::uint64_t generation = documentGeneration_;
                for (std::size_t i = 0; i < satin->rungs.size(); ++i) {
                    const auto rung = satin->rungs[i];
                    const QPointF a(to_millimeters(rung.a.x).value,
                                    -to_millimeters(rung.a.y).value);
                    const QPointF b(to_millimeters(rung.b.x).value,
                                    -to_millimeters(rung.b.y).value);
                    QPen guidePen(AppTheme::instance().tokens().accent);
                    guidePen.setCosmetic(true);
                    const bool selected = selectedSatinGuide_ && *selectedSatinGuide_ == i;
                    guidePen.setWidthF(selected ? 3.0 : 1.5);
                    if (selected) {
                        guidePen.setColor(AppTheme::instance().tokens().accent.lighter(150));
                    }
                    auto* line = new SatinGuideItem(QLineF(a, b), [this, targetId, i, generation] {
                        QTimer::singleShot(0, this, [this, targetId, i, generation] {
                            if (generation != documentGeneration_ || !satinGuideTarget_ ||
                                *satinGuideTarget_ != targetId) {
                                return;
                            }
                            selectedSatinGuide_ = i;
                            displayImage(processed_);
                            updateActions();
                        });
                    });
                    line->setPen(guidePen);
                    line->setZValue(99);
                    line->setToolTip(tr("Guide satin #%1 — cliquer pour sélectionner").arg(i + 1));
                    scene_->addItem(line);
                    baseItems_.append(line);

                    const auto addEndpoint = [this, targetId, i, rung, generation](
                                                 QPointF scenePos,
                                                 stitch_generation::SatinGuideSide side) {
                        auto* handle = new NodeHandleItem(
                            scenePos,
                            [this, targetId, i, rung, generation, side](QPointF released) {
                                const Vec2um desired{
                                    to_micrometers(Millimeters{released.x()}),
                                    to_micrometers(Millimeters{-released.y()})};
                                const auto* current = project_.findEmbroidery(targetId);
                                const auto* satinNow =
                                    current != nullptr
                                        ? std::get_if<document::SatinParams>(&current->params)
                                        : nullptr;
                                const auto moved =
                                    satinNow != nullptr
                                        ? stitch_generation::move_satin_guide_endpoint(
                                              *satinNow, i, side, desired)
                                        : std::nullopt;
                                if (!moved) {
                                    statusBar()->showMessage(tr(
                                        "Guide refusé : il doit rester ordonné sur les deux rails."));
                                    displayImage(processed_);
                                    return;
                                }
                                if (*moved == rung) {
                                    return;
                                }
                                QTimer::singleShot(
                                    0, this, [this, targetId, i, moved = *moved, generation] {
                                        if (generation != documentGeneration_) {
                                            return;
                                        }
                                        undoStack_.execute(
                                            std::make_unique<commands::MoveSatinGuideCommand>(
                                                targetId, i, moved),
                                            project_);
                                        refreshImage();
                                        updateActions();
                                    });
                            });
                        handle->setPen(QPen(AppTheme::instance().tokens().accent, 1.5));
                        handle->setBrush(
                            QBrush(AppTheme::instance().tokens().accent.lighter(160)));
                        handle->setToolTip(
                            side == stitch_generation::SatinGuideSide::RailA
                                ? tr("Guide satin #%1 — extrémité rail A").arg(i + 1)
                                : tr("Guide satin #%1 — extrémité rail B").arg(i + 1));
                        scene_->addItem(handle);
                        baseItems_.append(handle);
                    };
                    addEndpoint(a, stitch_generation::SatinGuideSide::RailA);
                    addEndpoint(b, stitch_generation::SatinGuideSide::RailB);
                }
            }
        }
    }

    // Poignées de points de couture (Lot 8.2, mode d'édition des points) :
    // un point déplaçable par entrée TopStitch/Stitch de la vue brute de
    // l'objet ciblé (stitchEditView_, tenu à jour par refreshImage). Visuellement
    // et conceptuellement distinctes des poignées de nœuds vectoriels
    // ci-dessus (couleur dédiée) : ici on déplace le point COUSU (retouche
    // locale), pas le nœud source (qui régénère tout).
    if (stitchEditModeAct_ != nullptr && stitchEditModeAct_->isChecked() && stitchEditView_ &&
        stitchEditTarget_) {
        // Un GraphicsItem par poignée coûte cher (mémoire + événements) : au-delà
        // de ce seuil, le mode reste actif mais n'affiche aucune poignée plutôt
        // que de dégrader l'interface (même logique que `drawDots` ci-dessous
        // pour la broderie complète). `updateActions()` (appelé juste après
        // tout `refreshImage()`) sort proprement du mode et l'explique en barre
        // de statut dès qu'il détecte ce dépassement — ce garde-fou ici couvre
        // seulement le tout premier rendu, avant qu'`updateActions()` ait pu
        // tourner (cf. son bloc `stitchEditTooManyPoints`).
        const auto& view = *stitchEditView_;
        const ObjectId targetId = *stitchEditTarget_;
        // Le seuil porte sur les points RÉELLEMENT déplaçables (une passe
        // TopStitch/Stitch), pas sur `view.raw.size()` qui compte aussi les
        // sauts/points de sous-couche jamais dotés d'une poignée.
        const auto movableCount = std::count_if(view.raw.begin(), view.raw.end(),
                                                 stitch_generation::is_movable_point);
        if (const auto* obj = project_.findEmbroidery(targetId);
            obj != nullptr &&
            static_cast<std::size_t>(movableCount) <= kMaxEditableStitchHandles) {
            // Overrides indexés une fois par `base_index` : la boucle sur les
            // poignées ci-dessous ne doit pas redevenir O(points*overrides) en
            // rescannant `obj->overrides` pour chaque point (motif dense).
            std::unordered_map<std::size_t, Vec2um> movedOverrides;
            movedOverrides.reserve(obj->overrides.size());
            for (const auto& ov : obj->overrides) {
                if (ov.moved_to) {
                    movedOverrides.emplace(ov.base_index, *ov.moved_to);
                }
            }
            for (std::size_t i = 0; i < view.raw.size(); ++i) {
                const auto& cmd = view.raw[i];
                if (!stitch_generation::is_movable_point(cmd)) {
                    continue;
                }
                Vec2um pos = cmd.pos;  // position affichée : retouche existante sinon brute
                if (auto it = movedOverrides.find(i); it != movedOverrides.end()) {
                    pos = it->second;
                }
                const QPointF sceneMm(to_millimeters(pos.x).value, -to_millimeters(pos.y).value);
                const std::size_t baseIndex = i;
                const std::uint64_t rawFingerprint = view.fingerprint;
                const std::uint32_t rawPointCount = view.point_count;
                // Génération du document au moment où la poignée est construite :
                // si elle a changé quand le QTimer différé ci-dessous se déclenche,
                // `project_` a été intégralement remplacé entretemps (nouveau
                // document/projet chargé/import DST) -- abandonner plutôt que de
                // muter un projet qui n'est plus celui visé par ce glisser.
                const std::uint64_t generation = documentGeneration_;
                auto* handle = new NodeHandleItem(
                    sceneMm, [this, targetId, baseIndex, pos, rawFingerprint, rawPointCount,
                              generation](QPointF newSceneMm) {
                        const Vec2um newPos{to_micrometers(Millimeters{newSceneMm.x()}),
                                            to_micrometers(Millimeters{-newSceneMm.y()})};
                        if (newPos == pos) {
                            return;  // pas de mouvement : aucune commande (pas de pollution undo)
                        }
                        // Diffère : refreshImage() détruirait cette poignée pendant
                        // son propre événement souris (même précaution que les
                        // poignées de nœuds/rotation ci-dessus).
                        QTimer::singleShot(
                            0, this,
                            [this, targetId, baseIndex, newPos, rawFingerprint, rawPointCount,
                             generation] {
                                if (generation != documentGeneration_) {
                                    return;  // document remplacé pendant le délai : commande abandonnée
                                }
                                undoStack_.execute(
                                    std::make_unique<commands::MoveStitchPointCommand>(
                                        targetId, baseIndex, newPos, rawFingerprint,
                                        rawPointCount),
                                    project_);
                                refreshImage();
                                updateActions();
                            });
                    },
                    [this](QPointF) {
                        statusBar()->showMessage(tr("Glisser pour déplacer le point de couture."));
                    });
                handle->setPen(QPen(AppTheme::instance().tokens().accent, 1.5));
                handle->setBrush(QBrush(AppTheme::instance().tokens().accent.lighter(150)));
                handle->setToolTip(tr("Point de couture #%1 — glisser pour déplacer").arg(i));
                scene_->addItem(handle);
                baseItems_.append(handle);
            }
        }
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
        baseItems_.append(mapItem);
    }
}

void MainWindow::renderStitches() {
    for (QGraphicsItem* it : stitchItems_) {
        scene_->removeItem(it);
        delete it;
    }
    stitchItems_.clear();

    if (showStitchesAct_ == nullptr || !showStitchesAct_->isChecked() || !sequence_) {
        return;
    }

    const int total = static_cast<int>(sequence_->commands.size());
    const int limit = simulating() ? simStep_ : total;
    // Les pastilles de pénétration (une ellipse par point) coûtent cher : on
    // ne les dessine que pour les petits motifs et hors simulation.
    const bool drawDots = !simulating() && total <= 4000;

    // Visibilité (filtres) et couleur de fil par objet de broderie.
    std::unordered_map<std::uint64_t, bool> visible;
    std::unordered_map<std::uint64_t, QRgb> colorOf;
    for (const auto& emb : project_.embroidery_objects) {
        visible[emb.id.value] = objectPassesFilter(emb);
        colorOf[emb.id.value] = qRgb(emb.rgb[0], emb.rgb[1], emb.rgb[2]);
    }
    const auto isVisible = [&](std::uint64_t src) {
        const auto it = visible.find(src);
        return it != visible.end() && it->second;
    };

    // Un tracé cousu par couleur de fil (pour afficher la broderie en couleur).
    std::map<QRgb, QPainterPath> sewByColor;
    QPainterPath jumpPath;
    QPainterPath dots;
    bool hasPos = false;
    QPointF last;
    std::uint64_t lastSource = 0;
    QPointF needle;
    bool hasNeedle = false;
    for (int i = 0; i < total && i <= limit; ++i) {
        const auto& cmd = sequence_->commands[static_cast<std::size_t>(i)];
        const QPointF p(to_millimeters(cmd.pos.x).value, -to_millimeters(cmd.pos.y).value);
        const std::uint64_t src = cmd.source.value;
        const bool vis = isVisible(src);
        switch (cmd.type) {
        case stitch::CommandType::Stitch:
            // On ne relie que deux points du MÊME objet (jamais entre objets).
            if (vis && hasPos && lastSource == src) {
                QPainterPath& path = sewByColor[colorOf[src]];
                path.moveTo(last);
                path.lineTo(p);
            }
            if (vis && drawDots) {
                dots.addEllipse(p, 0.15, 0.15);
            }
            last = p;
            hasPos = true;
            lastSource = src;
            if (vis) {
                needle = p;
                hasNeedle = true;
            }
            break;
        case stitch::CommandType::Jump:
            if (vis && hasPos && lastSource == src) {
                jumpPath.moveTo(last);
                jumpPath.lineTo(p);
            }
            last = p;
            hasPos = true;
            lastSource = src;
            if (vis) {
                needle = p;
                hasNeedle = true;
            }
            break;
        default:
            hasPos = false;  // ColorChange / End : rompt la continuité
            break;
        }
    }

    for (const auto& [rgb, path] : sewByColor) {
        QColor c = QColor::fromRgb(rgb);
        // Un fil très clair serait invisible sur fond blanc : on l'assombrit un peu.
        if (c.lightnessF() > 0.85) {
            c = c.darker(140);
        }
        QPen sewPen(c);
        sewPen.setCosmetic(true);
        sewPen.setWidth(2);
        auto* sewItem = scene_->addPath(path, sewPen);
        sewItem->setZValue(20);
        stitchItems_.append(sewItem);
    }

    const Tokens& tk = AppTheme::instance().tokens();
    QPen jumpPen(tk.canvasJump);
    jumpPen.setCosmetic(true);
    jumpPen.setStyle(Qt::DashLine);
    auto* jumpItem = scene_->addPath(jumpPath, jumpPen);
    jumpItem->setZValue(20);
    stitchItems_.append(jumpItem);

    if (drawDots) {
        auto* dotsItem = scene_->addPath(dots, Qt::NoPen, QBrush(tk.canvasStitch));
        dotsItem->setZValue(21);
        stitchItems_.append(dotsItem);
    }

    if (simulating() && hasNeedle) {
        auto* marker = scene_->addEllipse(needle.x() - 0.6, needle.y() - 0.6, 1.2, 1.2,
                                          QPen(Qt::NoPen), QBrush(tk.error));
        marker->setZValue(30);
        stitchItems_.append(marker);
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

    // Sélectionne le nouveau remplissage pour que « Orientation du remplissage… »
    // s'applique directement à lui.
    selectedEmbroidery_ = object.id;
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

void MainWindow::autoDigitize() {
    if (!project_.segmentation) {
        QMessageBox::information(
            this, tr("Numérisation automatique"),
            tr("Segmentez d'abord l'image (menu Segmentation), puis relancez."));
        return;
    }
    if (!project_.embroidery_objects.empty() || !project_.vector_objects.empty()) {
        const auto answer = QMessageBox::question(
            this, tr("Numérisation automatique"),
            tr("Des objets existent déjà ; la numérisation ajoute de nouveaux objets. "
               "Continuer ?"));
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    autodigitize::AutoOptions opts;
    opts.mm_per_px = project_.mm_per_px;
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    auto result = autodigitize::auto_digitize(*project_.segmentation, project_.object_ids, opts);
    QGuiApplication::restoreOverrideCursor();
    if (!result) {
        QMessageBox::warning(this, tr("Numérisation impossible"),
                             QString::fromStdString(result.error().message));
        return;
    }
    const std::size_t vecCount = result->vectors.size();
    const std::size_t embCount = result->embroideries.size();

    undoStack_.execute(std::make_unique<commands::AddObjectBatchCommand>(
                           std::move(result->vectors), std::move(result->embroideries)),
                       project_);
    showStitchesAct_->setChecked(true);
    refreshImage();
    updateActions();
    statusBar()->showMessage(
        tr("Numérisation automatique : %1 objet(s) vectoriel(s), %2 objet(s) de broderie — "
           "tous éditables")
            .arg(vecCount)
            .arg(embCount));
}

void MainWindow::createSatinObject() {
    if (!selectedObject_) {
        return;
    }
    const auto* source = project_.findObject(*selectedObject_);
    if (source == nullptr || source->paths.empty()) {
        return;
    }

    // Découpe le contour extérieur du premier morceau en deux rails.
    const auto rails = stitch_generation::rails_from_contour(source->paths.front().outer);
    if (!rails) {
        QMessageBox::warning(this, tr("Satin impossible"),
                             tr("La forme est trop petite ou trop complexe pour une colonne "
                                "satin. Essayez un remplissage tatami."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Colonne satin"));
    auto* layout = new QFormLayout(&dialog);
    auto* warn = new QLabel(
        tr("⚠ Le satin automatique est expérimental. Vérifiez impérativement le "
           "résultat (densité, virages, extrémités) avant tout passage sur machine."),
        &dialog);
    warn->setWordWrap(true);
    warn->setStyleSheet("color:#8a5a00;");
    layout->addRow(warn);
    auto* densitySpin = new QDoubleSpinBox(&dialog);
    densitySpin->setRange(0.1, 1.5);
    densitySpin->setValue(0.4);
    densitySpin->setDecimals(2);
    densitySpin->setSuffix(tr(" mm"));
    auto* compSpin = new QDoubleSpinBox(&dialog);
    compSpin->setRange(0.0, 1.0);
    compSpin->setValue(0.0);
    compSpin->setDecimals(2);
    compSpin->setSuffix(tr(" mm"));
    auto* underlayCheck = new QCheckBox(tr("Sous-couche centrale"), &dialog);
    underlayCheck->setChecked(true);
    layout->addRow(tr("Densité (écart) :"), densitySpin);
    layout->addRow(tr("Compensation de tirage :"), compSpin);
    layout->addRow(underlayCheck);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    document::SatinParams params;
    params.rail_a = rails->first;
    params.rail_b = rails->second;
    params.density = to_micrometers(Millimeters{densitySpin->value()});
    params.pull_compensation = to_micrometers(Millimeters{compSpin->value()});
    params.center_underlay = underlayCheck->isChecked();

    // Avertissement de largeur excessive (§5.3) : ne masque jamais la limite
    // physique — on prévient et on suggère le tatami.
    stitch_generation::SatinConfig probe;
    probe.density = params.density;
    const double maxWidth =
        stitch_generation::fill_satin(params.rail_a, params.rail_b, probe).max_width_um;
    if (maxWidth > static_cast<double>(params.max_width.value)) {
        const auto answer = QMessageBox::warning(
            this, tr("Satin large"),
            tr("La colonne atteint %1 mm de large — au-delà de la limite recommandée "
               "(%2 mm), le fil risque d'accrocher. Un remplissage tatami serait plus "
               "solide. Créer quand même le satin ?")
                .arg(maxWidth / 1000.0, 0, 'f', 1)
                .arg(params.max_width.value / 1000.0, 0, 'f', 1),
            QMessageBox::Yes | QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    document::EmbroideryObject object;
    object.id = project_.object_ids.next();
    object.name = tr("Satin de %1").arg(QString::fromStdString(source->name)).toStdString();
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
        statusBar()->showMessage(tr("Colonne satin générée : %1 points").arg(stats.stitches));
    }
}

document::EmbroideryObject* MainWindow::currentFillObject() {
    if (selectedEmbroidery_) {
        if (auto* emb = project_.findEmbroidery(*selectedEmbroidery_);
            emb != nullptr && emb->is_tatami()) {
            return emb;
        }
    }
    if (selectedObject_) {
        for (auto& emb : project_.embroidery_objects) {
            if (emb.source_vector == *selectedObject_ && emb.is_tatami()) {
                return &emb;
            }
        }
    }
    return nullptr;
}

std::optional<ObjectId> MainWindow::objectAt(QPointF posMm) const {
    std::optional<ObjectId> hit;
    for (const auto& object : project_.vector_objects) {
        if (object.visible && objectPainterPath(object).contains(posMm)) {
            hit = object.id;  // le dernier dessiné (au-dessus) gagne
        }
    }
    return hit;
}

document::EmbroideryObject* MainWindow::embroideryForVector(ObjectId vectorId) {
    for (auto& emb : project_.embroidery_objects) {
        if (emb.source_vector == vectorId) {
            return &emb;
        }
    }
    return nullptr;
}

void MainWindow::autoConvertToSatin() {
    if (!selectedObject_) {
        return;
    }
    const auto* source = project_.findObject(*selectedObject_);
    if (source == nullptr || source->paths.empty()) {
        return;
    }

    const auto result = auto_satin::build_satin_columns(source->paths.front(), {});
    const auto& rep = result.report;
    QString info = tr("Satinabilité : %1 (confiance %2)\n"
                      "Largeur min / moy / max : %3 / %4 / %5 mm\n"
                      "Branches : %6   ·   Colonnes proposées : %7")
                       .arg(QString::fromUtf8(auto_satin::to_string(rep.status)))
                       .arg(rep.confidence, 0, 'f', 2)
                       .arg(rep.minimum_width_mm, 0, 'f', 2)
                       .arg(rep.mean_width_mm, 0, 'f', 2)
                       .arg(rep.maximum_width_mm, 0, 'f', 2)
                       .arg(rep.branch_count)
                       .arg(result.columns.size());
    for (const auto& w : result.warnings) {
        info += tr("\nAttention : %1").arg(QString::fromStdString(w));
    }

    if (result.columns.empty()) {
        QMessageBox::information(
            this, tr("Conversion en satin"),
            tr("Conversion impossible : %1\n\n%2")
                .arg(QString::fromStdString(result.refusal), info));
        return;
    }
    const auto answer = QMessageBox::question(
        this, tr("Convertir en satin"),
        tr("%1\n\nCréer %2 colonne(s) satin ? (annulable)").arg(info).arg(result.columns.size()));
    if (answer != QMessageBox::Yes) {
        return;
    }

    std::vector<document::EmbroideryObject> objects;
    int idx = 0;
    for (const auto& col : result.columns) {
        document::SatinParams sp;
        sp.rail_a = col.rail_a;
        sp.rail_b = col.rail_b;
        for (const auto& r : col.rungs) {
            sp.rungs.push_back(document::SatinRung{r.a, r.b});
        }
        document::EmbroideryObject emb;
        emb.id = project_.object_ids.next();
        emb.name = tr("Satin auto de %1 (%2)")
                       .arg(QString::fromStdString(source->name))
                       .arg(++idx)
                       .toStdString();
        emb.source_vector = source->id;
        emb.rgb = source->rgb;
        emb.params = sp;
        objects.push_back(std::move(emb));
    }
    undoStack_.execute(std::make_unique<commands::AddObjectBatchCommand>(
                           std::vector<document::VectorObject>{}, std::move(objects)),
                       project_);
    showStitchesAct_->setChecked(true);
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("%1 colonne(s) satin créée(s).").arg(result.columns.size()));
}

void MainWindow::changeFillAngle() {
    auto* object = currentFillObject();
    if (object == nullptr) {
        return;
    }
    const auto& tatami = std::get<document::TatamiParams>(object->params);

    // Angle stocké en radians ; présenté en degrés. C'est l'orientation d'une
    // droite, donc modulo 180° (0° = rangées horizontales).
    const double currentDeg = tatami.angle.radians * 180.0 / std::numbers::pi;
    int startDeg = ((static_cast<int>(std::lround(currentDeg)) % 180) + 180) % 180;

    bool ok = false;
    const int deg = QInputDialog::getInt(this, tr("Orientation du remplissage"),
                                         tr("Angle des fils (°) :"), startDeg, 0, 179, 1, &ok);
    if (!ok) {
        return;
    }

    const Angle next{deg * std::numbers::pi / 180.0};
    undoStack_.execute(std::make_unique<commands::SetFillAngleCommand>(object->id, next), project_);
    refreshImage();
    updateActions();
    if (sequence_) {
        const auto stats = stitch::compute_stats(*sequence_);
        statusBar()->showMessage(
            tr("Orientation du remplissage : %1° — %2 points").arg(deg).arg(stats.stitches));
    }
}

void MainWindow::convertSatinsToTatami() {
    std::vector<ObjectId> satins;
    for (const auto& emb : project_.embroidery_objects) {
        if (emb.is_satin()) {
            satins.push_back(emb.id);
        }
    }
    if (satins.empty()) {
        return;
    }
    const auto answer = QMessageBox::question(
        this, tr("Convertir en tatami"),
        tr("%1 colonne(s) satin automatique(s) vont devenir des remplissages tatami "
           "(découpés sur leur région, sans débordement). L'orientation restera réglable. "
           "Continuer ?")
            .arg(satins.size()));
    if (answer != QMessageBox::Yes) {
        return;
    }
    undoStack_.execute(std::make_unique<commands::ConvertFillsToTatamiCommand>(std::move(satins)),
                       project_);
    refreshImage();
    updateActions();
    if (sequence_) {
        const auto stats = stitch::compute_stats(*sequence_);
        statusBar()->showMessage(tr("Satins convertis en tatami : %1 points").arg(stats.stitches));
    }
}

void MainWindow::setStitchType(ObjectId embroideryId, int type) {
    auto* emb = project_.findEmbroidery(embroideryId);
    if (emb == nullptr) {
        return;
    }
    document::StitchParams params;
    std::string label;
    switch (type) {
    case 0: {  // contour cousu
        document::RunningStitchParams rp;
        rp.repeats = 3;
        params = rp;
        label = "Type : contour";
        break;
    }
    case 1:  // tatami
        params = document::TatamiParams{};
        label = "Type : tatami";
        break;
    case 2: {  // satin : exige deux rails, construits depuis le contour source
        const auto* source = project_.findObject(emb->source_vector);
        if (source == nullptr || source->paths.empty()) {
            QMessageBox::warning(this, tr("Satin impossible"),
                                 tr("Aucun contour source pour construire les rails."));
            return;
        }
        auto rails = stitch_generation::rails_from_contour(source->paths.front().outer);
        if (!rails) {
            QMessageBox::warning(this, tr("Satin impossible"),
                                 tr("La forme est trop petite ou trop complexe pour une colonne "
                                    "satin. Essayez un tatami."));
            return;
        }
        document::SatinParams sp;
        sp.rail_a = rails->first;
        sp.rail_b = rails->second;
        params = sp;
        label = "Type : satin";
        break;
    }
    default:
        return;
    }
    undoStack_.execute(std::make_unique<commands::SetStitchTypeCommand>(embroideryId, std::move(params),
                                                                        std::move(label)),
                       project_);
    showStitchesAct_->setChecked(true);
    refreshImage();
    updateActions();
}

void MainWindow::onCanvasContextMenu(QPointF posMm, QPoint globalPos) {
    const auto hit = objectAt(posMm);
    QMenu menu(this);

    if (hit) {
        selectedObject_ = hit;
        selectedRegion_.reset();
        selectedEmbroidery_.reset();
        const auto* vec = project_.findObject(*hit);
        auto* emb = embroideryForVector(*hit);
        if (vec != nullptr) {
            auto* title = menu.addAction(QString::fromStdString(vec->name));
            title->setEnabled(false);
            menu.addSeparator();
        }
        if (emb != nullptr) {
            const ObjectId embId = emb->id;
            auto* typeMenu = menu.addMenu(tr("Type de points"));
            const int current = emb->is_tatami() ? 1 : emb->is_satin() ? 2 : 0;
            const char* labels[] = {"Contour cousu", "Remplissage tatami", "Colonne satin"};
            for (int t = 0; t < 3; ++t) {
                auto* act = typeMenu->addAction(tr(labels[t]));
                act->setCheckable(true);
                act->setChecked(t == current);
                connect(act, &QAction::triggered, this, [this, embId, t] { setStitchType(embId, t); });
            }
            if (emb->is_tatami()) {
                auto* rot = menu.addAction(tr("Orientation du remplissage…"));
                connect(rot, &QAction::triggered, this, &MainWindow::changeFillAngle);
            }
        }
    }

    if (!menu.isEmpty()) {
        menu.addSeparator();
    }
    // Calques accessibles partout (même sans objet sous le curseur).
    auto* layers = menu.addMenu(tr("Calques"));
    for (QAction* act : {showImageAct_, showSegAct_, showVectorsAct_, showStitchesAct_}) {
        if (act != nullptr) {
            layers->addAction(act);
        }
    }

    displayImage(processed_);  // reflète la sélection éventuelle
    updateActions();
    menu.exec(globalPos);
}

void MainWindow::buildHelpMenu() {
    // « Ajuster au canevas » aussi sur la touche F (en plus de Ctrl+0). On
    // n'intercepte PAS Tab (réservé à la navigation clavier, accessibilité).
    auto* fitShortcut = new QShortcut(QKeySequence(Qt::Key_F), this);
    connect(fitShortcut, &QShortcut::activated, view_, &CanvasView::fitCanvas);

    auto* helpMenu = menuBar()->addMenu(tr("Aid&e"));
    auto* shortcutsAct = helpMenu->addAction(tr("&Raccourcis clavier…"));
    connect(shortcutsAct, &QAction::triggered, this, [this] {
        QMessageBox::information(
            this, tr("Raccourcis clavier"),
            tr("Ctrl+O   Ouvrir une image\n"
               "Ctrl+S   Enregistrer le projet\n"
               "Ctrl+Z / Ctrl+Y   Annuler / Rétablir\n"
               "Suppr   Supprimer la région sélectionnée\n"
               "Ctrl++ / Ctrl+- / Ctrl+0   Zoom avant / arrière / ajuster\n"
               "F   Ajuster au canevas\n"
               "F5   Analyser le motif\n"
               "V / H / M   Outils : Sélection / Déplacer la vue / Rectangle\n"
               "Échap   Revenir à la Sélection\n"
               "Ctrl+Shift+P   Masquer / afficher les panneaux"));
    });
    auto* aboutAct = helpMenu->addAction(tr("À &propos"));
    connect(aboutAct, &QAction::triggered, this, [this] {
        QMessageBox::about(
            this, tr("À propos"),
            tr("%1 %2\nNumérisation de broderie machine, logiciel libre (Apache-2.0).")
                .arg(QString::fromUtf8(kAppName), QString::fromUtf8(kAppVersion)));
    });
}

void MainWindow::buildMainToolbar() {
    mainToolbar_ = addToolBar(tr("Barre principale"));
    mainToolbar_->setObjectName(QStringLiteral("mainToolbar"));
    mainToolbar_->setMovable(false);
    mainToolbar_->setIconSize(QSize(18, 18));

    const auto add = [this](const QIcon& icon, const QString& text, auto slot) {
        auto* act = mainToolbar_->addAction(icon, text);
        act->setToolTip(text);
        connect(act, &QAction::triggered, this, slot);
        return act;
    };
    add(icons::openImage(), tr("Ouvrir une image"), &MainWindow::openImage);
    add(icons::openProject(), tr("Ouvrir un projet"), &MainWindow::loadProject);
    add(icons::save(), tr("Enregistrer le projet"), &MainWindow::saveProject);
    mainToolbar_->addSeparator();
    undoAct_->setIcon(icons::undo());
    redoAct_->setIcon(icons::redo());
    mainToolbar_->addAction(undoAct_);
    mainToolbar_->addAction(redoAct_);
    mainToolbar_->addSeparator();
    add(icons::zoomOut(), tr("Zoom arrière"), [this] { view_->zoomOut(); });
    add(icons::fit(), tr("Ajuster au canevas"), [this] { view_->fitCanvas(); });
    add(icons::zoomIn(), tr("Zoom avant"), [this] { view_->zoomIn(); });
    mainToolbar_->addSeparator();
    analyzeAct_->setIcon(icons::analyze());
    mainToolbar_->addAction(analyzeAct_);
    showStitchesAct_->setIcon(icons::stitches());
    mainToolbar_->addAction(showStitchesAct_);
    exportDstAct_->setIcon(icons::exportDst());
    mainToolbar_->addAction(exportDstAct_);
}

void MainWindow::buildContextToolbar() {
    contextToolbar_ = addToolBar(tr("Barre contextuelle"));
    contextToolbar_->setObjectName(QStringLiteral("contextToolbar"));
    contextToolbar_->setMovable(false);
    updateContextToolbar();
}

document::EmbroideryObject* MainWindow::resolveSelectedEmbroidery() {
    document::EmbroideryObject* emb = nullptr;
    if (selectedEmbroidery_) {
        emb = project_.findEmbroidery(*selectedEmbroidery_);
    }
    if (emb == nullptr && selectedObject_) {
        emb = embroideryForVector(*selectedObject_);
    }
    return emb;
}

stitch_generation::ObjectEditState MainWindow::editStateOf(ObjectId id) const {
    for (const auto& [candidate, state] : editStates_) {
        if (candidate == id) {
            return state;
        }
    }
    return stitch_generation::ObjectEditState::Clean;  // absent du cache = Clean (jamais retouché)
}

void MainWindow::updateContextToolbar() {
    if (contextToolbar_ == nullptr) {
        return;
    }
    const document::EmbroideryObject* emb = resolveSelectedEmbroidery();
    const bool hasVec = selectedObject_.has_value();
    const bool hasReg = selectedRegion_ && project_.segmentation;
    const int pts = sequence_ ? static_cast<int>(sequence_->commands.size()) : 0;

    // Signature de l'état : évite de reconstruire (et de faire clignoter) la barre
    // quand rien de pertinent n'a changé.
    QString sig;
    if (emb != nullptr) {
        // Etat retouche (Lot 8.2) et mode d'edition inclus dans la signature :
        // sans eux, la barre ne se reconstruirait pas quand seul l'un des deux
        // change (meme objet/type toujours selectionne).
        sig = QStringLiteral("E%1t%2s%3m%4g%5i%6")
                  .arg(emb->id.value)
                  .arg(stitchTypeIndex(*emb))
                  .arg(static_cast<int>(editStateOf(emb->id)))
                  .arg(stitchEditModeAct_->isChecked() ? 1 : 0)
                  .arg(satinGuideModeAct_->isChecked() ? 1 : 0)
                  .arg(selectedSatinGuide_ ? static_cast<qlonglong>(*selectedSatinGuide_) : -1);
    } else if (hasVec) {
        sig = QStringLiteral("V%1").arg(selectedObject_->value);
    } else if (hasReg) {
        sig = QStringLiteral("R%1").arg(selectedRegion_->value);
    } else {
        sig = QStringLiteral("N%1_%2x%3")
                  .arg(pts)
                  .arg(project_.canvas.width.value)
                  .arg(project_.canvas.height.value);
    }
    if (sig == contextSig_) {
        return;
    }
    contextSig_ = sig;
    contextToolbar_->clear();

    if (emb != nullptr) {
        const ObjectId id = emb->id;
        contextToolbar_->addWidget(new QLabel(tr("Type de points :  "), contextToolbar_));
        const int current = stitchTypeIndex(*emb);
        const QString names[] = {tr("Contour"), tr("Tatami"), tr("Satin")};
        for (int t = 0; t < 3; ++t) {
            auto* btn = new QToolButton(contextToolbar_);
            btn->setText(names[t]);
            btn->setCheckable(true);
            btn->setChecked(t == current);
            connect(btn, &QToolButton::clicked, this, [this, id, t] { setStitchType(id, t); });
            contextToolbar_->addWidget(btn);
        }
        if (emb->is_tatami()) {
            contextToolbar_->addSeparator();
            auto* rot = contextToolbar_->addAction(tr("Orientation…"));
            connect(rot, &QAction::triggered, this, &MainWindow::changeFillAngle);
        }
        contextToolbar_->addSeparator();
        contextToolbar_->addAction(stitchEditModeAct_);
        if (emb->is_satin()) {
            contextToolbar_->addAction(satinGuideModeAct_);
            if (satinGuideModeAct_->isChecked()) {
                contextToolbar_->addAction(addSatinGuideAct_);
                contextToolbar_->addAction(removeSatinGuideAct_);
            }
        }
        const auto editState = editStateOf(id);
        if (editState == stitch_generation::ObjectEditState::ManuallyEdited) {
            contextToolbar_->addWidget(new QLabel(tr("  ✎ retouché  "), contextToolbar_));
        } else if (editState == stitch_generation::ObjectEditState::Dirty) {
            auto* warn = new QLabel(tr("  ⚠ retouches obsolètes  "), contextToolbar_);
            warn->setToolTip(
                tr("La géométrie ou l'ordre de couture a changé depuis les dernières "
                   "retouches : elles ne sont plus appliquées."));
            contextToolbar_->addWidget(warn);
        }
        if (editState != stitch_generation::ObjectEditState::Clean) {
            auto* discard = contextToolbar_->addAction(tr("Abandonner les retouches"));
            connect(discard, &QAction::triggered, this, [this, id] { discardOverrides(id); });
        }
    } else if (hasVec) {
        contextToolbar_->addWidget(
            new QLabel(tr("Objet vectoriel  ·  créer un objet de broderie :  "), contextToolbar_));
        const auto addCreate = [this](const QString& text, void (MainWindow::*slot)()) {
            auto* act = contextToolbar_->addAction(text);
            connect(act, &QAction::triggered, this, slot);
        };
        addCreate(tr("Contour"), &MainWindow::createRunningStitchObject);
        addCreate(tr("Tatami"), &MainWindow::createTatamiObject);
        addCreate(tr("Satin"), &MainWindow::createSatinObject);
    } else if (hasReg) {
        const auto* region = project_.segmentation->find(*selectedRegion_);
        if (region != nullptr) {
            const double mmPerPx = project_.mm_per_px.value;
            const double areaMm2 = region->pixel_count * mmPerPx * mmPerPx;
            contextToolbar_->addWidget(new QLabel(
                tr("Région %1  ·  %2 mm²    ").arg(region->id.value).arg(areaMm2, 0, 'f', 1),
                contextToolbar_));
        }
        contextToolbar_->addAction(mergeAct_);  // Fusionner (mode)
        auto* del = contextToolbar_->addAction(tr("Supprimer"));
        connect(del, &QAction::triggered, this, &MainWindow::deleteSelectedRegion);
        auto* vec = contextToolbar_->addAction(tr("Vectoriser"));
        connect(vec, &QAction::triggered, this, &MainWindow::vectorizeSelectedRegion);
    } else {
        if (project_.hasImage() && sequence_) {
            const auto st = stitch::compute_stats(*sequence_);
            const double w = to_millimeters(st.bounds.max.x - st.bounds.min.x).value;
            const double h = to_millimeters(st.bounds.max.y - st.bounds.min.y).value;
            contextToolbar_->addWidget(new QLabel(
                tr("Motif : %1 × %2 mm   ·   %3 points   ·   %4 changement(s) de couleur    ")
                    .arg(w, 0, 'f', 1)
                    .arg(h, 0, 'f', 1)
                    .arg(st.stitches)
                    .arg(st.color_changes),
                contextToolbar_));
        } else {
            contextToolbar_->addWidget(new QLabel(
                tr("Ouvrez une image, ou sélectionnez une région ou un objet.    "),
                contextToolbar_));
        }
        auto* hoop = contextToolbar_->addAction(
            tr("Cadre : %1 × %2 mm…")
                .arg(to_millimeters(project_.canvas.width).value, 0, 'f', 0)
                .arg(to_millimeters(project_.canvas.height).value, 0, 'f', 0));
        connect(hoop, &QAction::triggered, this, &MainWindow::setHoopSize);
    }
}

void MainWindow::buildToolPalette() {
    toolPalette_ = new QToolBar(tr("Outils"), this);
    toolPalette_->setObjectName(QStringLiteral("toolPalette"));
    toolPalette_->setMovable(false);
    toolPalette_->setIconSize(QSize(20, 20));
    addToolBar(Qt::LeftToolBarArea, toolPalette_);

    auto* group = new QActionGroup(this);
    const auto addTool = [&](const QIcon& icon, const QString& text, Tool tool,
                             const QKeySequence& key) {
        auto* act = toolPalette_->addAction(icon, text);
        act->setCheckable(true);
        act->setToolTip(tr("%1 (%2)").arg(text, key.toString()));
        act->setShortcut(key);
        group->addAction(act);
        connect(act, &QAction::triggered, this, [this, tool] { setTool(tool); });
        return act;
    };
    toolSelectAct_ = addTool(icons::select(), tr("Sélection"), Tool::Select,
                             QKeySequence(Qt::Key_V));
    toolPanAct_ = addTool(icons::pan(), tr("Déplacer la vue"), Tool::Pan, QKeySequence(Qt::Key_H));
    toolRectAct_ = addTool(icons::rect(), tr("Rectangle / Recadrage"), Tool::Rect,
                           QKeySequence(Qt::Key_M));
    toolSelectAct_->setChecked(true);

    // Échap : revient à la Sélection, annule le mode fusion en cours et
    // quitte le mode d'édition des points (Lot 8.2) s'il est actif -- pas
    // d'annulation du glisser en cours (l'architecture ne le permet pas
    // proprement : les poignées sont détruites/reconstruites à chaque
    // rafraîchissement, cf. renderBase), seulement une sortie propre du mode.
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escape, &QShortcut::activated, this, [this] {
        if (mergeAct_->isChecked()) {
            mergeAct_->setChecked(false);
        }
        if (stitchEditModeAct_->isChecked()) {
            stitchEditModeAct_->setChecked(false);
        }
        if (satinGuideModeAct_->isChecked()) {
            satinGuideModeAct_->setChecked(false);
        }
        setTool(Tool::Select);
    });
}

void MainWindow::setTool(Tool tool) {
    currentTool_ = tool;

    // Synchronise les cases d'outils (sans réémettre).
    const auto sync = [](QAction* act, bool on) {
        if (act == nullptr) return;
        QSignalBlocker block(act);
        act->setChecked(on);
    };
    sync(toolSelectAct_, tool == Tool::Select);
    sync(toolPanAct_, tool == Tool::Pan);
    sync(toolRectAct_, tool == Tool::Rect);
    if (cropAct_ != nullptr) {
        QSignalBlocker block(cropAct_);
        cropAct_->setChecked(tool == Tool::Rect);
    }

    // Applique au canevas : Rectangle = recadrage ; sinon vue libre.
    view_->setCropMode(tool == Tool::Rect);
    if (tool == Tool::Pan) {
        view_->setCursor(Qt::OpenHandCursor);
    } else if (tool == Tool::Select) {
        view_->setCursor(Qt::ArrowCursor);
    }

    if (toolLabel_ != nullptr) {
        const QString name = tool == Tool::Select ? tr("Sélection")
                             : tool == Tool::Pan  ? tr("Déplacer la vue")
                                                  : tr("Rectangle");
        toolLabel_->setText(tr("Outil : %1").arg(name));
    }
}

void MainWindow::buildWorkflowPanel() {
    workflowDock_ = new QDockWidget(tr("Workflow"), this);
    workflowDock_->setObjectName(QStringLiteral("workflowDock"));
    workflowDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    workflowPanel_ = new WorkflowPanel(workflowDock_);
    workflowDock_->setWidget(workflowPanel_);
    if (documentDock_ != nullptr) {
        splitDockWidget(documentDock_, workflowDock_, Qt::Vertical);  // sous Document
    } else {
        addDockWidget(Qt::LeftDockWidgetArea, workflowDock_);
    }
    connect(&AppTheme::instance(), &AppTheme::changed, workflowPanel_, &WorkflowPanel::applyTheme);
    connect(workflowPanel_, &WorkflowPanel::stepClicked, this, [this](int step) {
        static const char* hints[] = {
            QT_TR_NOOP("Menu Fichier ▸ Ouvrir une image pour commencer."),
            QT_TR_NOOP("Menu Segmentation ▸ Segmenter l'image."),
            QT_TR_NOOP("Sélectionnez une région, puis Segmentation ▸ Vectoriser."),
            QT_TR_NOOP("Menu Broderie ▸ Numérisation automatique, ou créez un objet."),
            QT_TR_NOOP("Appuyez sur F5 (Analyse) pour vérifier le motif."),
            QT_TR_NOOP("Menu Fichier ▸ Exporter en DST.")};
        statusBar()->showMessage(tr(hints[step]), 6000);
    });
}

void MainWindow::refreshWorkflow() {
    if (workflowPanel_ == nullptr) {
        return;
    }
    using S = WorkflowPanel::State;
    std::array<S, WorkflowPanel::kStepCount> st{};
    const bool img = project_.hasImage();
    const bool seg = project_.segmentation.has_value();
    const bool vec = !project_.vector_objects.empty();
    const bool emb = !project_.embroidery_objects.empty();
    const bool seq = sequence_.has_value();

    st[0] = img ? S::Done : S::NotStarted;
    st[1] = !img ? S::NotStarted : (seg ? S::Done : S::Available);
    st[2] = !seg ? S::NotStarted : (vec ? S::Done : S::Available);
    st[3] = emb ? S::Done : ((vec || seg) ? S::Available : S::NotStarted);
    st[4] = seq ? S::Available : S::NotStarted;
    st[5] = seq ? S::Available : S::NotStarted;
    workflowPanel_->setStates(st);
}

void MainWindow::buildDocumentPanel() {
    documentDock_ = new QDockWidget(tr("Document"), this);
    documentDock_->setObjectName(QStringLiteral("documentDock"));
    documentDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    documentPanel_ = new DocumentPanel(documentDock_);
    documentDock_->setWidget(documentPanel_);
    addDockWidget(Qt::LeftDockWidgetArea, documentDock_);
    if (orderDock_ != nullptr) {
        tabifyDockWidget(documentDock_, orderDock_);  // Document et Ordre en onglets
        documentDock_->raise();
    }

    connect(documentPanel_, &DocumentPanel::embroiderySelected, this, [this](ObjectId id) {
        selectedEmbroidery_ = id;
        selectedRegion_.reset();
        if (const auto* e = project_.findEmbroidery(id);
            e != nullptr && project_.findObject(e->source_vector) != nullptr) {
            selectedObject_ = e->source_vector;  // met en évidence la forme au canevas
        } else {
            selectedObject_.reset();
        }
        displayImage(processed_);
        updateActions();
    });
    connect(documentPanel_, &DocumentPanel::regionSelected, this, [this](RegionId id) {
        selectedRegion_ = id;
        selectedObject_.reset();
        selectedEmbroidery_.reset();
        displayImage(processed_);
        updateActions();
    });
}

void MainWindow::refreshDocumentPanel() {
    if (documentPanel_ == nullptr) {
        return;
    }
    documentPanel_->refresh(project_, editStates_);
    documentDock_->setVisible(project_.hasImage() || !project_.embroidery_objects.empty());
    syncDocumentSelection();
}

void MainWindow::syncDocumentSelection() {
    if (documentPanel_ == nullptr) {
        return;
    }
    const document::EmbroideryObject* emb = resolveSelectedEmbroidery();
    if (emb != nullptr) {
        documentPanel_->syncSelection(DocumentPanel::Kind::Embroidery, emb->id.value);
    } else if (selectedRegion_) {
        documentPanel_->syncSelection(DocumentPanel::Kind::Region, selectedRegion_->value);
    } else {
        documentPanel_->syncSelection(DocumentPanel::Kind::None, 0);
    }
}

void MainWindow::buildPropertiesPanel() {
    propertiesDock_ = new QDockWidget(tr("Propriétés"), this);
    propertiesDock_->setObjectName(QStringLiteral("propertiesDock"));
    propertiesDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    propertiesPanel_ = new PropertiesPanel(propertiesDock_);
    propertiesDock_->setWidget(propertiesPanel_);
    addDockWidget(Qt::RightDockWidgetArea, propertiesDock_);

    // Édition d'un paramètre -> commande annulable -> régénération.
    connect(propertiesPanel_, &PropertiesPanel::paramsEdited, this,
            [this](ObjectId id, document::StitchParams params) {
                undoStack_.execute(
                    std::make_unique<commands::SetStitchParamsCommand>(id, std::move(params)),
                    project_);
                refreshImage();
                // updateActions() (pas updateInspector() seul) : un changement de
                // paramètres peut faire passer un objet retouché à Dirty (Lot 8.2)
                // -- undo/redo, mode d'édition des points et indicateurs doivent
                // suivre. Ne reconstruit PAS le formulaire de paramètres (qui
                // perdrait le focus en cours de frappe) : le garde-fou
                // kind==inspectedKind_/id==inspectedId_ dans updateInspector()
                // s'applique toujours, seul l'état Clean/ManuallyEdited/Dirty
                // (mis à jour avant ce garde-fou) peut changer ici.
                updateActions();
            });
    connect(propertiesPanel_, &PropertiesPanel::discardOverridesRequested, this,
            &MainWindow::discardOverrides);
}

void MainWindow::updateInspector() {
    if (propertiesPanel_ == nullptr) {
        return;
    }
    // Cible : objet de broderie (dock Ordre) > remplissage rattaché à l'objet
    // vectoriel sélectionné > objet vectoriel > région > rien.
    const document::EmbroideryObject* emb = resolveSelectedEmbroidery();

    int kind = -1;
    std::uint64_t id = 0;
    if (emb != nullptr) {
        kind = 0;
        id = emb->id.value;
    } else if (selectedObject_) {
        kind = 1;
        id = selectedObject_->value;
    } else if (selectedRegion_ && project_.segmentation) {
        kind = 2;
        id = selectedRegion_->value;
    }

    // Indicateur Clean/ManuallyEdited/Dirty (Lot 8.2) : mis à jour à CHAQUE
    // appel, y compris quand kind/id n'ont pas changé (une retouche/undo/redo
    // peut faire évoluer l'état sans changer la sélection) -- volontairement
    // AVANT le "ne reconstruit que si la sélection a changé" ci-dessous, pour
    // ne jamais dépendre de lui.
    propertiesPanel_->setEditState(emb != nullptr ? std::optional<ObjectId>(emb->id) : std::nullopt,
                                   emb != nullptr ? editStateOf(emb->id)
                                                  : stitch_generation::ObjectEditState::Clean);

    // Ne reconstruit que si la sélection a changé (n'interrompt pas une édition).
    if (kind == inspectedKind_ && id == inspectedId_) {
        return;
    }
    inspectedKind_ = kind;
    inspectedId_ = id;

    if (kind == 0) {
        propertiesPanel_->showEmbroidery(*emb);
    } else if (kind == 1) {
        const auto* vec = project_.findObject(*selectedObject_);
        int nodes = 0;
        if (vec != nullptr) {
            for (const auto& set : vec->paths) {
                nodes += static_cast<int>(set.outer.nodes.size());
                for (const auto& h : set.holes) nodes += static_cast<int>(h.nodes.size());
            }
        }
        propertiesPanel_->showInfo(
            vec != nullptr ? QString::fromStdString(vec->name) : tr("Objet vectoriel"),
            tr("Objet vectoriel — %1 morceau(x), %2 nœud(s).\n"
               "Créez un objet de broderie (menu Broderie) pour régler la couture.")
                .arg(vec != nullptr ? vec->paths.size() : 0)
                .arg(nodes));
    } else if (kind == 2) {
        const auto* region = project_.segmentation->find(*selectedRegion_);
        if (region != nullptr) {
            const double mmPerPx = project_.mm_per_px.value;
            const double areaMm2 =
                region->pixel_count * mmPerPx * mmPerPx;
            propertiesPanel_->showInfo(
                tr("Région %1").arg(region->id.value),
                tr("Aire : %1 mm²   ·   %2 pixels\nCouleur : #%3%4%5")
                    .arg(areaMm2, 0, 'f', 1)
                    .arg(region->pixel_count)
                    .arg(region->rgb[0], 2, 16, QLatin1Char('0'))
                    .arg(region->rgb[1], 2, 16, QLatin1Char('0'))
                    .arg(region->rgb[2], 2, 16, QLatin1Char('0')));
        }
    } else {
        propertiesPanel_->showInfo(
            tr("Aucune sélection"),
            tr("Sélectionnez une région, un objet vectoriel ou un objet de broderie."));
    }
}

void MainWindow::buildAnalysisPanel() {
    analysisDock_ = new QDockWidget(tr("Analyse"), this);
    analysisDock_->setObjectName(QStringLiteral("analysisDock"));
    analysisDock_->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    analysisList_ = new QListWidget(analysisDock_);
    analysisDock_->setWidget(analysisList_);
    addDockWidget(Qt::RightDockWidgetArea, analysisDock_);
    analysisDock_->hide();

    // Double-clic sur un problème : centre la vue sur sa localisation.
    connect(analysisList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        const QPointF mm = item->data(Qt::UserRole).toPointF();
        if (!mm.isNull()) {
            view_->centerOn(mm);
        }
    });

    auto* analyseMenu = menuBar()->addMenu(tr("A&nalyse"));
    analyzeAct_ = analyseMenu->addAction(tr("&Analyser le motif"));
    analyzeAct_->setShortcut(QKeySequence(Qt::Key_F5));
    connect(analyzeAct_, &QAction::triggered, this, &MainWindow::runAnalysis);
}

void MainWindow::runAnalysis() {
    if (!sequence_) {
        QMessageBox::information(this, tr("Analyse"),
                                 tr("Générez d'abord des points de broderie."));
        return;
    }
    stitch_analysis::AnalysisOptions opts;
    const document::Canvas& canvas = project_.canvas;  // cadre défini par l'utilisateur
    opts.hoop = stitch::BoundsUm{Vec2um{Micrometers{-canvas.width.value / 2},
                                        Micrometers{-canvas.height.value / 2}},
                                 Vec2um{Micrometers{canvas.width.value / 2},
                                        Micrometers{canvas.height.value / 2}}};
    const auto findings = stitch_analysis::analyze(*sequence_, opts);

    analysisList_->clear();
    if (findings.empty()) {
        analysisList_->addItem(tr("✓ Aucun problème détecté."));
    } else {
        for (const auto& f : findings) {
            const QString prefix = f.severity == stitch_analysis::Severity::Error ? tr("⛔ ")
                                   : f.severity == stitch_analysis::Severity::Warning
                                       ? tr("⚠ ")
                                       : tr("ℹ ");
            auto* item = new QListWidgetItem(prefix + QString::fromStdString(f.message));
            item->setData(Qt::UserRole,
                          QPointF(to_millimeters(f.location.x).value,
                                  -to_millimeters(f.location.y).value));
            analysisList_->addItem(item);
        }
    }
    analysisDock_->show();
    analysisDock_->raise();
    statusBar()->showMessage(tr("Analyse : %1 problème(s) détecté(s)").arg(findings.size()));
}

Vec2um MainWindow::embroideryCentroid(const document::EmbroideryObject& object) const {
    // Satin : moyenne des nœuds des deux rails. Autres : centre du contour
    // extérieur du premier morceau de l'objet vectoriel source.
    std::int64_t sx = 0;
    std::int64_t sy = 0;
    std::int64_t n = 0;
    const auto accumulate = [&](const geometry::Path& path) {
        for (const auto& node : path.nodes) {
            sx += node.pos.x.value;
            sy += node.pos.y.value;
            ++n;
        }
    };
    if (const auto* satin = std::get_if<document::SatinParams>(&object.params)) {
        accumulate(satin->rail_a);
        accumulate(satin->rail_b);
    } else {
        for (const auto& vec : project_.vector_objects) {
            if (vec.id == object.source_vector && !vec.paths.empty()) {
                accumulate(vec.paths.front().outer);
                break;
            }
        }
    }
    if (n == 0) {
        return {};
    }
    return Vec2um{Micrometers{static_cast<std::int32_t>(sx / n)},
                  Micrometers{static_cast<std::int32_t>(sy / n)}};
}

void MainWindow::buildOrderPanel() {
    orderDock_ = new QDockWidget(tr("Ordre de couture"), this);
    orderDock_->setObjectName(QStringLiteral("orderDock"));
    orderDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    auto* panel = new QWidget(orderDock_);
    auto* layout = new QVBoxLayout(panel);

    orderList_ = new QListWidget(panel);
    layout->addWidget(orderList_, 1);
    connect(orderList_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && row < static_cast<int>(project_.embroidery_objects.size())) {
            selectedObject_.reset();  // sélection d'objet vectoriel distincte
            selectedEmbroidery_ = project_.embroidery_objects[static_cast<std::size_t>(row)].id;
            updateActions();
            displayImage(processed_);
        } else {
            selectedEmbroidery_.reset();
            updateActions();
        }
    });

    auto* buttons = new QHBoxLayout();
    auto* upBtn = new QPushButton(tr("↑ Monter"), panel);
    auto* downBtn = new QPushButton(tr("↓ Descendre"), panel);
    auto* lockBtn = new QPushButton(tr("🔒 Verrou"), panel);
    connect(upBtn, &QPushButton::clicked, this, &MainWindow::moveObjectUp);
    connect(downBtn, &QPushButton::clicked, this, &MainWindow::moveObjectDown);
    connect(lockBtn, &QPushButton::clicked, this, &MainWindow::toggleObjectLock);
    buttons->addWidget(upBtn);
    buttons->addWidget(downBtn);
    buttons->addWidget(lockBtn);
    layout->addLayout(buttons);

    orderStrategyCombo_ = new QComboBox(panel);
    orderStrategyCombo_->addItem(tr("Par couleur"),
                                 static_cast<int>(optimization::OrderStrategy::ByColor));
    orderStrategyCombo_->addItem(tr("Par proximité"),
                                 static_cast<int>(optimization::OrderStrategy::ByProximity));
    orderStrategyCombo_->addItem(tr("Couleur puis proximité"),
                                 static_cast<int>(optimization::OrderStrategy::ColorThenProximity));
    layout->addWidget(orderStrategyCombo_);
    auto* applyBtn = new QPushButton(tr("Optimiser l'ordre"), panel);
    connect(applyBtn, &QPushButton::clicked, this, &MainWindow::applyOrderStrategy);
    layout->addWidget(applyBtn);

    orderCostLabel_ = new QLabel(panel);
    layout->addWidget(orderCostLabel_);

    orderDock_->setWidget(panel);
    addDockWidget(Qt::LeftDockWidgetArea, orderDock_);
    orderDock_->hide();
}

void MainWindow::refreshOrderPanel() {
    if (orderList_ == nullptr) {
        return;
    }
    const int previousRow = orderList_->currentRow();
    orderList_->clear();
    std::vector<optimization::OrderItem> items;
    for (const auto& obj : project_.embroidery_objects) {
        QString label = QString::fromStdString(obj.name);
        if (obj.locked) {
            label = tr("🔒 ") + label;
        }
        auto* item = new QListWidgetItem(label);
        QPixmap swatch(12, 12);
        swatch.fill(QColor(obj.rgb[0], obj.rgb[1], obj.rgb[2]));
        item->setIcon(QIcon(swatch));
        orderList_->addItem(item);
        items.push_back({obj.id, obj.rgb, embroideryCentroid(obj), obj.locked});
    }
    if (previousRow >= 0 && previousRow < orderList_->count()) {
        orderList_->setCurrentRow(previousRow);
    }

    const auto cost = optimization::compute_cost(items);
    orderCostLabel_->setText(tr("Trajet : %1 mm — %2 changement(s) de fil")
                                 .arg(cost.travel_um / 1000.0, 0, 'f', 1)
                                 .arg(cost.color_changes));
    orderDock_->setVisible(!project_.embroidery_objects.empty());
}

int MainWindow::stitchTypeIndex(const document::EmbroideryObject& object) {
    return object.is_tatami() ? 1 : object.is_satin() ? 2 : 0;
}

double MainWindow::regionAreaMm2(const document::EmbroideryObject& object) const {
    const auto* vec = project_.findObject(object.source_vector);
    if (vec == nullptr) {
        return 0.0;
    }
    const auto shoelace_um2 = [](const geometry::Path& path) {
        double a = 0.0;
        const auto& n = path.nodes;
        for (std::size_t i = 0; i < n.size(); ++i) {
            const auto& p0 = n[i].pos;
            const auto& p1 = n[(i + 1) % n.size()].pos;
            a += static_cast<double>(p0.x.value) * p1.y.value -
                 static_cast<double>(p1.x.value) * p0.y.value;
        }
        return std::abs(a) / 2.0;
    };
    double area_um2 = 0.0;
    for (const auto& set : vec->paths) {
        area_um2 += shoelace_um2(set.outer);
        for (const auto& hole : set.holes) {
            area_um2 -= shoelace_um2(hole);
        }
    }
    return std::max(0.0, area_um2) / 1e6;
}

bool MainWindow::objectPassesFilter(const document::EmbroideryObject& object) const {
    if (!object.visible) {
        return false;
    }
    if (!showType_[static_cast<std::size_t>(stitchTypeIndex(object))]) {
        return false;
    }
    const std::uint32_t key = (static_cast<std::uint32_t>(object.rgb[0]) << 16) |
                              (static_cast<std::uint32_t>(object.rgb[1]) << 8) |
                              static_cast<std::uint32_t>(object.rgb[2]);
    if (hiddenColors_.count(key) != 0) {
        return false;
    }
    if (minAreaMm2_ > 0.0 && regionAreaMm2(object) < minAreaMm2_) {
        return false;
    }
    return true;
}

void MainWindow::buildFilterPanel() {
    filterDock_ = new QDockWidget(tr("Filtres d'affichage"), this);
    filterDock_->setObjectName(QStringLiteral("filterDock"));
    filterDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    auto* panel = new QWidget(filterDock_);
    auto* layout = new QVBoxLayout(panel);

    layout->addWidget(new QLabel(tr("Types de points :"), panel));
    const QString names[] = {tr("Contour"), tr("Tatami"), tr("Satin")};
    for (int t = 0; t < 3; ++t) {
        auto* check = new QCheckBox(names[t], panel);
        check->setChecked(true);
        connect(check, &QCheckBox::toggled, this, [this, t](bool on) {
            showType_[static_cast<std::size_t>(t)] = on;
            displayImage(processed_);
        });
        typeChecks_[static_cast<std::size_t>(t)] = check;
        layout->addWidget(check);
    }

    layout->addSpacing(6);
    layout->addWidget(new QLabel(tr("Taille min. des zones :"), panel));
    minAreaSpin_ = new QDoubleSpinBox(panel);
    minAreaSpin_->setRange(0.0, 10'000.0);
    minAreaSpin_->setDecimals(1);
    minAreaSpin_->setSuffix(tr(" mm²"));
    minAreaSpin_->setValue(0.0);
    connect(minAreaSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        minAreaMm2_ = v;
        displayImage(processed_);
    });
    layout->addWidget(minAreaSpin_);

    layout->addSpacing(6);
    layout->addWidget(new QLabel(tr("Couleurs de fil :"), panel));
    auto* colorContainer = new QWidget(panel);
    colorFilterLayout_ = new QVBoxLayout(colorContainer);
    colorFilterLayout_->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(colorContainer);

    layout->addStretch(1);
    filterDock_->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, filterDock_);
    filterDock_->hide();
}

void MainWindow::refreshFilterPanel() {
    if (colorFilterLayout_ == nullptr) {
        return;
    }
    // Vide la liste de couleurs.
    while (QLayoutItem* item = colorFilterLayout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    // Couleurs distinctes (ordre stable : première apparition).
    std::vector<std::array<std::uint8_t, 3>> colors;
    for (const auto& emb : project_.embroidery_objects) {
        if (std::find(colors.begin(), colors.end(), emb.rgb) == colors.end()) {
            colors.push_back(emb.rgb);
        }
    }
    for (const auto& rgb : colors) {
        const std::uint32_t key = (static_cast<std::uint32_t>(rgb[0]) << 16) |
                                  (static_cast<std::uint32_t>(rgb[1]) << 8) |
                                  static_cast<std::uint32_t>(rgb[2]);
        auto* check = new QCheckBox();
        check->setChecked(hiddenColors_.count(key) == 0);
        QPixmap swatch(12, 12);
        swatch.fill(QColor(rgb[0], rgb[1], rgb[2]));
        check->setIcon(QIcon(swatch));
        check->setText(QStringLiteral("#%1%2%3")
                           .arg(rgb[0], 2, 16, QLatin1Char('0'))
                           .arg(rgb[1], 2, 16, QLatin1Char('0'))
                           .arg(rgb[2], 2, 16, QLatin1Char('0')));
        connect(check, &QCheckBox::toggled, this, [this, key](bool on) {
            if (on) {
                hiddenColors_.erase(key);
            } else {
                hiddenColors_.insert(key);
            }
            displayImage(processed_);
        });
        colorFilterLayout_->addWidget(check);
    }
    filterDock_->setVisible(!project_.embroidery_objects.empty());
}

void MainWindow::moveObjectUp() {
    const int row = orderList_->currentRow();
    if (row <= 0) {
        return;
    }
    std::vector<ObjectId> order;
    for (const auto& obj : project_.embroidery_objects) {
        order.push_back(obj.id);
    }
    std::swap(order[static_cast<std::size_t>(row)], order[static_cast<std::size_t>(row - 1)]);
    undoStack_.execute(std::make_unique<commands::ReorderEmbroideryCommand>(order), project_);
    refreshImage();
    refreshOrderPanel();
    orderList_->setCurrentRow(row - 1);
    updateActions();
}

void MainWindow::moveObjectDown() {
    const int row = orderList_->currentRow();
    if (row < 0 || row >= static_cast<int>(project_.embroidery_objects.size()) - 1) {
        return;
    }
    std::vector<ObjectId> order;
    for (const auto& obj : project_.embroidery_objects) {
        order.push_back(obj.id);
    }
    std::swap(order[static_cast<std::size_t>(row)], order[static_cast<std::size_t>(row + 1)]);
    undoStack_.execute(std::make_unique<commands::ReorderEmbroideryCommand>(order), project_);
    refreshImage();
    refreshOrderPanel();
    orderList_->setCurrentRow(row + 1);
    updateActions();
}

void MainWindow::toggleObjectLock() {
    const int row = orderList_->currentRow();
    if (row < 0 || row >= static_cast<int>(project_.embroidery_objects.size())) {
        return;
    }
    const auto& obj = project_.embroidery_objects[static_cast<std::size_t>(row)];
    undoStack_.execute(
        std::make_unique<commands::SetEmbroideryLockCommand>(obj.id, !obj.locked), project_);
    refreshOrderPanel();
    updateActions();
}

void MainWindow::applyOrderStrategy() {
    if (project_.embroidery_objects.size() < 2) {
        return;
    }
    std::vector<optimization::OrderItem> items;
    for (const auto& obj : project_.embroidery_objects) {
        items.push_back({obj.id, obj.rgb, embroideryCentroid(obj), obj.locked});
    }
    const auto strategy = static_cast<optimization::OrderStrategy>(
        orderStrategyCombo_->currentData().toInt());
    const auto before = optimization::compute_cost(items);
    const auto order = optimization::optimize_order(items, strategy);

    undoStack_.execute(std::make_unique<commands::ReorderEmbroideryCommand>(order), project_);
    refreshImage();
    refreshOrderPanel();
    updateActions();

    // Coût après (recalculé sur le nouvel ordre).
    std::vector<optimization::OrderItem> reordered;
    for (const auto& obj : project_.embroidery_objects) {
        reordered.push_back({obj.id, obj.rgb, embroideryCentroid(obj), obj.locked});
    }
    const auto after = optimization::compute_cost(reordered);
    statusBar()->showMessage(tr("Ordre optimisé : trajet %1 → %2 mm, changements de fil %3 → %4")
                                 .arg(before.travel_um / 1000.0, 0, 'f', 1)
                                 .arg(after.travel_um / 1000.0, 0, 'f', 1)
                                 .arg(before.color_changes)
                                 .arg(after.color_changes));
}

void MainWindow::buildSimulationToolbar() {
    simToolbar_ = addToolBar(tr("Simulation"));
    simToolbar_->setObjectName(QStringLiteral("simToolbar"));
    simToolbar_->setMovable(false);

    simPlayAct_ = simToolbar_->addAction(tr("▶ Lecture"));
    simPlayAct_->setCheckable(true);
    connect(simPlayAct_, &QAction::toggled, this, &MainWindow::toggleSimulation);

    simSlider_ = new QSlider(Qt::Horizontal, simToolbar_);
    simSlider_->setMinimum(0);
    simSlider_->setEnabled(false);
    connect(simSlider_, &QSlider::valueChanged, this, &MainWindow::onSimSliderMoved);
    simToolbar_->addWidget(simSlider_);

    simLabel_ = new QLabel(tr("— / —"), simToolbar_);
    simLabel_->setMinimumWidth(120);
    simToolbar_->addWidget(simLabel_);

    simTimer_ = new QTimer(this);
    simTimer_->setInterval(16);  // ~60 pas/seconde
    connect(simTimer_, &QTimer::timeout, this, &MainWindow::onSimTick);

    // Zone réservée : la barre reste visible (contrôles grisés sans séquence)
    // pour ne pas faire sauter la mise en page à l'apparition des points.
    updateSimulationRange();
}

void MainWindow::updateSimulationRange() {
    const bool hasSeq = sequence_ && !sequence_->commands.empty();
    simPlayAct_->setEnabled(hasSeq);
    simSlider_->setEnabled(hasSeq);
    if (!hasSeq) {
        simTimer_->stop();
        simPlayAct_->setChecked(false);
        simStep_ = -1;
        simLabel_->setText(tr("— / —"));
        return;
    }
    const int n = static_cast<int>(sequence_->commands.size()) - 1;
    simSlider_->blockSignals(true);
    simSlider_->setMaximum(n);
    simSlider_->setEnabled(true);
    if (simStep_ > n || simStep_ < 0) {
        simStep_ = -1;  // hors simulation : tout affiché
        simSlider_->setValue(n);
    }
    simSlider_->blockSignals(false);
    simLabel_->setText(tr("%1 / %2").arg(simulating() ? simStep_ : n).arg(n));
}

void MainWindow::toggleSimulation(/* play */) {
    if (!sequence_ || sequence_->commands.empty()) {
        simPlayAct_->setChecked(false);
        return;
    }
    if (simPlayAct_->isChecked()) {
        // Démarre (ou redémarre depuis le début si on était à la fin).
        if (!simulating() || simStep_ >= static_cast<int>(sequence_->commands.size()) - 1) {
            simStep_ = 0;
        }
        simPlayAct_->setText(tr("⏸ Pause"));
        simTimer_->start();
    } else {
        simPlayAct_->setText(tr("▶ Lecture"));
        simTimer_->stop();
    }
}

void MainWindow::onSimTick() {
    if (!sequence_) {
        simTimer_->stop();
        return;
    }
    const int n = static_cast<int>(sequence_->commands.size()) - 1;
    // Avance proportionnellement à la taille (motif long = plus rapide).
    simStep_ = std::min(n, simStep_ + std::max(1, (n + 1) / 400));
    simSlider_->blockSignals(true);
    simSlider_->setValue(simStep_);
    simSlider_->blockSignals(false);
    simLabel_->setText(tr("%1 / %2").arg(simStep_).arg(n));
    renderStitches();  // seule la couche points change pendant la simulation
    if (simStep_ >= n) {
        simTimer_->stop();
        simPlayAct_->setChecked(false);
        simPlayAct_->setText(tr("▶ Lecture"));
    }
}

void MainWindow::onSimSliderMoved(int value) {
    simStep_ = value;
    const int n = static_cast<int>(sequence_ ? sequence_->commands.size() : 1) - 1;
    simLabel_->setText(tr("%1 / %2").arg(value).arg(n));
    renderStitches();
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

void MainWindow::saveProject() {
    if (!project_.hasImage() && project_.vector_objects.empty()) {
        QMessageBox::information(this, tr("Rien à enregistrer"),
                                 tr("Ouvrez une image et créez des objets d'abord."));
        return;
    }
    const QString file = QFileDialog::getSaveFileName(this, tr("Enregistrer le projet"), QString(),
                                                      tr("Projet OpenStitch (*.osp)"));
    if (file.isEmpty()) {
        return;
    }
    const auto written = project_io::save_project(std::filesystem::path(file.toStdWString()),
                                                  project_);
    if (!written) {
        QMessageBox::warning(this, tr("Enregistrement impossible"),
                             QString::fromStdString(written.error().message));
        return;
    }
    statusBar()->showMessage(tr("Projet enregistré : %1").arg(QFileInfo(file).fileName()));
    setWindowModified(false);
}

void MainWindow::loadProject() {
    const QString file = QFileDialog::getOpenFileName(this, tr("Ouvrir un projet"), QString(),
                                                      tr("Projet OpenStitch (*.osp)"));
    if (file.isEmpty()) {
        return;
    }
    auto loaded = project_io::load_project(std::filesystem::path(file.toStdWString()));
    if (!loaded) {
        QMessageBox::warning(this, tr("Ouverture impossible"),
                             QString::fromStdString(loaded.error().message));
        return;
    }
    applyLoadedProject(std::move(*loaded));
    statusBar()->showMessage(tr("Projet ouvert : %1 — %2 objet(s) vectoriel(s), %3 objet(s) de "
                                "broderie")
                                 .arg(QFileInfo(file).fileName())
                                 .arg(project_.vector_objects.size())
                                 .arg(project_.embroidery_objects.size()));
}

void MainWindow::applyLoadedProject(document::Project project) {
    project_ = std::move(project);
    ++documentGeneration_;  // projet remplacé : invalide les commandes différées en vol
    undoStack_.clear();
    sequence_.reset();
    sequenceImported_ = false;
    selectedRegion_.reset();
    selectedObject_.reset();
    selectedEmbroidery_.reset();
    // Nouveau projet chargé : sortie propre du mode d'édition des points
    // (Lot 8.2), même raison que openImage() ci-dessus.
    if (stitchEditModeAct_ != nullptr) {
        QSignalBlocker block(stitchEditModeAct_);
        stitchEditModeAct_->setChecked(false);
    }
    stitchEditTarget_.reset();
    stitchEditView_.reset();
    refreshImage();
    view_->fitCanvas();
    updateActions();
    setWindowModified(false);  // projet fraîchement chargé = propre
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

    // Résumé pré-export : décision réelle -> dialogue de confirmation.
    const auto stats = stitch::compute_stats(*sequence_);
    const double wMm = to_millimeters(stats.bounds.max.x - stats.bounds.min.x).value;
    const double hMm = to_millimeters(stats.bounds.max.y - stats.bounds.min.y).value;
    const auto& cv = project_.canvas;
    const bool overflow = stats.bounds.min.x.value < -cv.width.value / 2 ||
                          stats.bounds.max.x.value > cv.width.value / 2 ||
                          stats.bounds.min.y.value < -cv.height.value / 2 ||
                          stats.bounds.max.y.value > cv.height.value / 2;
    QString summary =
        tr("Dimensions : %1 × %2 mm\nPoints : %3\nSauts : %4\nCoupes : %5\n"
           "Changements de couleur : %6\nFil estimé : %7 m\nCadre : %8 × %9 mm\nFichier : %10")
            .arg(wMm, 0, 'f', 1)
            .arg(hMm, 0, 'f', 1)
            .arg(stats.stitches)
            .arg(stats.jumps)
            .arg(stats.trims)
            .arg(stats.color_changes)
            .arg(stats.thread_length_um / 1e9, 0, 'f', 2)
            .arg(to_millimeters(cv.width).value, 0, 'f', 0)
            .arg(to_millimeters(cv.height).value, 0, 'f', 0)
            .arg(QFileInfo(file).fileName());
    if (overflow) {
        summary += tr("\n\nAttention : le motif dépasse le cadre.");
    }
    summary += tr("\n\nLe DST ne conserve pas les objets éditables.");
    if (!project_.embroidery_objects.empty()) {
        summary += tr(" Pensez à enregistrer aussi le projet (.osp).");
    }
    QMessageBox box(this);
    box.setWindowTitle(tr("Exporter en DST"));
    box.setText(tr("Résumé de l'export"));
    box.setInformativeText(summary);
    box.setIcon(overflow ? QMessageBox::Warning : QMessageBox::Information);
    box.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Ok);
    if (box.exec() != QMessageBox::Ok) {
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
    statusBar()->showMessage(tr("DST exporté : %1 (%2 points).")
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
    ++documentGeneration_;  // document remplacé : invalide les commandes différées en vol
    undoStack_.clear();
    processed_ = {};
    selectedRegion_.reset();
    selectedObject_.reset();
    sequence_ = std::move(*seq);
    sequenceImported_ = true;
    showStitchesAct_->setChecked(true);
    updateSimulationRange();
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
    // En mode « Déplacer la vue », un clic ne sélectionne rien.
    if (currentTool_ == Tool::Pan) {
        return;
    }
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
            selectedEmbroidery_.reset();  // la sélection au canevas prime
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
            // updateActions() manquait ici (contrairement aux autres chemins de
            // sélection) : nécessaire pour que le mode d'édition des points
            // (Lot 8.2) sorte proprement quand la sélection est perdue par un
            // clic dans le vide plutôt que reportée sur un autre objet.
            updateActions();
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
    createSatinAct_->setEnabled(selectedObject_.has_value());
    autoSatinAct_->setEnabled(selectedObject_.has_value());
    fillAngleAct_->setEnabled(currentFillObject() != nullptr);
    convertSatinAct_->setEnabled(std::any_of(project_.embroidery_objects.begin(),
                                             project_.embroidery_objects.end(),
                                             [](const auto& e) { return e.is_satin(); }));
    statsAct_->setEnabled(sequence_.has_value());
    exportDstAct_->setEnabled(sequence_.has_value());
    const bool hasSelection = selectedRegion_.has_value() && project_.segmentation.has_value();
    for (QAction* act : regionActions_) {
        act->setEnabled(hasSelection);
    }
    if (!mergeAct_->isEnabled()) {
        mergeAct_->setChecked(false);
    }

    // Mode d'édition des points (Lot 8.2) : sortie propre dès que le contexte
    // qui l'a activé n'est plus valide -- sélection perdue, objet différent
    // sélectionné, ou objet devenu Dirty entre-temps (satin routé voisin,
    // suppression d'un objet adjacent…). `editStates_` est déjà à jour (calculé
    // par le refreshImage() qui précède systématiquement cet appel), donc
    // aucun appel au cœur ici : uniquement des lectures.
    document::EmbroideryObject* stitchEditEmb = resolveSelectedEmbroidery();
    const stitch_generation::ObjectEditState stitchEditState =
        stitchEditEmb != nullptr ? editStateOf(stitchEditEmb->id)
                                 : stitch_generation::ObjectEditState::Clean;
    const bool stitchEditSameTarget = stitchEditTarget_.has_value() && stitchEditEmb != nullptr &&
                                      *stitchEditTarget_ == stitchEditEmb->id;
    // Trop de points déplaçables (cf. kMaxEditableStitchHandles, renderBase) :
    // le mode ne doit jamais rester actif sans aucune poignée visible --
    // sortie propre + explication, plutôt qu'un mode "actif" muet.
    const bool stitchEditTooManyPoints =
        stitchEditView_.has_value() &&
        static_cast<std::size_t>(std::count_if(stitchEditView_->raw.begin(),
                                                stitchEditView_->raw.end(),
                                                stitch_generation::is_movable_point)) >
            kMaxEditableStitchHandles;
    bool stitchEditNeedsRerender = false;
    if (stitchEditModeAct_->isChecked() &&
        (stitchEditEmb == nullptr || !stitchEditSameTarget ||
         stitchEditState == stitch_generation::ObjectEditState::Dirty ||
         stitchEditTooManyPoints)) {
        if (stitchEditTooManyPoints) {
            statusBar()->showMessage(
                tr("Édition des points désactivée : cet objet compte plus de %1 points de "
                   "couture déplaçables. Réduisez la densité ou la longueur de point pour "
                   "l'éditer point par point.")
                    .arg(kMaxEditableStitchHandles));
        }
        QSignalBlocker block(stitchEditModeAct_);
        stitchEditModeAct_->setChecked(false);
        stitchEditTarget_.reset();
        stitchEditView_.reset();
        stitchEditNeedsRerender = true;
    }
    stitchEditModeAct_->setEnabled(stitchEditEmb != nullptr &&
                                   stitchEditState != stitch_generation::ObjectEditState::Dirty);
    if (stitchEditNeedsRerender) {
        displayImage(processed_);  // retire les poignées de l'objet quitté
    }

    document::EmbroideryObject* satinGuideEmb = resolveSelectedEmbroidery();
    const auto* selectedSatin =
        satinGuideEmb != nullptr
            ? std::get_if<document::SatinParams>(&satinGuideEmb->params)
            : nullptr;
    const bool satinGuideContext = selectedSatin != nullptr && selectedSatin->rungs.size() >= 2;
    const bool satinGuideSameTarget =
        satinGuideTarget_.has_value() && satinGuideEmb != nullptr &&
        *satinGuideTarget_ == satinGuideEmb->id;
    if (satinGuideModeAct_->isChecked() &&
        (!satinGuideContext || !satinGuideSameTarget)) {
        QSignalBlocker block(satinGuideModeAct_);
        satinGuideModeAct_->setChecked(false);
        satinGuideTarget_.reset();
        selectedSatinGuide_.reset();
        displayImage(processed_);
    }
    satinGuideModeAct_->setEnabled(satinGuideContext);
    if (selectedSatin != nullptr && selectedSatinGuide_ &&
        *selectedSatinGuide_ >= selectedSatin->rungs.size()) {
        selectedSatinGuide_.reset();
    }
    const bool satinGuideEditing = satinGuideModeAct_->isChecked() && satinGuideContext;
    addSatinGuideAct_->setEnabled(satinGuideEditing);
    removeSatinGuideAct_->setEnabled(satinGuideEditing && selectedSatinGuide_.has_value() &&
                                     selectedSatin->rungs.size() > 2);

    undoAct_->setEnabled(undoStack_.canUndo());
    redoAct_->setEnabled(undoStack_.canRedo());
    undoAct_->setText(undoStack_.canUndo()
                          ? tr("&Annuler %1").arg(QString::fromStdString(undoStack_.undoName()))
                          : tr("&Annuler"));
    redoAct_->setText(undoStack_.canRedo()
                          ? tr("&Rétablir %1").arg(QString::fromStdString(undoStack_.redoName()))
                          : tr("&Rétablir"));

    updateInspector();
    syncDocumentSelection();
    updateContextToolbar();
    refreshWorkflow();
    updateEmptyState();
}

}  // namespace openstitch::desktop
