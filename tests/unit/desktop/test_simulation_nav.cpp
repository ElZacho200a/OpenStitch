// SPDX-License-Identifier: Apache-2.0
// HP-SIM-001/002/004 : barre de navigation de la simulation (pas, objet), temps écoulé et
// vitesse « machine ». Headless, sans pixels ni sleep.
#include <QAction>
#include <QComboBox>
#include <QCoreApplication>
#include <QLabel>
#include <QSettings>
#include <QSlider>
#include <QTemporaryDir>
#include <QTest>

#include <vector>

#include "main_window.hpp"
#include "openstitch/document/project.hpp"
#include "simulation_nav_bar.hpp"

using openstitch::Micrometers;
using openstitch::ObjectId;
using openstitch::Vec2um;
using openstitch::desktop::MainWindow;
using openstitch::desktop::SimulationNavBar;

namespace {

namespace doc = openstitch::document;
namespace geo = openstitch::geometry;

doc::Project twoObjectProject() {
    doc::Project project;
    project.original.width = 2;
    project.original.height = 2;
    project.original.rgba.assign(2 * 2 * 4, 255);
    for (int k = 0; k < 2; ++k) {
        doc::VectorObject vec;
        vec.id = project.object_ids.next();
        vec.name = "Square";
        geo::Path square;
        square.closed = true;
        const std::int32_t s = 10'000;
        const std::int32_t o = k * 30'000;
        for (const auto& [x, y] : std::vector<std::pair<std::int32_t, std::int32_t>>{
                 {o, 0}, {o + s, 0}, {o + s, s}, {o, s}}) {
            square.nodes.push_back(geo::PathNode{Vec2um{Micrometers{x}, Micrometers{y}},
                                                 geo::NodeType::Corner, std::nullopt,
                                                 std::nullopt});
        }
        vec.paths.push_back(geo::PathSet{square, {}});
        doc::EmbroideryObject emb;
        emb.id = project.object_ids.next();
        emb.name = "Carre";
        emb.source_vector = vec.id;
        emb.params = doc::RunningStitchParams{};
        project.vector_objects.push_back(std::move(vec));
        project.embroidery_objects.push_back(std::move(emb));
    }
    return project;
}

} // namespace

namespace openstitch::desktop {

// Nom imposé par `friend class MainWindowTest` (main_window.hpp).
class MainWindowTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("SimulationNavTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }

    void init() { QSettings().clear(); }

    void stepButtonsMoveTheSimulationCursorByOnePoint() {
        MainWindow window;
        window.applyLoadedProject(twoObjectProject());
        QVERIFY(window.sequence_.has_value());
        QVERIFY(window.simNavBar_ != nullptr);
        auto* forward =
            window.simNavBar_->findChild<QAction*>(QStringLiteral("action_simStepForward"));
        auto* back = window.simNavBar_->findChild<QAction*>(QStringLiteral("action_simStepBack"));
        QVERIFY(forward != nullptr && back != nullptr);
        QVERIFY(forward->isEnabled());
        window.simSlider_->setValue(3);
        QVERIFY(window.simulating());
        forward->trigger();
        QCOMPARE(window.simStep_, 4);
        back->trigger();
        back->trigger();
        QCOMPARE(window.simStep_, 2);
    }

    void objectButtonsJumpToObjectBoundaries() {
        MainWindow window;
        window.applyLoadedProject(twoObjectProject());
        QVERIFY(window.sequence_.has_value());
        auto* next = window.simNavBar_->findChild<QAction*>(QStringLiteral("action_simNextObject"));
        auto* prev = window.simNavBar_->findChild<QAction*>(QStringLiteral("action_simPrevObject"));
        QVERIFY(next != nullptr && prev != nullptr);
        window.simSlider_->setValue(1);
        next->trigger();
        const int second = window.simStep_;
        QVERIFY(second > 1);
        QVERIFY(window.sequence_->commands[static_cast<std::size_t>(second)].source !=
                window.sequence_->commands[static_cast<std::size_t>(second - 1)].source);
        prev->trigger();
        QCOMPARE(window.simStep_, 0);
    }

    void timeLabelGrowsWithTheCursorAndEndsAtTotal() {
        MainWindow window;
        window.applyLoadedProject(twoObjectProject());
        QVERIFY(window.sequence_.has_value());
        const QStringList parts = window.simNavBar_->timeText().split(QStringLiteral(" / "));
        QCOMPARE(parts.size(), 2);
        QCOMPARE(parts[0], parts[1]); // couture complète : écoulé == total
        window.simSlider_->setValue(0);
        QVERIFY(window.simNavBar_->timeText().startsWith(QStringLiteral("0:00 / ")));
    }

    void machineSpeedIsOfferedAndAdvancesAtMachineRate() {
        MainWindow window;
        window.applyLoadedProject(twoObjectProject());
        QVERIFY(window.simSpeedCombo_->findData(-1.0) >= 0);
        QVERIFY(window.simSpeedCombo_->findData(64.0) >= 0);
        window.simSpeedCombo_->setCurrentIndex(window.simSpeedCombo_->findData(-1.0));
        window.simSlider_->setValue(0);
        window.simStep_ = 0;
        window.simAccum_ = 0.0;
        for (int i = 0; i < 100; ++i) { // 100 ticks x 16 ms x (700/60) pas/s ~ 18 pas
            window.onSimTick();
        }
        QVERIFY(window.simStep_ >= 17 && window.simStep_ <= 19);
    }

private:
    QTemporaryDir settingsDir_;
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_simulation_nav.moc"
