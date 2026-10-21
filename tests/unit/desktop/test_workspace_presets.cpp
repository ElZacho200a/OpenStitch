// SPDX-License-Identifier: Apache-2.0
#include <QAction>
#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include "main_window.hpp"

namespace openstitch::desktop {

// Accès aux docks de MainWindow par la déclaration friend existante.
class MainWindowTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchWorkspaceTest"));
        QCoreApplication::setApplicationName(QStringLiteral("WorkspacePresets"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }

    void init() { QSettings().clear(); }

    void presetsAreAvailable() {
        MainWindow window;
        QVERIFY(window.findChild<QAction*>(QStringLiteral("action_workspaceDigitize")));
        QVERIFY(window.findChild<QAction*>(QStringLiteral("action_workspaceRetouch")));
        QVERIFY(window.findChild<QAction*>(QStringLiteral("action_workspaceStitches")));
        QVERIFY(window.findChild<QAction*>(QStringLiteral("action_workspaceQuality")));
        QVERIFY(window.findChild<QAction*>(QStringLiteral("action_workspaceRestore")));
    }

    void profilesAdaptPanelsAndRestorePreviousState() {
        MainWindow window;
        window.show();
        const bool initialAnalysisHidden = window.analysisDock_->isHidden();
        const bool initialHistoryHidden = window.historyDock_->isHidden();
        const QByteArray previous = window.saveState(2);

        QAction* retouch = window.findChild<QAction*>(QStringLiteral("action_workspaceRetouch"));
        QAction* verify = window.findChild<QAction*>(QStringLiteral("action_workspaceQuality"));
        QAction* restore = window.findChild<QAction*>(QStringLiteral("action_workspaceRestore"));

        retouch->trigger();
        QVERIFY(QSettings().contains(QStringLiteral("ui/workspaces/previous")));
        QCOMPARE(QSettings().value(QStringLiteral("ui/workspaces/active")).toInt(), 1);
        QVERIFY(!window.propertiesDock_->isHidden());
        QVERIFY(window.analysisDock_->isHidden());

        verify->trigger();
        // Changer de preset ne remplace pas la vraie disposition d'origine.
        QCOMPARE(QSettings().value(QStringLiteral("ui/workspaces/previous")).toByteArray(),
                 previous);
        QCOMPARE(QSettings().value(QStringLiteral("ui/workspaces/active")).toInt(), 3);
        QVERIFY(!window.analysisDock_->isHidden());

        restore->trigger();
        QVERIFY(!QSettings().contains(QStringLiteral("ui/workspaces/previous")));
        QVERIFY(!QSettings().contains(QStringLiteral("ui/workspaces/active")));
        QCOMPARE(window.analysisDock_->isHidden(), initialAnalysisHidden);
        QCOMPARE(window.historyDock_->isHidden(), initialHistoryHidden);
    }

    void noPreviousLayoutIsHarmless() {
        MainWindow window;
        auto* restore = window.findChild<QAction*>(QStringLiteral("action_workspaceRestore"));
        const QByteArray before = window.saveState(2);
        restore->trigger();
        QCOMPARE(window.saveState(2), before);
    }

private:
    QTemporaryDir settingsDir_;
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_workspace_presets.moc"
