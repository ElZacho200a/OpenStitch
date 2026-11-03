// SPDX-License-Identifier: Apache-2.0
// Affichage par passe (HP-VIEW-003), zoom rectangle et 1:1 étalonné (HP-VIEW-007), grille et
// guides configurables (HP-VIEW-010), aperçu en direct pendant la saisie (HP-VIEW-012).
// Aucun calcul métier ici : les passes, la grille et les guides viennent de
// libs/stitch_render (view_display.hpp) ; tout est de l'AFFICHAGE, mémorisé via ui_memory, et ne
// touche jamais le document (donc aucune ICommand).
#include <QAction>
#include <QColorDialog>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QInputDialog>
#include <QMenu>
#include <QPainterPath>
#include <QPen>
#include <QSettings>
#include <QSignalBlocker>

#include <cmath>

#include "canvas_view.hpp"
#include "grid_settings_dialog.hpp"
#include "main_window.hpp"

namespace openstitch::desktop {

namespace {

// Surcouche graphique qui ne capte jamais ni clic ni survol : sa forme de test de collision est
// vide, les objets dessous restent sélectionnables.
class NoHitPathItem : public QGraphicsPathItem {
public:
    using QGraphicsPathItem::QGraphicsPathItem;
    [[nodiscard]] QPainterPath shape() const override { return {}; }
};

} // namespace

void MainWindow::applyViewDisplay() {
    view_->setGridSettings(viewDisplay_.grid);
    view_->setGuides(viewDisplay_.guides);
    view_->setGuidesVisible(viewDisplay_.guidesVisible);
    view_->setGuideColor(viewDisplay_.guideColor);
    view_->setActualSizeCalibration(viewDisplay_.actualPxPerMm);
    viewDisplay_.save();
    emit view_->viewChanged();
}

void MainWindow::buildViewDisplayMenu(QMenu* viewMenu) {
    viewDisplay_ = ViewDisplayState::load();
    // Le pas affiché et le pas d'accrochage sont une seule et même grille.
    if (viewDisplay_.grid.step_mm > 0.0) {
        gridStepMm_ = viewDisplay_.grid.step_mm;
    }

    // --- Zoom rectangle et 1:1 étalonné (HP-VIEW-007) : pas de raccourci (ils sont pris). ---
    zoomRectAct_ = viewMenu->addAction(tr("Zoom rectangle"));
    zoomRectAct_->setObjectName(QStringLiteral("action_zoomRect"));
    zoomRectAct_->setCheckable(true);
    zoomRectAct_->setToolTip(tr("Glissez un rectangle sur le canevas pour cadrer la vue dessus "
                                "(Échap : annuler)."));
    connect(zoomRectAct_, &QAction::toggled, this, [this](bool on) { view_->setZoomRectMode(on); });
    connect(view_, &CanvasView::zoomRectModeEnded, this, [this] {
        const QSignalBlocker block(zoomRectAct_);
        zoomRectAct_->setChecked(false);
    });
    auto* calibrateAct = viewMenu->addAction(tr("Étalonner le zoom 100 %…"));
    calibrateAct->setObjectName(QStringLiteral("action_zoomCalibrate"));
    calibrateAct->setToolTip(tr("Indique la densité de l'écran (pixels par pouce) pour que "
                                "« 100 % » affiche la taille réelle ; 0 = automatique."));
    connect(calibrateAct, &QAction::triggered, this, [this] {
        bool ok = false;
        const double dpi = QInputDialog::getDouble(
            this, tr("Étalonner le zoom 100 %"), tr("Densité de l'écran (pixels par pouce) :"),
            view_->actualSizePxPerMm() * 25.4, 0.0, 2540.0, 1, &ok);
        if (ok) {
            const double pxPerMm = dpi / 25.4;
            viewDisplay_.actualPxPerMm = (pxPerMm >= 1.0 && pxPerMm <= 100.0) ? pxPerMm : 0.0;
            applyViewDisplay();
        }
    });

    // --- Passes de couture (HP-VIEW-003) ---
    auto* passMenu = viewMenu->addMenu(tr("Passes de couture"));
    passMenu->setObjectName(QStringLiteral("menu_passes"));
    const auto addPass = [this, passMenu](const QString& label, const char* name, bool initial,
                                          bool stitch_render::PassDisplay::*flag) {
        auto* act = passMenu->addAction(label);
        act->setObjectName(QString::fromLatin1(name));
        act->setCheckable(true);
        act->setChecked(initial);
        connect(act, &QAction::toggled, this, [this, flag](bool on) {
            viewDisplay_.passes.*flag = on;
            viewDisplay_.save();
            renderStitches();
        });
        return act;
    };
    const auto& ps = viewDisplay_.passes;
    addPass(tr("Sous-couche"), "action_passUnderlay", ps.underlay,
            &stitch_render::PassDisplay::underlay);
    addPass(tr("Couche supérieure"), "action_passTop", ps.top, &stitch_render::PassDisplay::top);
    addPass(tr("Déplacements"), "action_passTravel", ps.travel,
            &stitch_render::PassDisplay::travel);
    addPass(tr("Verrous"), "action_passLock", ps.lock, &stitch_render::PassDisplay::lock);
    addPass(tr("Points manuels"), "action_passManual", ps.manual,
            &stitch_render::PassDisplay::manual);
    passMenu->addSeparator();
    addPass(tr("Une couleur par passe"), "action_passDistinct", ps.distinct_colors,
            &stitch_render::PassDisplay::distinct_colors)
        ->setToolTip(tr("Remplace la couleur du fil par une teinte propre à chaque passe."));
    addPass(tr("Symboles d'entrée et de coupe"), "action_passFunctions", ps.function_markers,
            &stitch_render::PassDisplay::function_markers)
        ->setToolTip(tr("Cercle : début d'un enchaînement de points. Triangle : coupe du fil."));

    // --- Grille et guides (HP-VIEW-010) ---
    auto* gridMenu = viewMenu->addMenu(tr("Grille et guides"));
    gridMenu->setObjectName(QStringLiteral("menu_gridGuides"));
    auto* gridShowAct = gridMenu->addAction(tr("Afficher la grille"));
    gridShowAct->setObjectName(QStringLiteral("action_gridShow"));
    gridShowAct->setCheckable(true);
    gridShowAct->setChecked(viewDisplay_.grid.visible);
    connect(gridShowAct, &QAction::toggled, this, [this](bool on) {
        viewDisplay_.grid.visible = on;
        applyViewDisplay();
    });
    auto* gridSetAct = gridMenu->addAction(tr("Réglages de la grille…"));
    gridSetAct->setObjectName(QStringLiteral("action_gridSettings"));
    connect(gridSetAct, &QAction::triggered, this, [this, gridShowAct] {
        GridSettingsDialog dialog(viewDisplay_.grid, this);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        viewDisplay_.grid = dialog.settings();
        if (viewDisplay_.grid.step_mm > 0.0) {
            gridStepMm_ = viewDisplay_.grid.step_mm; // l'accrochage suit la grille dessinée
            QSettings().setValue(QStringLiteral("edit/gridStepMm"), gridStepMm_);
        }
        {
            const QSignalBlocker block(gridShowAct);
            gridShowAct->setChecked(viewDisplay_.grid.visible);
        }
        applyViewDisplay();
    });
    gridMenu->addSeparator();
    auto* guidesShowAct = gridMenu->addAction(tr("Afficher les guides"));
    guidesShowAct->setObjectName(QStringLiteral("action_guidesShow"));
    guidesShowAct->setCheckable(true);
    guidesShowAct->setChecked(viewDisplay_.guidesVisible);
    connect(guidesShowAct, &QAction::toggled, this, [this](bool on) {
        viewDisplay_.guidesVisible = on;
        applyViewDisplay();
    });
    const auto addGuide = [this](stitch_render::GuideAxis axis) {
        bool ok = false;
        const bool vertical = axis == stitch_render::GuideAxis::Vertical;
        const double mm = QInputDialog::getDouble(
            this, vertical ? tr("Guide vertical") : tr("Guide horizontal"),
            vertical ? tr("Position X du guide (mm) :") : tr("Position Y du guide (mm) :"), 0.0,
            -2000.0, 2000.0, 2, &ok);
        if (!ok || viewDisplay_.guides.size() >= stitch_render::kMaxGuides) {
            return;
        }
        viewDisplay_.guides.push_back(stitch_render::Guide{
            axis, Micrometers{static_cast<std::int32_t>(std::lround(mm * 1000.0))}});
        viewDisplay_.guides = stitch_render::normalized(std::move(viewDisplay_.guides));
        viewDisplay_.guidesVisible = true;
        applyViewDisplay();
    };
    auto* addVAct = gridMenu->addAction(tr("Ajouter un guide vertical…"));
    addVAct->setObjectName(QStringLiteral("action_guideAddVertical"));
    connect(addVAct, &QAction::triggered, this,
            [addGuide] { addGuide(stitch_render::GuideAxis::Vertical); });
    auto* addHAct = gridMenu->addAction(tr("Ajouter un guide horizontal…"));
    addHAct->setObjectName(QStringLiteral("action_guideAddHorizontal"));
    connect(addHAct, &QAction::triggered, this,
            [addGuide] { addGuide(stitch_render::GuideAxis::Horizontal); });
    auto* clearGuidesAct = gridMenu->addAction(tr("Effacer les guides"));
    clearGuidesAct->setObjectName(QStringLiteral("action_guidesClear"));
    connect(clearGuidesAct, &QAction::triggered, this, [this] {
        viewDisplay_.guides.clear();
        applyViewDisplay();
    });
    auto* guideColorAct = gridMenu->addAction(tr("Couleur des guides…"));
    guideColorAct->setObjectName(QStringLiteral("action_guideColor"));
    connect(guideColorAct, &QAction::triggered, this, [this] {
        const QColor c =
            QColorDialog::getColor(viewDisplay_.guideColor, this, tr("Couleur des guides"));
        if (c.isValid()) {
            viewDisplay_.guideColor = c;
            applyViewDisplay();
        }
    });

    // --- Aperçu en direct pendant la saisie (HP-VIEW-012) ---
    auto* liveAct = viewMenu->addAction(tr("Aperçu en direct pendant la saisie"));
    liveAct->setObjectName(QStringLiteral("action_liveTyping"));
    liveAct->setCheckable(true);
    liveAct->setChecked(viewDisplay_.liveTyping);
    liveAct->setToolTip(tr("Les champs numériques de l'inspecteur mettent à jour l'aperçu à chaque "
                           "chiffre tapé (un seul pas d'annulation). S'applique au prochain "
                           "affichage de l'inspecteur."));
    connect(liveAct, &QAction::toggled, this, [this](bool on) {
        viewDisplay_.liveTyping = on;
        viewDisplay_.save();
    });

    applyViewDisplay();
}

void MainWindow::addFunctionMarkers() {
    if (!viewDisplay_.passes.function_markers || !sequence_ || simulating()) {
        return;
    }
    const auto markers = stitch_render::collect_function_markers(*sequence_);
    if (markers.empty()) {
        return;
    }
    constexpr double kRadius = 0.6;
    QPainterPath entries;
    QPainterPath cuts;
    for (const auto& m : markers) {
        const QPointF p(m.x, m.y);
        if (m.kind == stitch_render::FunctionKind::Entry) {
            entries.addEllipse(p, kRadius, kRadius);
        } else {
            cuts.moveTo(p.x(), p.y() - kRadius * 1.2);
            cuts.lineTo(p.x() + kRadius * 1.1, p.y() + kRadius * 0.8);
            cuts.lineTo(p.x() - kRadius * 1.1, p.y() + kRadius * 0.8);
            cuts.closeSubpath();
        }
    }
    const auto add = [this](const QPainterPath& path, const QColor& color, bool filled) {
        if (path.isEmpty()) {
            return;
        }
        QPen pen(color);
        pen.setCosmetic(true);
        pen.setWidth(2);
        auto* item = new NoHitPathItem(path);
        item->setPen(pen);
        item->setBrush(filled ? QBrush(color) : QBrush(Qt::NoBrush));
        item->setZValue(25);
        item->setAcceptedMouseButtons(Qt::NoButton);
        item->setAcceptHoverEvents(false);
        item->setData(0, QStringLiteral("functionMarkers"));
        scene_->addItem(item);
        stitchItems_.append(item);
    };
    add(entries, QColor(0x1E, 0x84, 0x49), false);
    add(cuts, QColor(0xC0, 0x39, 0x2B), true);
}

} // namespace openstitch::desktop
