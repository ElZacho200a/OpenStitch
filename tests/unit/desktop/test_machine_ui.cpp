// SPDX-License-Identifier: Apache-2.0
// Affichage > Machine… (HP-HOOP-002) et forme du cadre (HP-HOOP-001) : dialogue, application
// d'un profil + cadre en UN pas d'annulation, forme dessinee par le canevas. Headless, sans
// pixels. Les verifications elles-memes sont testees dans tests/unit/stitch_analysis.
#include <QAction>
#include <QComboBox>
#include <QCoreApplication>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include "canvas_view.hpp"
#include "machine_dialog.hpp"
#include "main_window.hpp"
#include "openstitch/document/machine_profile.hpp"
#include "openstitch/document/project.hpp"

using openstitch::desktop::MachineDialog;
using openstitch::desktop::MainWindow;
namespace doc = openstitch::document;

namespace openstitch::desktop {

// Nom impose par `friend class MainWindowTest` (main_window.hpp).
class MainWindowTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir settingsDir_;

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("MachineUiTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }
    void init() { QSettings().clear(); }

    void dialogListsProfilesAndTheirHoops() {
        MachineDialog dialog{QString()};
        auto* machines = dialog.findChild<QComboBox*>(QStringLiteral("machineCombo"));
        auto* hoops = dialog.findChild<QComboBox*>(QStringLiteral("machineHoopCombo"));
        auto* details = dialog.findChild<QLabel*>(QStringLiteral("machineDetailsLabel"));
        QVERIFY(machines != nullptr && hoops != nullptr && details != nullptr);
        QCOMPARE(machines->count(), 1 + static_cast<int>(doc::machine_profiles().size()));
        QVERIFY(dialog.machineId().isEmpty());
        QVERIFY(!hoops->isEnabled()); // aucune machine : rien a choisir

        machines->setCurrentIndex(machines->findData(QStringLiteral("generic-multi-6")));
        QCOMPARE(dialog.machineId(), QStringLiteral("generic-multi-6"));
        QVERIFY(hoops->isEnabled());
        QVERIFY(details->text().contains(QStringLiteral("Aiguilles : 6")));
        QVERIFY(dialog.hoopId().isEmpty()); // par defaut : garder le cadre

        hoops->setCurrentIndex(hoops->findData(QStringLiteral("round-150")));
        QCOMPARE(dialog.hoopId(), QStringLiteral("round-150"));
    }

    void dialogPreselectsTheProjectMachine() {
        MachineDialog dialog(QStringLiteral("generic-single-100"));
        QCOMPARE(dialog.machineId(), QStringLiteral("generic-single-100"));
    }

    void machineChoiceAndHoopUndoInOneStep() {
        MainWindow window;
        QVERIFY(window.machineAct_ != nullptr);
        bool inViewMenu = false;
        for (const QMenu* menu : window.menuBar()->findChildren<QMenu*>()) {
            inViewMenu = inViewMenu || menu->actions().contains(window.machineAct_);
        }
        QVERIFY(inViewMenu);
        QVERIFY(window.project_.machine_id.empty());

        const bool wasClean = window.undoStack_.canUndo();
        window.applyMachineChoice(QStringLiteral("generic-multi-6"), QStringLiteral("round-150"));
        QCOMPARE(QString::fromStdString(window.project_.machine_id),
                 QStringLiteral("generic-multi-6"));
        QCOMPARE(window.project_.canvas.shape, doc::HoopShape::Ellipse);
        QCOMPARE(window.project_.canvas.width.value, 150'000);
        // Le canevas dessine la forme du projet.
        QCOMPARE(window.view_->hoopShape(), doc::HoopShape::Ellipse);

        // Un seul pas d'annulation pour machine + cadre.
        QVERIFY(window.undoStack_.undo(window.project_));
        QVERIFY(window.project_.machine_id.empty());
        QCOMPARE(window.project_.canvas.shape, doc::HoopShape::Rectangle);
        QCOMPARE(window.undoStack_.canUndo(), wasClean);
    }

    void removingTheMachineKeepsTheHoop() {
        MainWindow window;
        window.applyMachineChoice(QStringLiteral("generic-single-130x180"), QString());
        const auto canvas = window.project_.canvas;
        window.applyMachineChoice(QString(), QString());
        QVERIFY(window.project_.machine_id.empty());
        QVERIFY(window.project_.canvas == canvas);
    }
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_machine_ui.moc"
