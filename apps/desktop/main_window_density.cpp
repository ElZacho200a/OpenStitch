// SPDX-License-Identifier: Apache-2.0
// Carte de densité (HP-VIEW-005) et seuils des règles d'analyse optionnelles (HP-PROD-007).
// Aucun calcul métier ici : la grille et les zones sur-denses viennent de
// libs/stitch_analysis (density_map.hpp), calculées sur la séquence EFFECTIVE déjà en cache
// (`sequence_`) dans un fil de travail (BusyIndicator::run) sur un instantané. Le résultat est
// mis en cache sous une clé (séquence + seuil) : tant qu'elle ne change pas, un redessin de la
// scène pose simplement l'image déjà calculée, comme le rendu réaliste. La carte ne modifie
// jamais le document.
#include <QAction>
#include <QCheckBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

#include "busy_indicator.hpp"
#include "canvas_view.hpp"
#include "density_overlay.hpp"
#include "main_window.hpp"
#include "ui_memory.hpp"

namespace openstitch::desktop {

namespace {

constexpr int kHotspotCenterRole = Qt::UserRole;     // QPointF, repère scène (mm, Y vers le bas)
constexpr int kHotspotObjectRole = Qt::UserRole + 2; // id de l'objet dominant (0 = aucun)
constexpr int kHotspotDelayMs = 150;                 // regroupe une rafale de modifications

const QString kEnabledKey = QStringLiteral("view/densityMap");
const QString kThicknessKey = QStringLiteral("view/thicknessMap");

} // namespace

void MainWindow::buildDensityFeature() {
    // --- Action « Affichage > Carte de densité » -------------------------------------------
    densityMapAct_ = new QAction(tr("Carte de densit&é"), this);
    densityMapAct_->setObjectName(QStringLiteral("action_densityMap"));
    densityMapAct_->setCheckable(true);
    densityMapAct_->setToolTip(tr("Surimpression colorée de la densité de points (pénétrations "
                                  "par mm², du froid au chaud) ; les zones sur-denses sont "
                                  "listées dans le panneau Analyse."));
    densityMapAct_->setChecked(ui_memory::boolValue(kEnabledKey, false));

    // --- Action « Affichage > Carte d'épaisseur » (HP-VIEW-014) ------------------------------
    thicknessMapAct_ = new QAction(tr("Carte d'épaisseur"), this);
    thicknessMapAct_->setObjectName(QStringLiteral("action_thicknessMap"));
    thicknessMapAct_->setCheckable(true);
    thicknessMapAct_->setToolTip(
        tr("Surimpression colorée de l'épaisseur estimée du fil (en mm, du froid au chaud), "
           "toutes passes comprises. C'est une estimation à partir de la longueur de fil, pas "
           "une mesure ; la carte suit le zoom."));
    // Exclusive avec la carte de densité : une seule surimpression à la fois.
    thicknessMapAct_->setChecked(ui_memory::boolValue(kThicknessKey, false));
    if (thicknessMapAct_->isChecked() && densityMapAct_->isChecked()) {
        densityMapAct_->setChecked(false);
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
        viewMenu->insertAction(insertBefore, densityMapAct_);
        viewMenu->insertAction(insertBefore, thicknessMapAct_);
    }

    // --- Légende sur la vue ------------------------------------------------------------------
    auto* legend = new DensityLegend(view_->viewport());
    legend->setRange(1.0, loadDensityThreshold());

    // --- Recalcul différé --------------------------------------------------------------------
    densityTimer_ = new QTimer(this);
    densityTimer_->setSingleShot(true);
    densityTimer_->setInterval(kHotspotDelayMs);
    connect(densityTimer_, &QTimer::timeout, this, &MainWindow::recomputeDensity);

    // --- Section du panneau Analyse ----------------------------------------------------------
    QWidget* container = analysisDock_ != nullptr ? analysisDock_->widget() : nullptr;
    if (auto* scroll = qobject_cast<QScrollArea*>(container)) {
        container = scroll->widget(); // le contenu du dock Analyse défile (HP-UX-024)
    }
    auto* layout = container != nullptr ? qobject_cast<QVBoxLayout*>(container->layout()) : nullptr;
    if (layout != nullptr) {
        auto* group = new QGroupBox(tr("Carte de densité"), container);
        group->setObjectName(QStringLiteral("densityGroup"));
        auto* box = new QVBoxLayout(group);
        box->setContentsMargins(6, 6, 6, 6);
        box->setSpacing(4);

        auto* summary = new QLabel(tr("Carte désactivée (Affichage > Carte de densité)."), group);
        summary->setObjectName(QStringLiteral("densitySummary"));
        summary->setWordWrap(true);
        box->addWidget(summary);

        auto* row = new QHBoxLayout();
        auto* thresholdLabel = new QLabel(tr("Seuil (pén./mm²)"), group);
        auto* threshold = new QDoubleSpinBox(group);
        threshold->setObjectName(QStringLiteral("densityThresholdSpin"));
        threshold->setAccessibleName(tr("Seuil de zone sur-dense, en pénétrations par mm²"));
        threshold->setRange(0.5, 100.0);
        threshold->setSingleStep(0.5);
        threshold->setDecimals(1);
        threshold->setValue(loadDensityThreshold());
        thresholdLabel->setBuddy(threshold);
        row->addWidget(thresholdLabel);
        row->addWidget(threshold, 1);
        box->addLayout(row);

        // Réglages de la carte d'épaisseur : estimation, donc réglable (docs/veille-epaisseur).
        const ThicknessSettings thickness = ThicknessSettings::load();
        auto* layerRow = new QHBoxLayout();
        auto* layerLabel = new QLabel(tr("Épaisseur d'une couche (mm)"), group);
        auto* layerSpin = new QDoubleSpinBox(group);
        layerSpin->setObjectName(QStringLiteral("thicknessLayerSpin"));
        layerSpin->setAccessibleName(tr("Épaisseur estimée d'une couche dense, en millimètres"));
        layerSpin->setRange(0.05, 0.6);
        layerSpin->setSingleStep(0.01);
        layerSpin->setDecimals(2);
        layerSpin->setValue(thickness.layerMm);
        layerLabel->setBuddy(layerSpin);
        layerRow->addWidget(layerLabel);
        layerRow->addWidget(layerSpin, 1);
        box->addLayout(layerRow);
        auto* maxLayersRow = new QHBoxLayout();
        auto* maxLayersLabel = new QLabel(tr("Seuil d'épaisseur (couches)"), group);
        auto* maxLayersSpin = new QDoubleSpinBox(group);
        maxLayersSpin->setObjectName(QStringLiteral("thicknessMaxLayersSpin"));
        maxLayersSpin->setAccessibleName(tr("Seuil d'épaisseur, en couches de remplissage dense"));
        maxLayersSpin->setRange(0.5, 20.0);
        maxLayersSpin->setSingleStep(0.5);
        maxLayersSpin->setDecimals(1);
        maxLayersSpin->setValue(thickness.maxLayers);
        maxLayersLabel->setBuddy(maxLayersSpin);
        maxLayersRow->addWidget(maxLayersLabel);
        maxLayersRow->addWidget(maxLayersSpin, 1);
        box->addLayout(maxLayersRow);
        const auto storeThickness = [this, layerSpin, maxLayersSpin] {
            ThicknessSettings s;
            s.layerMm = layerSpin->value();
            s.maxLayers = maxLayersSpin->value();
            s.save();
            densityValid_ = false; // la clé de cache porte ces réglages : recalcul
            if (thicknessMode()) {
                densityTimer_->start();
            }
        };
        connect(layerSpin, &QDoubleSpinBox::valueChanged, this, storeThickness);
        connect(maxLayersSpin, &QDoubleSpinBox::valueChanged, this, storeThickness);

        auto* list = new QListWidget(group);
        list->setObjectName(QStringLiteral("densityHotspotList"));
        list->setAccessibleName(tr("Zones sur-denses"));
        list->setMaximumHeight(120);
        box->addWidget(list);
        layout->addWidget(group);

        connect(threshold, &QDoubleSpinBox::valueChanged, this, [this](double value) {
            saveDensityThreshold(value);
            densityValid_ = false; // la clé de cache porte le seuil : recalcul
            if (densityMapAct_->isChecked()) {
                densityTimer_->start();
            }
        });
        const auto center = [this](QListWidgetItem* item) {
            if (item != nullptr && item->data(kHotspotCenterRole).isValid()) {
                view_->centerOn(item->data(kHotspotCenterRole).toPointF());
            }
        };
        connect(list, &QListWidget::itemClicked, this, center);
        connect(list, &QListWidget::itemActivated, this, center);

        // --- Règles optionnelles (désactivées par défaut dans le cœur) -----------------------
        const ExtraRuleSettings extra = ExtraRuleSettings::load();
        auto* rules = new QGroupBox(tr("Règles optionnelles"), container);
        rules->setObjectName(QStringLiteral("extraRulesGroup"));
        auto* rulesBox = new QVBoxLayout(rules);
        rulesBox->setContentsMargins(6, 6, 6, 6);
        rulesBox->setSpacing(4);

        auto* trimsRow = new QHBoxLayout();
        auto* trimsCheck = new QCheckBox(tr("Trop de coupes, au-delà de"), rules);
        trimsCheck->setObjectName(QStringLiteral("extraTrimsCheck"));
        trimsCheck->setChecked(extra.trimsEnabled);
        auto* trimsSpin = new QSpinBox(rules);
        trimsSpin->setObjectName(QStringLiteral("extraTrimsSpin"));
        trimsSpin->setAccessibleName(tr("Nombre de coupes au-delà duquel signaler"));
        trimsSpin->setRange(1, 100'000);
        trimsSpin->setValue(extra.maxTrims);
        trimsSpin->setSuffix(tr(" coupes"));
        trimsSpin->setEnabled(extra.trimsEnabled);
        trimsRow->addWidget(trimsCheck, 1);
        trimsRow->addWidget(trimsSpin);
        rulesBox->addLayout(trimsRow);

        auto* densityRow = new QHBoxLayout();
        auto* densityCheck = new QCheckBox(tr("Densité excessive, au-delà de"), rules);
        densityCheck->setObjectName(QStringLiteral("extraDensityCheck"));
        densityCheck->setChecked(extra.densityEnabled);
        auto* densitySpin = new QDoubleSpinBox(rules);
        densitySpin->setObjectName(QStringLiteral("extraDensitySpin"));
        densitySpin->setAccessibleName(tr("Densité au-delà de laquelle signaler, en pén./mm²"));
        densitySpin->setRange(0.5, 100.0);
        densitySpin->setSingleStep(0.5);
        densitySpin->setDecimals(1);
        densitySpin->setValue(extra.maxDensity);
        densitySpin->setSuffix(tr(" pén./mm²"));
        densitySpin->setEnabled(extra.densityEnabled);
        densityRow->addWidget(densityCheck, 1);
        densityRow->addWidget(densitySpin);
        rulesBox->addLayout(densityRow);
        layout->addWidget(rules);

        // Un changement de seuil périme le résultat affiché (ré-analyse différée du panneau).
        const auto store = [this, trimsCheck, trimsSpin, densityCheck, densitySpin] {
            ExtraRuleSettings s;
            s.trimsEnabled = trimsCheck->isChecked();
            s.maxTrims = trimsSpin->value();
            s.densityEnabled = densityCheck->isChecked();
            s.maxDensity = densitySpin->value();
            s.save();
            trimsSpin->setEnabled(s.trimsEnabled);
            densitySpin->setEnabled(s.densityEnabled);
            markAnalysisStale();
        };
        connect(trimsCheck, &QCheckBox::toggled, this, store);
        connect(densityCheck, &QCheckBox::toggled, this, store);
        connect(trimsSpin, &QSpinBox::valueChanged, this, store);
        connect(densitySpin, &QDoubleSpinBox::valueChanged, this, store);
    }

    connect(densityMapAct_, &QAction::toggled, this, [this](bool on) { setDensityMapEnabled(on); });
    connect(thicknessMapAct_, &QAction::toggled, this,
            [this](bool on) { setThicknessMapEnabled(on); });
    // Réactivité : la carte d'épaisseur suit le zoom (case plus fine en zoom avant, plus grossière
    // et plus rapide en zoom arrière). Le recalcul est différé pour regrouper une rafale de
    // molette.
    connect(view_, &CanvasView::viewChanged, this, [this] {
        if (thicknessMode() && sequence_.has_value() && densityValid_ &&
            densityCellUm_ != thicknessCellForView().value) {
            densityTimer_->start();
        }
    });
}

Micrometers MainWindow::thicknessCellForView() const {
    const double pixelsPerMm = view_ != nullptr ? std::abs(view_->transform().m11()) : 0.0;
    return stitch_analysis::adaptive_cell(pixelsPerMm);
}

void MainWindow::setThicknessMapEnabled(bool on) {
    ui_memory::setBoolValue(kThicknessKey, on);
    if (thicknessMapAct_ != nullptr && thicknessMapAct_->isChecked() != on) {
        const QSignalBlocker blocker(thicknessMapAct_);
        thicknessMapAct_->setChecked(on);
    }
    if (on && densityMapAct_ != nullptr && densityMapAct_->isChecked()) {
        const QSignalBlocker blocker(densityMapAct_); // exclusives : la densité s'éteint
        densityMapAct_->setChecked(false);
        ui_memory::setBoolValue(kEnabledKey, false);
    }
    auto* summary = analysisDock_ != nullptr
                        ? analysisDock_->findChild<QLabel*>(QStringLiteral("densitySummary"))
                        : nullptr;
    auto* list = analysisDock_ != nullptr
                     ? analysisDock_->findChild<QListWidget*>(QStringLiteral("densityHotspotList"))
                     : nullptr;
    densityValid_ = false; // l'autre mode (ou plus aucun) : la clé ne correspond plus
    if (!on) {
        if (densityMapAct_ == nullptr || !densityMapAct_->isChecked()) {
            densityTimer_->stop();
            if (list != nullptr) {
                list->clear();
            }
            if (summary != nullptr) {
                summary->setText(tr("Carte désactivée (Affichage > Carte de densité)."));
            }
        }
        renderStitches(); // retire la surimpression, ou planifie le calcul de la densité
        return;
    }
    if (summary != nullptr) {
        summary->setText(tr("Calcul de la carte d'épaisseur…"));
    }
    renderStitches(); // la clé ne correspond pas : planifie le calcul
}

void MainWindow::adoptOverlayRaster(const DensityRaster& raster, Vec2um origin, Micrometers cell) {
    densityPixmap_ = QPixmap();
    if (raster.width > 0) {
        const QImage image(raster.rgba.data(), raster.width, raster.height, raster.width * 4,
                           QImage::Format_RGBA8888);
        densityPixmap_ = QPixmap::fromImage(image); // copie : `raster` est local à l'appelant
        const double cellMm = to_millimeters(cell).value;
        const double rows = raster.height;
        // Repère scène : x = mm, y = -mm (Y du document vers le haut) ; coin haut-gauche.
        densityRect_ = QRectF(to_millimeters(origin.x).value,
                              -(to_millimeters(origin.y).value + rows * cellMm),
                              raster.width * cellMm, rows * cellMm);
    }
    densityScaleMax_ = raster.scale_max;
}

void MainWindow::setDensityMapEnabled(bool on) {
    ui_memory::setBoolValue(kEnabledKey, on);
    if (densityMapAct_ != nullptr && densityMapAct_->isChecked() != on) {
        const QSignalBlocker blocker(densityMapAct_);
        densityMapAct_->setChecked(on);
    }
    if (on && thicknessMapAct_ != nullptr && thicknessMapAct_->isChecked()) {
        const QSignalBlocker blocker(thicknessMapAct_); // exclusives : l'épaisseur s'éteint
        thicknessMapAct_->setChecked(false);
        ui_memory::setBoolValue(kThicknessKey, false);
    }
    auto* summary = analysisDock_ != nullptr
                        ? analysisDock_->findChild<QLabel*>(QStringLiteral("densitySummary"))
                        : nullptr;
    auto* list = analysisDock_ != nullptr
                     ? analysisDock_->findChild<QListWidget*>(QStringLiteral("densityHotspotList"))
                     : nullptr;
    if (!on) {
        if (!thicknessMode()) {
            densityTimer_->stop();
            if (list != nullptr) {
                list->clear();
            }
            if (summary != nullptr) {
                summary->setText(tr("Carte désactivée (Affichage > Carte de densité)."));
            }
        }
        densityValid_ = false;
        renderStitches(); // retire la surimpression (ou planifie le calcul de l'épaisseur)
        return;
    }
    if (summary != nullptr) {
        summary->setText(tr("Calcul de la carte…"));
    }
    densityValid_ = false;
    renderStitches(); // la clé ne correspond pas : planifie le calcul
}

double MainWindow::densityThresholdValue() const {
    if (analysisDock_ != nullptr) {
        if (const auto* spin =
                analysisDock_->findChild<QDoubleSpinBox*>(QStringLiteral("densityThresholdSpin"))) {
            return spin->value();
        }
    }
    return loadDensityThreshold();
}

// Appelée par renderStitches() juste après le nettoyage de la couche points : pose l'image en
// cache si elle décrit la séquence courante, sinon planifie le recalcul. L'élément rejoint
// `stitchItems_` (donc retiré au redessin suivant) et ne capte aucun clic.
void MainWindow::refreshDensityOverlay() {
    if (densityMapAct_ == nullptr) {
        return;
    }
    auto* legend = view_ != nullptr ? view_->viewport()->findChild<DensityLegend*>() : nullptr;
    const bool thickness = thicknessMode();
    const bool wanted = (densityMapAct_->isChecked() || thickness) && sequence_.has_value() &&
                        !simulating() && showStitchesAct_ != nullptr &&
                        showStitchesAct_->isChecked();
    if (legend != nullptr) {
        legend->setVisible(false);
    }
    if (!wanted) {
        return;
    }
    const ThicknessSettings thicknessSettings = ThicknessSettings::load();
    const auto thicknessOptions = thicknessSettings.options(thicknessCellForView());
    const double threshold =
        thickness ? stitch_analysis::layers_to_mm(thicknessOptions.max_layers, thicknessOptions)
                  : densityThresholdValue();
    const std::uint64_t key =
        thickness ? thicknessKey(*sequence_, thicknessOptions) : densityKey(*sequence_, threshold);
    if (!densityValid_ || densityKey_ != key) {
        densityValid_ = false;
        densityTimer_->start();
        return;
    }
    if (densityPixmap_.isNull()) {
        return; // séquence sans point cousu : rien à peindre
    }
    auto* item = scene_->addPixmap(densityPixmap_);
    item->setTransformationMode(Qt::FastTransformation);
    item->setAcceptedMouseButtons(Qt::NoButton);
    item->setPos(densityRect_.x(), densityRect_.y());
    item->setScale(densityRect_.width() / densityPixmap_.width());
    item->setZValue(25); // au-dessus du rendu réaliste (20)
    item->setData(0, QStringLiteral("densityOverlay"));
    stitchItems_.append(item);
    if (legend != nullptr) {
        legend->setThicknessMode(thickness);
        legend->setRange(densityScaleMax_, threshold);
        legend->setVisible(true);
        legend->raise();
    }
}

void MainWindow::recomputeThickness() {
    const stitch::StitchSequence snapshot = *sequence_; // instantané immuable
    const Micrometers cell = thicknessCellForView();
    const auto options = ThicknessSettings::load().options(cell);
    const std::uint64_t key = thicknessKey(snapshot, options);

    ThicknessResult result;
    {
        BusyIndicator busy(this, tr("Calcul de la carte d'épaisseur…"),
                           {autosaveTimer_, simTimer_});
        result = busy.run([&snapshot, &options] { return computeThickness(snapshot, options); });
    }
    // Pendant l'attente, le motif, les réglages ou le zoom ont pu changer : résultat périmé.
    if (!sequence_.has_value() || !thicknessMode() ||
        thicknessKey(*sequence_, ThicknessSettings::load().options(thicknessCellForView())) !=
            key) {
        densityTimer_->start();
        return;
    }
    adoptOverlayRaster(result.raster, result.origin, result.cell);
    densityKey_ = key;
    densityCellUm_ = cell.value;
    densityValid_ = true;
    ++densityComputeCount_;

    // Pic d'épaisseur dans le panneau Analyse : clic = centrer la vue dessus.
    if (auto* list = analysisDock_->findChild<QListWidget*>(QStringLiteral("densityHotspotList"))) {
        list->clear();
        if (result.peak.found) {
            const auto* emb = project_.findEmbroidery(result.peak.object);
            const QString where = (emb != nullptr && !emb->name.empty())
                                      ? tr(" · « %1 »").arg(QString::fromStdString(emb->name))
                                      : QString();
            auto* item = new QListWidgetItem(tr("Plus épais : %1 mm estimés (%2 couches)%3")
                                                 .arg(result.peak.mm, 0, 'f', 2)
                                                 .arg(result.peak.layers, 0, 'f', 1)
                                                 .arg(where));
            item->setData(kHotspotCenterRole,
                          QPointF(to_millimeters(result.peak.position.x).value,
                                  -to_millimeters(result.peak.position.y).value));
            item->setData(kHotspotObjectRole, static_cast<qulonglong>(result.peak.object.value));
            item->setToolTip(tr("Cliquer pour centrer la vue sur la zone la plus épaisse."));
            list->addItem(item);
        }
    }
    if (auto* summary = analysisDock_->findChild<QLabel*>(QStringLiteral("densitySummary"))) {
        if (!result.peak.found) {
            summary->setText(tr("Aucun fil cousu : pas d'épaisseur à estimer."));
        } else {
            summary->setText(
                (result.peak.over_limit
                     ? tr("Épaisseur estimée maximale : %1 mm (%2 couches), au-dessus du seuil de "
                          "%3 couches.")
                     : tr("Épaisseur estimée maximale : %1 mm (%2 couches), sous le seuil de %3 "
                          "couches."))
                    .arg(result.peak.mm, 0, 'f', 2)
                    .arg(result.peak.layers, 0, 'f', 1)
                    .arg(options.max_layers, 0, 'f', 1) +
                tr(" Estimation à partir de la longueur de fil, non mesurée sur machine."));
        }
    }
    renderStitches(); // la clé correspond maintenant : pose la surimpression
}

void MainWindow::recomputeDensity() {
    densityTimer_->stop();
    if ((!densityMapAct_->isChecked() && !thicknessMode()) || !sequence_.has_value()) {
        return;
    }
    if (thicknessMode()) {
        recomputeThickness();
        return;
    }
    const stitch::StitchSequence snapshot = *sequence_; // instantané immuable
    const double threshold = densityThresholdValue();
    const std::uint64_t key = densityKey(snapshot, threshold);

    DensityResult result;
    {
        BusyIndicator busy(this, tr("Calcul de la carte de densité…"), {autosaveTimer_, simTimer_});
        result = busy.run([&snapshot, threshold] { return computeDensity(snapshot, threshold); });
    }
    // Pendant l'attente, le motif ou le seuil a pu changer : ce résultat serait périmé.
    if (!sequence_.has_value() || densityKey(*sequence_, densityThresholdValue()) != key) {
        densityTimer_->start();
        return;
    }

    adoptOverlayRaster(result.raster, result.origin, result.cell);
    densityKey_ = key;
    densityValid_ = true;
    ++densityComputeCount_;

    // Zones sur-denses dans le panneau Analyse : clic = centrer la vue sur la zone.
    if (auto* list = analysisDock_->findChild<QListWidget*>(QStringLiteral("densityHotspotList"))) {
        list->clear();
        int n = 0;
        for (const auto& h : result.hotspots) {
            ++n;
            const auto* emb = project_.findEmbroidery(h.object);
            const QString where = (emb != nullptr && !emb->name.empty())
                                      ? tr(" · « %1 »").arg(QString::fromStdString(emb->name))
                                      : QString();
            auto* item = new QListWidgetItem(tr("Zone %1 : jusqu'à %2 pén./mm² sur %3 mm²%4")
                                                 .arg(n)
                                                 .arg(h.max_penetrations_per_mm2, 0, 'f', 1)
                                                 .arg(h.area_mm2, 0, 'f', 0)
                                                 .arg(where));
            const double cx =
                (to_millimeters(h.box.min.x).value + to_millimeters(h.box.max.x).value) / 2;
            const double cy =
                (to_millimeters(h.box.min.y).value + to_millimeters(h.box.max.y).value) / 2;
            item->setData(kHotspotCenterRole, QPointF(cx, -cy));
            item->setData(kHotspotObjectRole, static_cast<qulonglong>(h.object.value));
            item->setToolTip(tr("Cliquer pour centrer la vue sur cette zone."));
            list->addItem(item);
        }
    }
    if (auto* summary = analysisDock_->findChild<QLabel*>(QStringLiteral("densitySummary"))) {
        summary->setText(result.hotspots.empty()
                             ? tr("Aucune zone au-dessus de %1 pén./mm² (maximum %2).")
                                   .arg(threshold, 0, 'f', 1)
                                   .arg(result.max_density, 0, 'f', 1)
                             : tr("%1 zone(s) sur-dense(s) au-dessus de %2 pén./mm² (maximum %3).")
                                   .arg(result.hotspots.size())
                                   .arg(threshold, 0, 'f', 1)
                                   .arg(result.max_density, 0, 'f', 1));
    }
    renderStitches(); // la clé correspond maintenant : pose la surimpression
}

} // namespace openstitch::desktop
