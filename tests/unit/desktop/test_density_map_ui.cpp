// SPDX-License-Identifier: Apache-2.0
// Carte de densité (HP-VIEW-005) et règles d'analyse optionnelles (HP-PROD-007) câblées dans
// MainWindow. Le calcul lui-même est testé dans tests/unit/stitch_analysis. Headless, aucune
// comparaison de pixels, aucun sleep : les recalculs sont déclenchés explicitement.
#include <QAction>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <cmath>

#include "canvas_view.hpp"
#include "density_overlay.hpp"
#include "main_window.hpp"
#include "openstitch/document/project.hpp"

using openstitch::Micrometers;
using openstitch::ObjectId;
using openstitch::Vec2um;
using openstitch::desktop::DensityLegend;
using openstitch::desktop::MainWindow;

namespace {

namespace doc = openstitch::document;
namespace geo = openstitch::geometry;

void addShape(doc::Project& project, std::int32_t x0, std::int32_t side,
              const doc::StitchParams& params, const char* name) {
    doc::VectorObject vector;
    vector.id = project.object_ids.next();
    vector.name = name;
    geo::Path path;
    path.closed = true;
    for (const auto& [x, y] :
         {std::pair{0, 0}, std::pair{side, 0}, std::pair{side, side}, std::pair{0, side}}) {
        path.nodes.push_back({Vec2um{Micrometers{x0 + x}, Micrometers{y}}, geo::NodeType::Corner,
                              std::nullopt, std::nullopt});
    }
    vector.paths.push_back({std::move(path), {}});
    doc::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.source_vector = vector.id;
    emb.name = name;
    emb.params = params;
    project.vector_objects.push_back(std::move(vector));
    project.embroidery_objects.push_back(std::move(emb));
}

doc::Project emptyProject() {
    doc::Project project;
    project.original.width = 2;
    project.original.height = 2;
    project.original.rgba.assign(16, 255);
    return project;
}

// Remplissage serré (rangées à 0,2 mm) : ~1,7 pénétration/mm², au-dessus du seuil minimal 0,5.
doc::Project denseProject(std::int32_t x0 = 0) {
    doc::Project project = emptyProject();
    doc::TatamiParams tatami;
    tatami.row_spacing = Micrometers{200};
    addShape(project, x0, 10'000, tatami, "Aplat");
    return project;
}

} // namespace

namespace openstitch::desktop {

// Nom imposé par `friend class MainWindowTest` (main_window.hpp).
class MainWindowTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir settingsDir_;

