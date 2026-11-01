// SPDX-License-Identifier: Apache-2.0
// Surimpression « Sauts à vide » du panneau Ordre, câblée dans MainWindow. Le calcul des segments
// est testé dans tests/unit/optimization (jump_segments). Headless, aucune comparaison de pixels,
// aucun sleep : on vérifie les données exposées (segments, éléments de scène, libellé d'écart).
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

#include "main_window.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/optimization/order.hpp"

using openstitch::Micrometers;
using openstitch::Vec2um;
using openstitch::desktop::MainWindow;

namespace {

namespace doc = openstitch::document;
namespace geo = openstitch::geometry;

void addShape(doc::Project& project, std::int32_t x0, std::int32_t side, const char* name) {
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
    emb.params = doc::TatamiParams{};
    project.vector_objects.push_back(std::move(vector));
    project.embroidery_objects.push_back(std::move(emb));
}

// Trois carrés : A (0 mm), B (60 mm), C (10 mm). L'ordre du document A, B, C fait deux grands
// allers-retours ; A, C, B (par proximité) est plus court.
doc::Project zigzagProject() {
    doc::Project project;
    project.original.width = 2;
    project.original.height = 2;
    project.original.rgba.assign(16, 255);
    addShape(project, 0, 5'000, "A");
    addShape(project, 60'000, 5'000, "B");
    addShape(project, 10'000, 5'000, "C");
    return project;
}

} // namespace

namespace openstitch::desktop {

// Nom imposé par `friend class MainWindowTest` (main_window.hpp).
class MainWindowTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir settingsDir_;

