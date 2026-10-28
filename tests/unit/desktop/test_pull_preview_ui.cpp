// SPDX-License-Identifier: Apache-2.0
// Prévisualisation du tirage du tissu (niveau 3) câblée dans MainWindow : carte de déplacement et
// aperçu des points décalés. Le calcul est testé dans tests/unit/stitch (test_pull_map.cpp).
// Headless, aucune comparaison de pixels, aucun sleep.
#include <QAction>
#include <QCoreApplication>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

#include "canvas_view.hpp"
#include "main_window.hpp"
#include "openstitch/document/project.hpp"

using openstitch::Micrometers;
using openstitch::Vec2um;
using openstitch::desktop::MainWindow;

namespace {

namespace doc = openstitch::document;
namespace geo = openstitch::geometry;

void addShape(doc::Project& project, std::int32_t x0, std::int32_t side,
              std::array<std::uint8_t, 3> rgb, const char* name) {
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
    emb.rgb = rgb;
    doc::TatamiParams tatami;
    tatami.row_spacing = Micrometers{300};
    emb.params = tatami;
    project.vector_objects.push_back(std::move(vector));
    project.embroidery_objects.push_back(std::move(emb));
}

// Deux aplats de couleurs différentes : un changement de fil, donc un second bloc à décaler.
doc::Project twoColors() {
    doc::Project project;
    project.original.width = 2;
    project.original.height = 2;
    project.original.rgba.assign(16, 255);
    addShape(project, 0, 10'000, {200, 30, 30}, "Rouge");
    addShape(project, 10'000, 10'000, {20, 20, 20}, "Noir");
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
            n += item->data(0).toString() == QStringLiteral("pullMapOverlay") ? 1 : 0;
        }
        return n;
    }

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("PullPreviewUiTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }
    void init() { QSettings().clear(); }

    void actionsLiveInTheViewMenuAndAreOffByDefault() {
        MainWindow window;
        QVERIFY(window.pullMapAct_ != nullptr && window.pullPreviewAct_ != nullptr);
        QVERIFY(window.pullMapAct_->isCheckable() && window.pullPreviewAct_->isCheckable());
        QVERIFY(!window.pullMapAct_->isChecked());
        QVERIFY(!window.pullPreviewAct_->isChecked());
        bool inViewMenu = false;
        for (const QMenu* menu : window.menuBar()->findChildren<QMenu*>()) {
            if (menu->actions().contains(window.pullMapAct_) &&
                menu->actions().contains(window.pullPreviewAct_)) {
                inViewMenu = menu->title().contains(QStringLiteral("Affichage"));
            }
        }
        QVERIFY(inViewMenu);
    }

    void mapOverlayIsDrawnCachedAndRemoved() {
        MainWindow window;
        window.applyLoadedProject(twoColors());
        QVERIFY(window.sequence_.has_value());
        QCOMPARE(overlayCount(window), 0);

        window.pullMapAct_->setChecked(true);
        QCOMPARE(overlayCount(window), 1);
        QCOMPARE(window.pullComputeCount_, 1);

        window.renderStitches(); // même séquence, même tissu : le cache est réutilisé
        QCOMPARE(overlayCount(window), 1);
        QCOMPARE(window.pullComputeCount_, 1);

        window.pullMapAct_->setChecked(false);
        QCOMPARE(overlayCount(window), 0);
    }

    void projectFabricFeedsDefaultPredictionButNotExplicitPreviewPresets() {
        auto project = twoColors();
        project.fabric.material = "jersey";
        project.fabric.stabilizer = "cut-away";
        MainWindow window;
        window.applyLoadedProject(project);
        const auto fromProject = window.pullFabric();
        QVERIFY(std::abs(fromProject.gain - 0.020 * 1.20 * 0.80) < 1e-9);
        auto* linen = window.findChild<QAction*>(QStringLiteral("action_pullFabric_linen"));
        QVERIFY(linen != nullptr);
        linen->setChecked(true);
        QVERIFY(std::abs(window.pullFabric().gain - 0.030) < 1e-9);
    }

    void changingTheFabricInvalidatesTheMap() {
        MainWindow window;
        window.applyLoadedProject(twoColors());
        window.pullMapAct_->setChecked(true);
        QCOMPARE(window.pullComputeCount_, 1);
        auto* linen = window.findChild<QAction*>(QStringLiteral("action_pullFabric_linen"));
        QVERIFY(linen != nullptr);
        linen->setChecked(true);
        QCOMPARE(window.pullComputeCount_, 2);
        QCOMPARE(overlayCount(window), 1);
    }

    void previewShiftsLaterColorBlocksOnly() {
        MainWindow window;
        window.applyLoadedProject(twoColors());
        QVERIFY(window.pullPreviewSequence() == nullptr); // éteint : rien n'est calculé
        QCOMPARE(window.pullComputeCount_, 0);

        window.pullPreviewAct_->setChecked(true);
        const auto* shown = window.pullPreviewSequence();
        QVERIFY(shown != nullptr);
        QCOMPARE(shown->commands.size(), window.sequence_->commands.size());
        bool moved = false;
        for (std::size_t i = 0; i < shown->commands.size(); ++i) {
            moved = moved || !(shown->commands[i].pos == window.sequence_->commands[i].pos);
        }
        QVERIFY(moved);
        QVERIFY(window.pullPreviewSequence() == shown); // en cache : même objet, pas de recalcul
        QCOMPARE(window.pullComputeCount_, 1);
        // Le document n'est jamais touché : la séquence effective reste celle du moteur.
        QVERIFY(window.sequence_->commands.front().pos == shown->commands.front().pos);
    }
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_pull_preview_ui.moc"
