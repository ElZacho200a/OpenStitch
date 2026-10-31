// SPDX-License-Identifier: Apache-2.0
// Surimpression « Risque tissu » : colore le canevas par zones (vert / orange / rouge) à partir de
// la MÊME analyse que le CLI (`project_ops::measure_fabric_risk`, indice relatif heuristique 0 à
// 100, non calibré sur machine). Aucun calcul métier ici : le calcul tourne hors du fil
// d'interface (BusyIndicator::run) sur un instantané du document, et son résultat (grille de cases
// classées) est mis en cache sous une clé exacte (séquence effective + profil de tissu), donc
// invalidé dès que la séquence change. La carte ne modifie jamais le document.
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "busy_indicator.hpp"
#include "canvas_view.hpp"
#include "fabric_risk_overlay.hpp"
#include "main_window.hpp"
#include "ui_memory.hpp"

namespace openstitch::desktop {

namespace {

const QString kEnabledKey = QStringLiteral("view/fabricRisk");
const QString kProfileKey = QStringLiteral("view/fabricRiskProfile");
constexpr int kRecomputeDelayMs = 150; // regroupe une rafale de modifications
constexpr double kOverlayZ = 23.0;     // sous les sauts à vide (24) et la carte de densité (25)

} // namespace

int MainWindow::fabricRiskProfile() const {
    if (analysisDock_ != nullptr) {
        if (const auto* combo =
                analysisDock_->findChild<QComboBox*>(QStringLiteral("combo_fabricRiskProfile"))) {
            return std::clamp(combo->currentIndex(), 0, 2);
        }
    }
    return ui_memory::intValue(kProfileKey, 0, 0, 2);
}

void MainWindow::buildFabricRiskFeature() {
    // --- Action « Affichage > Risque tissu » -----------------------------------------------------
    fabricRiskAct_ = new QAction(tr("Risque tissu"), this);
    fabricRiskAct_->setObjectName(QStringLiteral("action_fabricRisk"));
    fabricRiskAct_->setCheckable(true);
    fabricRiskAct_->setToolTip(
        tr("Surimpression colorée du risque relatif de tirage du tissu (vert : faible, orange : "
           "modéré, rouge : élevé). Indice heuristique non calibré sur machine, pas une mesure."));
    fabricRiskAct_->setChecked(ui_memory::boolValue(kEnabledKey, false));

    QMenu* viewMenu = nullptr;
    for (QMenu* menu : menuBar()->findChildren<QMenu*>()) {
        for (const QAction* action : menu->actions()) {
            if (action->menu() != nullptr && realisticAct_ != nullptr &&
                action->menu()->actions().contains(realisticAct_)) {
                viewMenu = menu;
            }
        }
    }
    if (viewMenu != nullptr) {
        viewMenu->addAction(fabricRiskAct_);
    }

    // --- Légende sur la vue ----------------------------------------------------------------------
    new FabricRiskLegend(view_->viewport());

    // --- Recalcul différé ------------------------------------------------------------------------
    fabricRiskTimer_ = new QTimer(this);
    fabricRiskTimer_->setSingleShot(true);
    fabricRiskTimer_->setInterval(kRecomputeDelayMs);
    connect(fabricRiskTimer_, &QTimer::timeout, this, &MainWindow::recomputeFabricRisk);

    // --- Section du panneau Analyse
    // ----------------------------------------------------------------
    QWidget* container = analysisDock_ != nullptr ? analysisDock_->widget() : nullptr;
    if (auto* scroll = qobject_cast<QScrollArea*>(container)) {
        container = scroll->widget(); // le contenu du dock Analyse défile (HP-UX-024)
    }
    auto* layout = container != nullptr ? qobject_cast<QVBoxLayout*>(container->layout()) : nullptr;
    if (layout != nullptr) {
        auto* group = new QGroupBox(tr("Risque tissu"), container);
        group->setObjectName(QStringLiteral("fabricRiskGroup"));
        auto* box = new QVBoxLayout(group);
        box->setContentsMargins(6, 6, 6, 6);
        box->setSpacing(4);

        auto* check = new QCheckBox(tr("Afficher la carte sur le canevas"), group);
        check->setObjectName(QStringLiteral("check_fabricRisk"));
        check->setToolTip(fabricRiskAct_->toolTip());
        check->setChecked(fabricRiskAct_->isChecked());
        box->addWidget(check);

        auto* row = new QHBoxLayout();
        auto* profileLabel = new QLabel(tr("Profil de tissu"), group);
        auto* profile = new QComboBox(group);
        profile->setObjectName(QStringLiteral("combo_fabricRiskProfile"));
        profile->setAccessibleName(tr("Profil indicatif de tissu pour le risque de tirage"));
        profile->addItem(tr("Coton tissé"));
        profile->addItem(tr("Jersey"));
        profile->addItem(tr("Feutrine"));
        profile->setCurrentIndex(ui_memory::intValue(kProfileKey, 0, 0, 2));
        profileLabel->setBuddy(profile);
        row->addWidget(profileLabel);
        row->addWidget(profile, 1);
        box->addLayout(row);

        auto* summary = new QLabel(tr("Carte désactivée (Affichage > Risque tissu)."), group);
        summary->setObjectName(QStringLiteral("label_fabricRiskSummary"));
        summary->setWordWrap(true);
        box->addWidget(summary);
        layout->addWidget(group);

        connect(check, &QCheckBox::toggled, this, [this](bool on) { setFabricRiskEnabled(on); });
        connect(profile, &QComboBox::currentIndexChanged, this, [this](int index) {
            ui_memory::setIntValue(kProfileKey, index);
            fabricRiskValid_ = false; // la clé porte le profil : recalcul
            if (fabricRiskAct_->isChecked()) {
                fabricRiskTimer_->start();
            }
        });
    }
    connect(fabricRiskAct_, &QAction::toggled, this, [this](bool on) { setFabricRiskEnabled(on); });
}

void MainWindow::setFabricRiskEnabled(bool on) {
    ui_memory::setBoolValue(kEnabledKey, on);
    if (fabricRiskAct_ != nullptr && fabricRiskAct_->isChecked() != on) {
        const QSignalBlocker blocker(fabricRiskAct_);
        fabricRiskAct_->setChecked(on);
    }
    auto* check = analysisDock_ != nullptr
                      ? analysisDock_->findChild<QCheckBox*>(QStringLiteral("check_fabricRisk"))
                      : nullptr;
    if (check != nullptr && check->isChecked() != on) {
        const QSignalBlocker blocker(check);
        check->setChecked(on);
    }
    auto* summary =
        analysisDock_ != nullptr
            ? analysisDock_->findChild<QLabel*>(QStringLiteral("label_fabricRiskSummary"))
            : nullptr;
    if (!on) {
        fabricRiskTimer_->stop();
        if (summary != nullptr) {
            summary->setText(tr("Carte désactivée (Affichage > Risque tissu)."));
        }
    } else if (summary != nullptr) {
        summary->setText(tr("Calcul de la carte…"));
    }
    fabricRiskValid_ = false;
    renderStitches(); // retire la surimpression, ou planifie le calcul (clé périmée)
}

// Appelée par renderStitches() juste après le nettoyage de la couche points : pose l'image en
// cache si elle décrit la séquence courante, sinon planifie le recalcul. L'élément rejoint
// `stitchItems_` (donc retiré au redessin suivant) et ne capte aucun clic.
void MainWindow::refreshFabricRiskOverlay() {
    if (fabricRiskAct_ == nullptr) {
        return;
    }
    auto* legend = view_ != nullptr ? view_->viewport()->findChild<FabricRiskLegend*>() : nullptr;
    if (legend != nullptr) {
        legend->setVisible(false);
    }
    if (!fabricRiskAct_->isChecked() || !sequence_.has_value() || simulating()) {
        return;
    }
    const std::uint64_t key = fabricRiskKey(*sequence_, fabricRiskProfile());
    if (!fabricRiskValid_ || fabricRiskKey_ != key) {
        fabricRiskValid_ = false;
        fabricRiskTimer_->start();
        return;
    }
    if (fabricRiskPixmap_.isNull()) {
        return; // rien de cousu à classer
    }
    auto* item = scene_->addPixmap(fabricRiskPixmap_);
    item->setTransformationMode(Qt::FastTransformation);
    item->setAcceptedMouseButtons(Qt::NoButton);
    item->setPos(fabricRiskRect_.x(), fabricRiskRect_.y());
    item->setScale(fabricRiskRect_.width() / fabricRiskPixmap_.width());
    item->setZValue(kOverlayZ);
    item->setData(0, QStringLiteral("fabricRiskOverlay"));
    stitchItems_.append(item);
    if (legend != nullptr) {
        legend->setVisible(true);
        legend->raise();
    }
}

void MainWindow::recomputeFabricRisk() {
    fabricRiskTimer_->stop();
    if (!fabricRiskAct_->isChecked() || !sequence_.has_value()) {
        return;
    }
    const int profile = fabricRiskProfile();
    const std::uint64_t key = fabricRiskKey(*sequence_, profile);
    project_ops::FabricRiskOptions options;
    options.fabric = fabricRiskProfileId(profile);
    options.heatmap = false; // la grille suffit : pas de PNG

    const document::Project snapshot = project_; // instantané immuable pour le fil de travail
    BusyIndicator busy(this, tr("Calcul du risque tissu…"), {autosaveTimer_, simTimer_});
    auto report = busy.run([&] { return project_ops::measure_fabric_risk(snapshot, options); });

    // Pendant l'attente, le motif ou le profil a pu changer : ce résultat serait périmé.
    if (!sequence_.has_value() || fabricRiskKey(*sequence_, fabricRiskProfile()) != key) {
        fabricRiskTimer_->start();
        return;
    }
    fabricRiskPixmap_ = QPixmap();
    fabricRiskGrid_ = project_ops::FabricRiskGrid{};
    QString text;
    if (report) {
        fabricRiskGrid_ = report->grid;
        const FabricRiskRaster raster = rasterizeFabricRisk(fabricRiskGrid_);
        if (raster.width > 0) {
            const QImage image(raster.rgba.data(), raster.width, raster.height, raster.width * 4,
                               QImage::Format_RGBA8888);
            fabricRiskPixmap_ = QPixmap::fromImage(image); // copie : `raster` est local
            const double cellMm = to_millimeters(fabricRiskGrid_.cell).value;
            fabricRiskRect_ = QRectF(to_millimeters(fabricRiskGrid_.top_left.x).value,
                                     -to_millimeters(fabricRiskGrid_.top_left.y).value,
                                     raster.width * cellMm, raster.height * cellMm);
        }
        const FabricRiskCounts counts = countFabricRisk(fabricRiskGrid_);
        const double cellMm2 = std::pow(to_millimeters(fabricRiskGrid_.cell).value, 2);
        text = tr("Indice max %1 / 100 · %2 mm² à risque élevé, %3 mm² modéré. Estimation "
                  "heuristique non calibrée, pas une mesure.")
                   .arg(report->max_score, 0, 'f', 0)
                   .arg(counts.high * cellMm2, 0, 'f', 0)
                   .arg(counts.moderate * cellMm2, 0, 'f', 0);
    } else {
        text = tr("Aucun fil cousu à analyser : pas de carte de risque.");
    }
    fabricRiskKey_ = key;
    fabricRiskValid_ = true;
    ++fabricRiskComputeCount_;
    if (auto* summary =
            analysisDock_->findChild<QLabel*>(QStringLiteral("label_fabricRiskSummary"))) {
        summary->setText(text);
    }
    renderStitches(); // la clé correspond maintenant : pose la surimpression
}

} // namespace openstitch::desktop