    static int itemCount(const MainWindow& window, const QString& tag) {
        int n = 0;
        for (const QGraphicsItem* item : window.scene_->items()) {
            n += item->data(0).toString() == tag ? 1 : 0;
        }
        return n;
    }
    static void chooseProximity(MainWindow& window) {
        const int index = window.orderStrategyCombo_->findData(
            static_cast<int>(optimization::OrderStrategy::ByProximity));
        window.orderStrategyCombo_->setCurrentIndex(index);
    }

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("OrderJumpsUiTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }
    void init() { QSettings().clear(); }

    void actionAndCheckBoxAreOffByDefaultAndStayInSync() {
        MainWindow window;
        QVERIFY(window.orderJumpsAct_ != nullptr);
        QVERIFY(window.orderJumpsAct_->isCheckable());
        QVERIFY(!window.orderJumpsAct_->isChecked());
        bool inMenu = false;
        for (const QMenu* menu : window.menuBar()->findChildren<QMenu*>()) {
            inMenu = inMenu || menu->actions().contains(window.orderJumpsAct_);
        }
        QVERIFY(inMenu);
        auto* check = window.orderDock_->findChild<QCheckBox*>(QStringLiteral("check_orderJumps"));
        QVERIFY(check != nullptr);
        QVERIFY(!check->isChecked());
        check->setChecked(true);
        QVERIFY(window.orderJumpsAct_->isChecked());
        window.orderJumpsAct_->setChecked(false);
        QVERIFY(!check->isChecked());
    }

    void jumpsFollowTheSewingOrderOfTheEffectiveSequence() {
        MainWindow window;
        window.applyLoadedProject(zigzagProject());
        QVERIFY(window.sequence_.has_value());
        QCOMPARE(itemCount(window, QStringLiteral("orderJumpsOverlay")), 0);
        QVERIFY(window.orderJumps_.empty());

        window.orderJumpsAct_->setChecked(true);
        // Mêmes segments que la fonction pure appliquée aux extrémités cousues.
        std::vector<optimization::SewnSpan> spans;
        for (const auto& e : stitch::object_endpoints(*window.sequence_)) {
            spans.push_back({e.first, e.last});
        }
        const auto expected = optimization::jump_segments(spans);
        QCOMPARE(expected.size(), std::size_t{2});
        QVERIFY(window.orderJumps_ == expected);
        QCOMPARE(itemCount(window, QStringLiteral("orderJumpsOverlay")), 1);
        QCOMPARE(itemCount(window, QStringLiteral("orderJumpsOverlayOld")), 0);
        QVERIFY(window.orderJumpsDeltaText_.isEmpty());

        // Un simple redessin remplace l'élément (pas de doublon).
        window.renderStitches();
        QCOMPARE(itemCount(window, QStringLiteral("orderJumpsOverlay")), 1);

        window.orderJumpsAct_->setChecked(false);
        QCOMPARE(itemCount(window, QStringLiteral("orderJumpsOverlay")), 0);
        QVERIFY(window.orderJumps_.empty());
    }

    void applyingTheOrderKeepsOldJumpsAndShowsTheGain() {
        MainWindow window;
        window.applyLoadedProject(zigzagProject());
        window.orderJumpsAct_->setChecked(true);
        const auto beforeJumps = window.orderJumps_;
        const auto beforeLength = optimization::jump_length_um(beforeJumps);
        QVERIFY(beforeLength > 0);

        chooseProximity(window);
        window.applyOrderStrategy();

        QVERIFY(window.orderJumpsHasBefore_);
        QVERIFY(window.orderJumpsBefore_ == beforeJumps);
        QVERIFY(window.orderJumps_ != beforeJumps);
        const auto afterLength = optimization::jump_length_um(window.orderJumps_);
        QVERIFY2(afterLength < beforeLength, "l'ordre par proximite doit raccourcir les sauts");
        QCOMPARE(itemCount(window, QStringLiteral("orderJumpsOverlayOld")), 1);
        QCOMPARE(itemCount(window, QStringLiteral("orderJumpsOverlay")), 1);
        QCOMPARE(itemCount(window, QStringLiteral("orderJumpsDelta")), 1);
        // Libellé « −X mm » (signe moins typographique).
        QVERIFY2(window.orderJumpsDeltaText_.startsWith(QStringLiteral("−")),
                 qPrintable(window.orderJumpsDeltaText_));
        QVERIFY(window.orderJumpsDeltaText_.endsWith(QStringLiteral(" mm")));
        auto* label = window.orderDock_->findChild<QLabel*>(QStringLiteral("label_orderJumps"));
        QVERIFY(label != nullptr);
        QVERIFY(label->text().contains(window.orderJumpsDeltaText_));

        // Décocher « comparer » retire les anciens sauts et le libellé, sans toucher aux nouveaux.
        auto* compare =
            window.orderDock_->findChild<QCheckBox*>(QStringLiteral("check_orderJumpsCompare"));
        QVERIFY(compare != nullptr);
        compare->setChecked(false);
        QCOMPARE(itemCount(window, QStringLiteral("orderJumpsOverlayOld")), 0);
        QCOMPARE(itemCount(window, QStringLiteral("orderJumpsDelta")), 0);
        QCOMPARE(itemCount(window, QStringLiteral("orderJumpsOverlay")), 1);
        compare->setChecked(true);
        QCOMPARE(itemCount(window, QStringLiteral("orderJumpsOverlayOld")), 1);
    }

    void comparisonExpiresAtTheNextRefreshAndNeverTouchesTheDocumentByItself() {
        MainWindow window;
        window.applyLoadedProject(zigzagProject());
        window.orderJumpsAct_->setChecked(true);
        chooseProximity(window);
        window.applyOrderStrategy();
        QVERIFY(window.orderJumpsHasBefore_);

        // Annuler l'optimisation est une modification suivante : la comparaison disparaît.
        window.undoStack_.undo(window.project_);
        window.refreshImage();
        QVERIFY(!window.orderJumpsHasBefore_);
        QCOMPARE(itemCount(window, QStringLiteral("orderJumpsOverlayOld")), 0);
        QVERIFY(window.orderJumpsDeltaText_.isEmpty());
    }

    void overlayAloneDoesNotChangeTheDocument() {
        MainWindow window;
        window.applyLoadedProject(zigzagProject());
        const auto undoBefore = window.undoStack_.undoNames().size();
        const auto commandsBefore = window.sequence_->commands.size();
        window.orderJumpsAct_->setChecked(true);
        window.renderStitches();
        QCOMPARE(window.undoStack_.undoNames().size(), undoBefore);
        QCOMPARE(window.sequence_->commands.size(), commandsBefore);
    }

    void applyingWithTheOverlayOffStillCapturesTheBaseline() {
        MainWindow window;
        window.applyLoadedProject(zigzagProject());
        chooseProximity(window);
        window.applyOrderStrategy();
        QVERIFY(window.orderJumpsHasBefore_);
        // Allumer la surimpression juste après l'optimisation montre déjà la comparaison.
        window.orderJumpsAct_->setChecked(true);
        QVERIFY(window.orderJumpsHasBefore_);
        QCOMPARE(itemCount(window, QStringLiteral("orderJumpsOverlayOld")), 1);
        // Une modification suivante la périme.
        window.refreshImage();
        QVERIFY(!window.orderJumpsHasBefore_);
        QCOMPARE(itemCount(window, QStringLiteral("orderJumpsOverlayOld")), 0);
    }
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_order_jumps_ui.moc"