    static int overlayCount(const MainWindow& window) {
        int n = 0;
        for (const QGraphicsItem* item : window.scene_->items()) {
            n += item->data(0).toString() == QStringLiteral("densityOverlay") ? 1 : 0;
        }
        return n;
    }
    static QListWidget* hotspotList(MainWindow& window) {
        return window.analysisDock_->findChild<QListWidget*>(QStringLiteral("densityHotspotList"));
    }
    static QDoubleSpinBox* thresholdSpin(MainWindow& window) {
        return window.analysisDock_->findChild<QDoubleSpinBox*>(
            QStringLiteral("densityThresholdSpin"));
    }
    static QStringList findingTexts(MainWindow& window) {
        QStringList texts;
        for (int i = 0; i < window.analysisList_->count(); ++i) {
            texts << window.analysisList_->item(i)->text();
        }
        return texts;
    }
    static bool anyContains(const QStringList& texts, const QString& needle) {
        for (const QString& t : texts) {
            if (t.contains(needle)) {
                return true;
            }
        }
        return false;
    }

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("DensityMapUiTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }
    void init() { QSettings().clear(); }

    void actionLivesInTheViewMenuAndIsOffByDefault() {
        MainWindow window;
        QVERIFY(window.densityMapAct_ != nullptr);
        QVERIFY(window.densityMapAct_->isCheckable());
        QVERIFY(!window.densityMapAct_->isChecked());
        bool inViewMenu = false;
        for (const QMenu* menu : window.menuBar()->findChildren<QMenu*>()) {
            if (menu->actions().contains(window.densityMapAct_)) {
                inViewMenu = menu->title().contains(QStringLiteral("Affichage"));
            }
        }
        QVERIFY(inViewMenu);
        // Le libellé porte un mnémonique (« densit&é ») : on compare sans le « & ».
        QString label = window.densityMapAct_->text();
        label.remove(QLatin1Char('&'));
        QVERIFY(label.contains(QStringLiteral("densité")));
    }

    void toggleDrawsAndRemovesTheOverlayWithLegend() {
        MainWindow window;
        window.applyLoadedProject(denseProject());
        QVERIFY(window.sequence_.has_value());
        QCOMPARE(overlayCount(window), 0);

        window.densityMapAct_->setChecked(true);
        QVERIFY(window.densityTimer_->isActive()); // recalcul planifié (cache vide)
        window.recomputeDensity();
        QVERIFY(window.densityValid_);
        QCOMPARE(window.densityComputeCount_, 1);
        QCOMPARE(overlayCount(window), 1);
        auto* legend = window.view_->viewport()->findChild<DensityLegend*>();
        QVERIFY(legend != nullptr);
        QVERIFY(!legend->isHidden());
        QVERIFY(legend->scaleMax() >= 1.0);

        // Un simple redessin réutilise l'image en cache : pas de nouveau calcul.
        window.renderStitches();
        QCOMPARE(window.densityComputeCount_, 1);
        QCOMPARE(overlayCount(window), 1);

        window.densityMapAct_->setChecked(false);
        QCOMPARE(overlayCount(window), 0);
        QVERIFY(legend->isHidden());
        QCOMPARE(hotspotList(window)->count(), 0);
    }

    void overlayDoesNotChangeTheDocument() {
        MainWindow window;
        window.applyLoadedProject(denseProject());
        const auto undoBefore = window.undoStack_.undoNames().size();
        const auto objectsBefore = window.project_.embroidery_objects.size();
        const auto commandsBefore = window.sequence_->commands.size();
        window.densityMapAct_->setChecked(true);
        window.recomputeDensity();
        QCOMPARE(window.undoStack_.undoNames().size(), undoBefore);
        QCOMPARE(window.project_.embroidery_objects.size(), objectsBefore);
        QCOMPARE(window.sequence_->commands.size(), commandsBefore);
    }

    void enabledStateIsRemembered() {
        {
            MainWindow window;
            window.densityMapAct_->setChecked(true);
        }
        MainWindow again;
        QVERIFY(again.densityMapAct_->isChecked());
        again.densityMapAct_->setChecked(false);
        MainWindow third;
        QVERIFY(!third.densityMapAct_->isChecked());
    }

    // ---- HP-VIEW-014 : carte d'épaisseur estimée
    // -------------------------------------------------

    static QLabel* summaryLabel(MainWindow& window) {
        return window.analysisDock_->findChild<QLabel*>(QStringLiteral("densitySummary"));
    }

    void thicknessActionIsOffByDefaultAndExclusiveWithDensity() {
        MainWindow window;
        QVERIFY(window.thicknessMapAct_ != nullptr);
        QVERIFY(window.thicknessMapAct_->isCheckable());
        QVERIFY(!window.thicknessMapAct_->isChecked());
        bool inViewMenu = false;
        for (const QMenu* menu : window.menuBar()->findChildren<QMenu*>()) {
            if (menu->actions().contains(window.thicknessMapAct_)) {
                inViewMenu = menu->title().contains(QStringLiteral("Affichage"));
            }
        }
        QVERIFY(inViewMenu);

        window.densityMapAct_->setChecked(true);
        window.thicknessMapAct_->setChecked(true); // une seule surimpression à la fois
        QVERIFY(window.thicknessMapAct_->isChecked());
        QVERIFY(!window.densityMapAct_->isChecked());
        window.densityMapAct_->setChecked(true);
        QVERIFY(window.densityMapAct_->isChecked());
        QVERIFY(!window.thicknessMapAct_->isChecked());
    }

    void thicknessOverlayShowsMillimetresAndTheThickestSpot() {
        MainWindow window;
        window.applyLoadedProject(denseProject());
        window.thicknessMapAct_->setChecked(true);
        QVERIFY(window.densityTimer_->isActive());
        window.recomputeDensity();
        QVERIFY(window.densityValid_);
        QCOMPARE(window.densityComputeCount_, 1);
        QCOMPARE(overlayCount(window), 1);

        auto* legend = window.view_->viewport()->findChild<DensityLegend*>();
        QVERIFY(legend != nullptr);
        QVERIFY(legend->thicknessMode());
        QVERIFY(!legend->isHidden());
        QVERIFY(legend->scaleMax() >= 0.1);

        // La ligne du pic : épaisseur en mm, objet nommé, cible = centre d'une case de l'aplat.
        auto* list = hotspotList(window);
        QCOMPARE(list->count(), 1);
        QVERIFY(list->item(0)->text().contains(QStringLiteral("mm estimés")));
        QVERIFY(list->item(0)->text().contains(QStringLiteral("Aplat")));
        const QPointF target = list->item(0)->data(Qt::UserRole).toPointF();
        QVERIFY(target.x() > 0.0 && target.x() < 10.0);
        QVERIFY(target.y() < 0.0 && target.y() > -10.0);
        // Résumé : c'est annoncé comme une estimation, jamais comme une mesure.
        QVERIFY(summaryLabel(window)->text().contains(QStringLiteral("non mesurée")));

        // Un simple redessin réutilise le cache.
        window.renderStitches();
        QCOMPARE(window.densityComputeCount_, 1);

        window.thicknessMapAct_->setChecked(false);
        QCOMPARE(overlayCount(window), 0);
        QVERIFY(legend->isHidden());
        QCOMPARE(hotspotList(window)->count(), 0);
    }

    void thicknessMapDoesNotChangeTheDocument() {
        MainWindow window;
        window.applyLoadedProject(denseProject());
        const auto undoBefore = window.undoStack_.undoNames().size();
        const auto objectsBefore = window.project_.embroidery_objects.size();
        const auto commandsBefore = window.sequence_->commands.size();
        window.thicknessMapAct_->setChecked(true);
        window.recomputeDensity();
        QCOMPARE(window.undoStack_.undoNames().size(), undoBefore);
        QCOMPARE(window.project_.embroidery_objects.size(), objectsBefore);
        QCOMPARE(window.sequence_->commands.size(), commandsBefore);
    }

    void thicknessMapFollowsTheZoom() {
        MainWindow window;
        window.applyLoadedProject(denseProject());
        window.thicknessMapAct_->setChecked(true);
        window.recomputeDensity();
        const int cellBefore = window.densityCellUm_;
        QCOMPARE(cellBefore, window.thicknessCellForView().value);

        // Zoom avant : la case de la carte devient plus fine (jusqu'à 0,5 mm), le recalcul est
        // planifié, et le calcul suivant utilise le nouveau côté.
        for (int i = 0; i < 12 && window.thicknessCellForView().value >= cellBefore; ++i) {
            window.view_->zoomIn();
        }
        const int wanted = window.thicknessCellForView().value;
        QVERIFY2(wanted < cellBefore, "le zoom avant doit affiner la case");
        QVERIFY(window.densityTimer_->isActive());
        window.recomputeDensity();
        QCOMPARE(window.densityCellUm_, wanted);
        QCOMPARE(window.densityComputeCount_, 2);
        QCOMPARE(overlayCount(window), 1);
    }

    void thicknessSettingsInvalidateTheCacheAndRescaleTheLegend() {
        MainWindow window;
        window.applyLoadedProject(denseProject());
        window.thicknessMapAct_->setChecked(true);
        window.recomputeDensity();
        const double scaleBefore = window.densityScaleMax_;
        auto* spin =
            window.analysisDock_->findChild<QDoubleSpinBox*>(QStringLiteral("thicknessLayerSpin"));
        QVERIFY(spin != nullptr);
        QCOMPARE(spin->value(), 0.2); // valeur par défaut du cœur
        window.densityTimer_->stop();
        spin->setValue(0.4); // couche deux fois plus épaisse : l'échelle en mm double au moins
        QVERIFY(!window.densityValid_);
        QVERIFY(window.densityTimer_->isActive());
        window.recomputeDensity();
        QVERIFY(window.densityValid_);
        QVERIFY(window.densityScaleMax_ > scaleBefore);
        QCOMPARE(window.densityComputeCount_, 2);
    }

    void thicknessEnabledStateIsRemembered() {
        {
            MainWindow window;
            window.thicknessMapAct_->setChecked(true);
        }
        MainWindow again;
        QVERIFY(again.thicknessMapAct_->isChecked());
        QVERIFY(!again.densityMapAct_->isChecked());
        again.thicknessMapAct_->setChecked(false);
        MainWindow third;
        QVERIFY(!third.thicknessMapAct_->isChecked());
    }

    void hotspotsAreListedAndClickCentersTheView() {
        MainWindow window;
        window.applyLoadedProject(denseProject());
        window.densityMapAct_->setChecked(true);
        thresholdSpin(window)->setValue(0.5);
        window.recomputeDensity();
        auto* list = hotspotList(window);
        QVERIFY(list != nullptr);
        QVERIFY(list->count() >= 1);
        QVERIFY(list->item(0)->text().contains(QStringLiteral("pén./mm²")));
        QVERIFY(list->item(0)->text().contains(QStringLiteral("Aplat")));

        const QPointF target = list->item(0)->data(Qt::UserRole).toPointF();
        // L'aplat couvre x 0..10 mm, y 0..10 mm (document, Y vers le haut -> scène y négatif).
        QVERIFY(target.x() > 0.0 && target.x() < 10.0);
        QVERIFY(target.y() < 0.0 && target.y() > -10.0);
        // Le clic appelle CanvasView::centerOn(centre de la zone) ; la plage de défilement étant
        // bornée par la scène, la position exacte n'est pas vérifiable en headless : on contrôle
        // la cible (donnée de la ligne) et que le clic ne modifie rien.
        const auto undoBefore = window.undoStack_.undoNames().size();
        emit list->itemClicked(list->item(0));
        QCOMPARE(window.undoStack_.undoNames().size(), undoBefore);
    }

    void aHighThresholdListsNothing() {
        MainWindow window;
        window.applyLoadedProject(denseProject());
        window.densityMapAct_->setChecked(true);
        thresholdSpin(window)->setValue(100.0);
        window.recomputeDensity();
        QCOMPARE(hotspotList(window)->count(), 0);
        QCOMPARE(overlayCount(window), 1); // la carte reste affichée sans zone signalée
    }

    void thresholdChangeInvalidatesAndIsRemembered() {
        MainWindow window;
        window.applyLoadedProject(denseProject());
        window.densityMapAct_->setChecked(true);
        window.recomputeDensity();
        QVERIFY(window.densityValid_);
        thresholdSpin(window)->setValue(2.5);
        QVERIFY(!window.densityValid_);
        QVERIFY(window.densityTimer_->isActive());
        window.recomputeDensity();
        QCOMPARE(window.densityComputeCount_, 2);
        QCOMPARE(loadDensityThreshold(), 2.5);
    }

    void documentChangeInvalidatesTheCache() {
        MainWindow window;
        window.applyLoadedProject(denseProject());
        window.densityMapAct_->setChecked(true);
        window.recomputeDensity();
        const auto keyBefore = window.densityKey_;
        QCOMPARE(overlayCount(window), 1);

        // Autre motif : l'ancienne image est retirée (jamais montrée périmée) et le calcul
        // replanifié.
        window.applyLoadedProject(denseProject(30'000));
        QCOMPARE(overlayCount(window), 0);
        QVERIFY(window.densityTimer_->isActive());
        window.recomputeDensity();
        QVERIFY(window.densityKey_ != keyBefore);
        QCOMPARE(overlayCount(window), 1);
        QCOMPARE(window.densityComputeCount_, 2);

        // Plus aucun point : plus de surimpression, la légende disparaît.
        window.applyLoadedProject(emptyProject());
        QCOMPARE(overlayCount(window), 0);
        auto* legend = window.view_->viewport()->findChild<DensityLegend*>();
        QVERIFY(legend->isHidden());
    }

    void newAnalysisRulesAreListedInThePanel() {
        MainWindow window;
        doc::Project project = emptyProject();
        project.finishing.enabled = true;
        project.finishing.lock_type = doc::LockStitch::None; // pas de points d'arrêt posés
        project.finishing.filter_short_stitches = false;
        doc::RunningStitchParams tiny;
        tiny.stitch_length = Micrometers{200};
        addShape(project, 0, 800, tiny, "Minuscule");
        window.applyLoadedProject(std::move(project));
        window.runAnalysis();
        const QStringList texts = findingTexts(window);
        QVERIFY2(anyContains(texts, QStringLiteral("le détail risque de se perdre")),
                 qPrintable(texts.join('\n')));
        QVERIFY2(anyContains(texts, QStringLiteral("Objet sans point d'arrêt")),
                 qPrintable(texts.join('\n')));
        // Même mécanisme que les autres règles : objet nommé et localisé (zoom au clic).
        for (int i = 0; i < window.analysisList_->count(); ++i) {
            const auto* item = window.analysisList_->item(i);
            if (item->text().contains(QStringLiteral("le détail risque de se perdre"))) {
                QVERIFY(item->text().contains(QStringLiteral("Minuscule")));
                QVERIFY(item->data(Qt::UserRole + 4).toBool()); // position connue
                QVERIFY(item->data(Qt::UserRole + 2).toULongLong() != 0);
            }
        }
    }

    void optionalRulesAreOffThenSwitchedOnFromThePanel() {
        MainWindow window;
        doc::Project project = emptyProject();
        doc::TatamiParams tatami;
        tatami.row_spacing = Micrometers{200};
        addShape(project, 0, 10'000, tatami, "A");
        addShape(project, 40'000, 10'000, tatami, "B");
        addShape(project, 80'000, 10'000, tatami, "C");
        window.applyLoadedProject(std::move(project));
        window.runAnalysis();
        QVERIFY(!anyContains(findingTexts(window), QStringLiteral("coupes (plus de")));
        QVERIFY(!anyContains(findingTexts(window), QStringLiteral("Zone trop dense")));

        auto* trimsCheck =
            window.analysisDock_->findChild<QCheckBox*>(QStringLiteral("extraTrimsCheck"));
        auto* trimsSpin =
            window.analysisDock_->findChild<QSpinBox*>(QStringLiteral("extraTrimsSpin"));
        auto* densityCheck =
            window.analysisDock_->findChild<QCheckBox*>(QStringLiteral("extraDensityCheck"));
        auto* densitySpin =
            window.analysisDock_->findChild<QDoubleSpinBox*>(QStringLiteral("extraDensitySpin"));
        QVERIFY(trimsCheck && trimsSpin && densityCheck && densitySpin);
        QVERIFY(!trimsCheck->isChecked() && !densityCheck->isChecked());
        QVERIFY(!trimsSpin->isEnabled() && !densitySpin->isEnabled());
        QCOMPARE(densitySpin->value(),
                 openstitch::stitch_analysis::DensityOptions{}.max_penetrations_per_mm2);

        trimsCheck->setChecked(true);
        trimsSpin->setValue(1);
        densityCheck->setChecked(true);
        densitySpin->setValue(0.5);
        QVERIFY(trimsSpin->isEnabled() && densitySpin->isEnabled());
        QVERIFY(window.analysisIsStale_); // le résultat affiché ne correspond plus aux réglages
        window.runAnalysis();
        const QStringList texts = findingTexts(window);
        QVERIFY2(anyContains(texts, QStringLiteral("coupes (plus de 1)")),
                 qPrintable(texts.join('\n')));
        QVERIFY2(anyContains(texts, QStringLiteral("Zone trop dense")),
                 qPrintable(texts.join('\n')));

        // Réglages mémorisés pour la session suivante.
        const auto saved = ExtraRuleSettings::load();
        QVERIFY(saved.trimsEnabled && saved.densityEnabled);
        QCOMPARE(saved.maxTrims, 1);
    }
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_density_map_ui.moc"
