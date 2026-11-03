// SPDX-License-Identifier: Apache-2.0
// Surimpression « Risque tissu » câblée dans MainWindow. Le calcul (indice, classes, grille) est
// testé dans tests/unit/project_ops. Headless, aucune comparaison de pixels, aucun sleep : les
// recalculs sont déclenchés explicitement et on vérifie la grille de cases classées exposée.
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDockWidget>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include "canvas_view.hpp"
#include "fabric_risk_overlay.hpp"
#include "main_window.hpp"
#include "openstitch/document/project.hpp"

using openstitch::Micrometers;
using openstitch::Vec2um;
using openstitch::desktop::FabricRiskLegend;
using openstitch::desktop::MainWindow;
using openstitch::project_ops::FabricRiskLevel;

namespace {

namespace doc = openstitch::document;
namespace geo = openstitch::geometry;

void addShape(doc::Project& project, std::int32_t side, const char* name) {
    doc::VectorObject vector;
    vector.id = project.object_ids.next();
    vector.name = name;
    geo::Path path;
    path.closed = true;
    for (const auto& [x, y] :
         {std::pair{0, 0}, std::pair{side, 0}, std::pair{side, side}, std::pair{0, side}}) {
        path.nodes.push_back({Vec2um{Micrometers{x}, Micrometers{y}}, geo::NodeType::Corner,
                              std::nullopt, std::nullopt});
    }
    vector.paths.push_back({std::move(path), {}});
    doc::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.source_vector = vector.id;
    emb.name = name;
    emb.params = doc::TatamiParams{};
    project.vector_objects.push_back(std::move(vector));
    project.embroidery_objects.push_back(std::move(emb));
}

doc::Project squareProject(int layers) {
    doc::Project project;
    project.original.width = 2;
    project.original.height = 2;
    project.original.rgba.assign(16, 255);
    for (int i = 0; i < layers; ++i) {
        addShape(project, 10'000, "Aplat");
    }
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
            n += item->data(0).toString() == QStringLiteral("fabricRiskOverlay") ? 1 : 0;
        }
        return n;
    }

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("FabricRiskUiTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }
    void init() { QSettings().clear(); }

    void actionLivesInTheViewMenuAndIsOffByDefault() {
        MainWindow window;
        QVERIFY(window.fabricRiskAct_ != nullptr);
        QVERIFY(window.fabricRiskAct_->isCheckable());
        QVERIFY(!window.fabricRiskAct_->isChecked());
        bool inMenu = false;
        for (const QMenu* menu : window.menuBar()->findChildren<QMenu*>()) {
            inMenu = inMenu || menu->actions().contains(window.fabricRiskAct_);
        }
        QVERIFY(inMenu);
        // La case du panneau Analyse suit l'action.
        auto* check =
            window.analysisDock_->findChild<QCheckBox*>(QStringLiteral("check_fabricRisk"));
        QVERIFY(check != nullptr);
        check->setChecked(true);
        QVERIFY(window.fabricRiskAct_->isChecked());
        window.fabricRiskAct_->setChecked(false);
        QVERIFY(!check->isChecked());
    }

    void toggleComputesOnceCachesAndDrawsTheClassifiedGrid() {
        MainWindow window;
        window.applyLoadedProject(squareProject(3));
        QVERIFY(window.sequence_.has_value());
        QCOMPARE(overlayCount(window), 0);

        window.fabricRiskAct_->setChecked(true);
        QVERIFY(window.fabricRiskTimer_->isActive()); // recalcul planifié (cache vide)
        QVERIFY(!window.fabricRiskValid_);
        window.recomputeFabricRisk();
        QVERIFY(window.fabricRiskValid_);
        QCOMPARE(window.fabricRiskComputeCount_, 1);
        QCOMPARE(overlayCount(window), 1);

        // Grille de cases classées : dimensions cohérentes, cases cousues non vides.
        const auto& grid = window.fabricRiskGrid_;
        QVERIFY(grid.cols > 0 && grid.rows > 0);
        QCOMPARE(grid.levels.size(), static_cast<std::size_t>(grid.cols) * grid.rows);
        const FabricRiskCounts counts = countFabricRisk(grid);
        QVERIFY(counts.none < grid.levels.size());
        QVERIFY(counts.low + counts.moderate + counts.high > 0);
        QCOMPARE(counts.none + counts.low + counts.moderate + counts.high, grid.levels.size());
        // Trois couches de remplissage superposées : au moins une case à risque élevé.
        QVERIFY(counts.high > 0);

        auto* legend = window.view_->viewport()->findChild<FabricRiskLegend*>();
        QVERIFY(legend != nullptr);
        QVERIFY(!legend->isHidden());

        // Un simple redessin réutilise l'image en cache : pas de nouveau calcul.
        window.renderStitches();
        QCOMPARE(window.fabricRiskComputeCount_, 1);
        QCOMPARE(overlayCount(window), 1);

        window.fabricRiskAct_->setChecked(false);
        QCOMPARE(overlayCount(window), 0);
        QVERIFY(legend->isHidden());
    }

    void cacheIsInvalidatedWhenTheSequenceChanges() {
        MainWindow window;
        window.applyLoadedProject(squareProject(1));
        window.fabricRiskAct_->setChecked(true);
        window.recomputeFabricRisk();
        const auto oneLayer = countFabricRisk(window.fabricRiskGrid_);
        const auto keyBefore = window.fabricRiskKey_;

        // Nouveau motif (couches supplémentaires) : la clé change, le cache est périmé.
        window.applyLoadedProject(squareProject(3));
        QVERIFY(window.fabricRiskTimer_->isActive());
        window.recomputeFabricRisk();
        QCOMPARE(window.fabricRiskComputeCount_, 2);
        QVERIFY(window.fabricRiskKey_ != keyBefore);
        const auto threeLayers = countFabricRisk(window.fabricRiskGrid_);
        QVERIFY(threeLayers.high > oneLayer.high ||
                threeLayers.moderate + threeLayers.high > oneLayer.moderate + oneLayer.high);
    }

    void profileChangeRecomputesAndJerseyIsAtLeastAsRisky() {
        MainWindow window;
        window.applyLoadedProject(squareProject(2));
        window.fabricRiskAct_->setChecked(true);
        window.recomputeFabricRisk();
        const auto woven = countFabricRisk(window.fabricRiskGrid_);

        auto* combo =
            window.analysisDock_->findChild<QComboBox*>(QStringLiteral("combo_fabricRiskProfile"));
        QVERIFY(combo != nullptr);
        QCOMPARE(combo->count(), 3);
        combo->setCurrentIndex(1); // jersey
        QVERIFY(window.fabricRiskTimer_->isActive());
        window.recomputeFabricRisk();
        QCOMPARE(window.fabricRiskComputeCount_, 2);
        const auto jersey = countFabricRisk(window.fabricRiskGrid_);
        QVERIFY(jersey.high >= woven.high);
        QVERIFY(jersey.moderate + jersey.high >= woven.moderate + woven.high);
    }

    void overlayDoesNotChangeTheDocument() {
        MainWindow window;
        window.applyLoadedProject(squareProject(2));
        const auto undoBefore = window.undoStack_.undoNames().size();
        const auto objectsBefore = window.project_.embroidery_objects.size();
        const auto commandsBefore = window.sequence_->commands.size();
        window.fabricRiskAct_->setChecked(true);
        window.recomputeFabricRisk();
        QCOMPARE(window.undoStack_.undoNames().size(), undoBefore);
        QCOMPARE(window.project_.embroidery_objects.size(), objectsBefore);
        QCOMPARE(window.sequence_->commands.size(), commandsBefore);
    }

    void enabledStateAndProfileAreRemembered() {
        {
            MainWindow window;
            window.fabricRiskAct_->setChecked(true);
            auto* combo = window.analysisDock_->findChild<QComboBox*>(
                QStringLiteral("combo_fabricRiskProfile"));
            combo->setCurrentIndex(2);
        }
        MainWindow again;
        QVERIFY(again.fabricRiskAct_->isChecked());
        auto* combo =
            again.analysisDock_->findChild<QComboBox*>(QStringLiteral("combo_fabricRiskProfile"));
        QCOMPARE(combo->currentIndex(), 2);
        QCOMPARE(again.fabricRiskProfile(), 2);
    }
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_fabric_risk_ui.moc"
