// SPDX-License-Identifier: Apache-2.0
// Prévisualisation du tirage du tissu (AdaptivePullCompensation, niveau 3). Aucun calcul métier
// ici : le champ de déplacement, la carte et la séquence « telle que le tissu la restituera »
// viennent de libs/stitch_generation/pull_map.hpp, calculés sur la séquence EFFECTIVE déjà en
// cache (`sequence_`). Deux affichages, jamais écrits dans le document :
//   - « Carte de tirage du tissu » : surimpression colorée du déplacement prédit ;
//   - « Aperçu avec tirage du tissu » : le rendu des points décalés par le tissu, pour voir les
//     trous entre couleurs (voir `renderRealisticStitches`).
// Les coefficients sont empiriques et non calibrés : c'est un indicateur, pas une mesure.
#include <QAction>
#include <QActionGroup>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QImage>
#include <QMenu>
#include <QMenuBar>
#include <QSignalBlocker>
#include <QStatusBar>

#include <cmath>

#include "canvas_view.hpp"
#include "density_overlay.hpp"
#include "main_window.hpp"
#include "openstitch/stitch_generation/predicted_pull.hpp"
#include "openstitch/stitch_generation/pull_map.hpp"
#include "ui_memory.hpp"

namespace openstitch::desktop {

namespace {

const QString kMapKey = QStringLiteral("view/pullMap");
const QString kPreviewKey = QStringLiteral("view/pullPreview");
const QString kFabricKey = QStringLiteral("view/pullFabric");
constexpr Micrometers kMapCell{500};

} // namespace

void MainWindow::buildPullPreviewFeature() {
    pullMapAct_ = new QAction(tr("Carte de tirage du tissu"), this);
    pullMapAct_->setObjectName(QStringLiteral("action_pullMap"));
    pullMapAct_->setCheckable(true);
    pullMapAct_->setToolTip(tr(
        "Surimpression colorée du déplacement PRÉDIT du tissu sous la traction des fils (du bleu "
        "au rouge, en mm). Estimation empirique, non calibrée : un indicateur, pas une mesure."));
    pullMapAct_->setChecked(ui_memory::boolValue(kMapKey, false));

    pullPreviewAct_ = new QAction(tr("Aperçu avec tirage du tissu"), this);
    pullPreviewAct_->setObjectName(QStringLiteral("action_pullPreview"));
    pullPreviewAct_->setCheckable(true);
    pullPreviewAct_->setToolTip(
        tr("Dessine les points tels que le tissu les restituera : chaque couleur suit le tissu "
           "déjà déformé par les précédentes, ce qui fait apparaître les trous entre couleurs. "
           "Estimation non calibrée ; visible au zoom du rendu réaliste."));
    pullPreviewAct_->setChecked(ui_memory::boolValue(kPreviewKey, false));

    auto* fabricMenu = new QMenu(tr("Tissu du tirage"), this);
    fabricMenu->setObjectName(QStringLiteral("menu_pullFabric"));
    auto* group = new QActionGroup(fabricMenu);
    const struct {
        const char* id;
        QString label;
    } kinds[] = {{"default", tr("Tissu du projet / standard")},
                 {"linen", tr("Lin lâche")},
                 {"stable", tr("Bien stabilisé")}};
    const int saved = ui_memory::intValue(kFabricKey, 0, 0, 2);
    int index = 0;
    for (const auto& kind : kinds) {
        auto* act = fabricMenu->addAction(kind.label);
        act->setObjectName(QStringLiteral("action_pullFabric_") + QLatin1String(kind.id));
        act->setCheckable(true);
        act->setActionGroup(group);
        act->setChecked(index == saved);
        connect(act, &QAction::toggled, this, [this, index](bool on) {
            if (on) {
                ui_memory::setIntValue(kFabricKey, index);
                pullMapValid_ = false;
                pullPreviewValid_ = false;
                if (pullMapAct_->isChecked() || pullPreviewAct_->isChecked()) {
                    renderStitches();
                }
            }
        });
        ++index;
    }

    QMenu* viewMenu = nullptr;
    QAction* insertBefore = nullptr;
    for (QMenu* menu : menuBar()->findChildren<QMenu*>()) {
        const auto actions = menu->actions();
        for (int i = 0; i < actions.size(); ++i) {
            if (actions[i]->menu() != nullptr && realisticAct_ != nullptr &&
                actions[i]->menu()->actions().contains(realisticAct_)) {
                viewMenu = menu;
                insertBefore = i + 1 < actions.size() ? actions[i + 1] : nullptr;
            }
        }
    }
    if (viewMenu != nullptr) {
        viewMenu->insertAction(insertBefore, pullMapAct_);
        viewMenu->insertAction(insertBefore, pullPreviewAct_);
        viewMenu->insertMenu(insertBefore, fabricMenu);
    }

    connect(pullMapAct_, &QAction::toggled, this, [this](bool on) {
        ui_memory::setBoolValue(kMapKey, on);
        renderStitches();
    });
    connect(pullPreviewAct_, &QAction::toggled, this, [this](bool on) {
        ui_memory::setBoolValue(kPreviewKey, on);
        renderStitches();
    });
}

stitch_generation::FabricResponse MainWindow::pullFabric() const {
    static const char* const kIds[] = {"default", "linen", "stable"};
    const int choice = ui_memory::intValue(kFabricKey, 0, 0, 2);
    const auto response = stitch_generation::fabric_preset(kIds[choice]);
    // Par défaut, la prévisualisation lit la même matière que la compensation
    // Predicted de l'export ; les deux autres entrées sont des essais explicites.
    if (choice == 0) {
        return stitch_generation::predicted_fabric_response(project_, response);
    }
    return response;
}

// Séquence décalée du tirage prédit, en cache sous la clé (séquence + tissu). Nul si l'aperçu est
// éteint ou s'il n'y a pas de séquence.
const stitch::StitchSequence* MainWindow::pullPreviewSequence() {
    if (pullPreviewAct_ == nullptr || !pullPreviewAct_->isChecked() || !sequence_.has_value()) {
        return nullptr;
    }
    const std::uint64_t key =
        densityKey(*sequence_, 0.0) ^
        (static_cast<std::uint64_t>(ui_memory::intValue(kFabricKey, 0, 0, 2)) + 1) *
            0x9e3779b97f4a7c15ULL;
    if (!pullPreviewValid_ || pullPreviewKey_ != key) {
        pullPreviewSeq_ = stitch_generation::simulate_fabric_pull(*sequence_, pullFabric());
        pullPreviewKey_ = key;
        pullPreviewValid_ = true;
        ++pullComputeCount_;
    }
    return &pullPreviewSeq_;
}

// Appelée par renderStitches() : pose la carte de déplacement (en cache sous la clé séquence +
// tissu).
void MainWindow::refreshPullMapOverlay() {
    if (pullMapAct_ == nullptr || !pullMapAct_->isChecked() || !sequence_.has_value() ||
        simulating() || showStitchesAct_ == nullptr || !showStitchesAct_->isChecked()) {
        return;
    }
    const std::uint64_t key =
        densityKey(*sequence_, 0.0) ^
        (static_cast<std::uint64_t>(ui_memory::intValue(kFabricKey, 0, 0, 2)) + 7) *
            0xc2b2ae3d27d4eb4fULL;
    if (!pullMapValid_ || pullMapKey_ != key) {
        const auto map = stitch_generation::compute_pull_map(*sequence_, pullFabric(), kMapCell);
        const auto raster = stitch_generation::rasterize_pull_map(map);
        pullMapPixmap_ = QPixmap();
        if (raster.width > 0) {
            const QImage image(raster.rgba.data(), raster.width, raster.height, raster.width * 4,
                               QImage::Format_RGBA8888);
            pullMapPixmap_ = QPixmap::fromImage(image); // copie : `raster` est local
            const double cellMm = to_millimeters(map.cell).value;
            pullMapRect_ = QRectF(to_millimeters(map.origin.x).value,
                                  -(to_millimeters(map.origin.y).value + raster.height * cellMm),
                                  raster.width * cellMm, raster.height * cellMm);
            const auto peak = stitch_generation::find_pull_peak(map);
            statusBar()->showMessage(
                tr("Tirage prédit du tissu : jusqu'à %1 mm (estimation non calibrée).")
                    .arg(peak.magnitude_mm, 0, 'f', 2),
                8000);
        }
        pullMapKey_ = key;
        pullMapValid_ = true;
        ++pullComputeCount_;
    }
    if (pullMapPixmap_.isNull()) {
        return;
    }
    auto* item = scene_->addPixmap(pullMapPixmap_);
    item->setTransformationMode(Qt::FastTransformation);
    item->setAcceptedMouseButtons(Qt::NoButton);
    item->setPos(pullMapRect_.x(), pullMapRect_.y());
    item->setScale(pullMapRect_.width() / pullMapPixmap_.width());
    item->setZValue(26); // au-dessus du rendu réaliste (20) et des cartes de densité (25)
    item->setData(0, QStringLiteral("pullMapOverlay"));
    stitchItems_.append(item);
}

} // namespace openstitch::desktop
