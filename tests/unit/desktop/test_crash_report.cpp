// SPDX-License-Identifier: Apache-2.0
// Rapport de plantage (HP-QA-002) : action de menu, ticket sans chemin, recherche des dumps.
// Headless (QT_QPA_PLATFORM=offscreen), sans sleep ni comparaison de pixels.
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QMenu>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QUrlQuery>

#include <chrono>
#include <filesystem>
#include <fstream>

#include "crash_report.hpp"
#include "crash_report_ticket.hpp"
#include "main_window.hpp"

namespace openstitch::desktop {

class CrashReportTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("CrashReportTest"));
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDir_.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }

    void helpMenuHasCrashReportAction() {
        MainWindow window;
        auto* act = window.findChild<QAction*>(QStringLiteral("action_crash_report"));
        QVERIFY(act != nullptr);
        QVERIFY(act->text().contains(QStringLiteral("plantage")));
        auto* help = window.findChild<QMenu*>(QStringLiteral("menu_help"));
        QVERIFY(help != nullptr);
        QVERIFY(help->actions().contains(act));
    }

    void ticketContainsNoProjectPath() {
        const QStringList tail{
            QStringLiteral("[info] projet ouvert C:\\Users\\alice\\Broderie\\mon_projet.osp"),
            QStringLiteral("[info] export /home/bob/docs/secret.dst termine"),
            QStringLiteral("[info] import D:/tmp/photo.png ok"),
            QStringLiteral("[error] echec sur logo.jpg")};
        const QString body = crashReportBody(tail, true);
        QVERIFY(!body.contains(QStringLiteral("alice")));
        QVERIFY(!body.contains(QStringLiteral("mon_projet")));
        QVERIFY(!body.contains(QStringLiteral("bob")));
        QVERIFY(!body.contains(QStringLiteral("secret")));
        QVERIFY(!body.contains(QStringLiteral("photo")));
        QVERIFY(!body.contains(QStringLiteral("logo")));
        QVERIFY(body.contains(QStringLiteral("<chemin>")));
        QVERIFY(body.contains(QStringLiteral("OpenStitch")));

        const QUrl url = crashReportUrl(tail, false);
        QCOMPARE(url.host(), QStringLiteral("github.com"));
        QVERIFY(url.path().endsWith(QStringLiteral("/issues/new")));
        const QString decoded =
            QUrlQuery(url).queryItemValue(QStringLiteral("body"), QUrl::FullyDecoded);
        QVERIFY(!decoded.contains(QStringLiteral("alice")));
        QVERIFY(url.toString().size() < 8000);
    }

    void recentDumpIsFoundOnceThenSeen() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const std::filesystem::path d = dir.path().toStdString();
        QVERIFY(!findRecentUnseenDump(d).has_value());
        std::ofstream(d / "crash-20260101-000001.dmp") << "x";
        std::ofstream(d / "crash-20260101-000002.dmp") << "y";
        const auto found = findRecentUnseenDump(d);
        QVERIFY(found.has_value());
        QCOMPARE(QString::fromStdString(found->filename().string()),
                 QStringLiteral("crash-20260101-000002.dmp"));
        QVERIFY(findLatestDump(d).has_value());
        markDumpSeen(*found);
        const auto next = findRecentUnseenDump(d);
        QVERIFY(next.has_value());
        QCOMPARE(QString::fromStdString(next->filename().string()),
                 QStringLiteral("crash-20260101-000001.dmp"));
        // Un dump trop ancien n'est plus propose.
        QVERIFY(!findRecentUnseenDump(d, std::chrono::seconds(-1)).has_value());
    }

private:
    QTemporaryDir settingsDir_;
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::CrashReportTest)
#include "test_crash_report.moc"
