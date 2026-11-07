// SPDX-License-Identifier: Apache-2.0
#include "journey_harness.hpp"
#include <QAccessible>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QSet>
#include <algorithm>

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;
using namespace openstitch::desktop_v2::studio::e2e;

class Journey20 : public QObject {
    Q_OBJECT
private slots:
    void run() {
        QTemporaryDir state;
        QVERIFY(state.isValid());
        const auto expectedPath = state.filePath(QStringLiteral("before-interruption.osp"));
        QProcess writer;
        writer.start(QCoreApplication::applicationFilePath(),
                     {QStringLiteral("--prepare-interrupted-session"), state.path(), expectedPath});
        QVERIFY(writer.waitForStarted());
        QVERIFY(writer.waitForFinished(60000));
        QCOMPARE(writer.exitStatus(), QProcess::NormalExit);
        QCOMPARE(writer.exitCode(), 86); // _Exit: no controller/application destructors ran
        auto expected =
            project_io::load_project(std::filesystem::path(expectedPath.toStdWString()));
        QVERIFY(expected.has_value());
        Harness recovered(state.path());
        QVERIFY(recovered.ready());
        QVERIFY(recovered.files.hasRecovery());
        QVERIFY(recovered.click("recoveryRecover"));
        QVERIFY(recovered.settle());
        QVERIFY(!recovered.files.hasRecovery());
        QVERIFY(recovered.session.dirty());
        QCOMPARE(recovered.bytes(), Harness::bytes(*expected));
        QVERIFY(recovered.prepareSavePath());
        QVERIFY(recovered.saveRoundTrip());
        // Saving removes the interrupted session and prevents another recovery prompt.
        FileController next(recovered.studio, &recovered.actions, state.path());
        QVERIFY(!next.hasRecovery());
    }
};
namespace {
[[maybe_unused]] const bool registered = registerJourney<Journey20>("20");
}
#include "journey_20_recovery.moc"
