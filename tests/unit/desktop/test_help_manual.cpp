// SPDX-License-Identifier: Apache-2.0
// Manuel en ligne, aide contextuelle (Maj+F1) et tutoriel guide (HP-HELP-001/002).
// Headless (QT_QPA_PLATFORM=offscreen), sans sleep ni comparaison de pixels.
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDockWidget>
#include <QFile>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenu>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBrowser>

#include "help_manual.hpp"
#include "main_window.hpp"
#include "tools.hpp"

namespace openstitch::desktop {

class HelpManualTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("HelpManualTest"));
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDir_.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }

    // Garde doc/IHM : la ressource embarquee est le guide, et chaque section citee par l'aide
    // contextuelle existe vraiment (un titre renomme dans user-guide.md fait echouer ce test).
    void manualResourceIsEmbedded() {
        const QString md = manualMarkdown();
        QVERIFY(md.size() > 10000);
        QVERIFY(md.contains(QStringLiteral("## Menu Aide")));
    }

    void everyContextHeadingExistsInTheGuide() {
        const QStringList headings = markdownHeadings(manualMarkdown());
        QVERIFY(!headings.isEmpty());
        for (const QString& h : manualPanelHeadings()) {
            QVERIFY2(headings.contains(h), qPrintable(h));
        }
        for (int i = static_cast<int>(Tool::Select); i <= static_cast<int>(Tool::ClickToStitch);
             ++i) {
            const QString h = manualHeadingForTool(static_cast<Tool>(i));
            QVERIFY2(headings.contains(h), qPrintable(h));
        }
    }

    void guideEmbeddedIsTheSourceFile() {
        QFile source(QStringLiteral(OPENSTITCH_SOURCE_DIR "/docs/source/user-guide.md"));
        QVERIFY(source.open(QIODevice::ReadOnly));
        const QString onDisk = QString::fromUtf8(source.readAll())
                                   .replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        QString embedded = manualMarkdown();
        embedded.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        QCOMPARE(embedded, onDisk);
    }

    void manualDialogScrollsToTopicAndSearches() {
        ManualDialog dialog;
        QVERIFY(dialog.browser()->document()->characterCount() > 10000);
        QVERIFY(dialog.showTopic(QStringLiteral("Menu Texte (lettrage)")));
        QCOMPARE(dialog.currentTopic(), QStringLiteral("Menu Texte (lettrage)"));
        QVERIFY(!dialog.showTopic(QStringLiteral("Section inexistante")));
        QVERIFY(dialog.search(QStringLiteral("tatami")));
        QVERIFY(!dialog.search(QStringLiteral("zzqxzzqx-introuvable")));
        QVERIFY(!dialog.search(QString()));
    }

    void helpMenuOffersManualContextAndTutorial() {
        MainWindow window;
        auto* context = window.findChild<QAction*>(QStringLiteral("action_help_context"));
        auto* manual = window.findChild<QAction*>(QStringLiteral("action_help_manual"));
        auto* tour = window.findChild<QAction*>(QStringLiteral("action_help_tutorial"));
        QVERIFY(context != nullptr && manual != nullptr && tour != nullptr);
        QCOMPARE(context->shortcut(), QKeySequence(Qt::SHIFT | Qt::Key_F1));
        QVERIFY(manual->shortcut().isEmpty());
        for (const QAction* a : {context, manual, tour}) {
            QVERIFY2(!a->toolTip().isEmpty() && !a->statusTip().isEmpty(),
                     qPrintable(a->objectName()));
        }
        // F1 reste le raccourci des gestes (pas de collision).
        auto* gestures = window.findChild<QAction*>(QStringLiteral("action_help_gestures"));
        QCOMPARE(gestures->shortcut(), QKeySequence(Qt::Key_F1));
    }

    void contextualHelpFollowsActiveTool() {
        MainWindow window;
        window.setTool(Tool::Text);
        QCOMPARE(window.contextualHelpHeading(), manualHeadingForTool(Tool::Text));
        window.setTool(Tool::Measure);
        QCOMPARE(window.contextualHelpHeading(), manualHeadingForTool(Tool::Measure));
        window.showContextualHelp();
        auto* dialog = window.findChild<ManualDialog*>();
        QVERIFY(dialog != nullptr);
        QVERIFY(dialog->isVisible());
        QCOMPARE(dialog->currentTopic(), manualHeadingForTool(Tool::Measure));
        // Une seule instance, reutilisee.
        window.showManual();
        QCOMPARE(window.findChildren<ManualDialog*>().size(), 1);
    }

    void tutorialSampleImageIsDeterministicAndColored() {
        const QImage a = tutorialSampleImage();
        const QImage b = tutorialSampleImage();
        QVERIFY(!a.isNull());
        QCOMPARE(a, b);
        QCOMPARE(a.pixelColor(140, 160), QColor(200, 40, 50));
        QCOMPARE(a.pixelColor(340, 100), QColor(40, 90, 190));
        QCOMPARE(a.pixelColor(360, 290), QColor(240, 190, 30));
        QCOMPARE(a.pixelColor(5, 5), QColor(250, 250, 250));
    }

private:
    QTemporaryDir settingsDir_;
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::HelpManualTest)
#include "test_help_manual.moc"
