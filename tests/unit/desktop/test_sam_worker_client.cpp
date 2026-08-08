// SPDX-License-Identifier: Apache-2.0
#include <QDir>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include "sam_worker_client.hpp"

using openstitch::desktop::AiRuntimeKind;
using openstitch::desktop::SamSegmentParams;
using openstitch::desktop::SamWorkerClient;
using openstitch::desktop::SamWorkerConfig;

namespace {

QString findPython() {
    for (const char* name : {"python", "python3"}) {
        const QString path = QStandardPaths::findExecutable(QString::fromLatin1(name));
        if (!path.isEmpty()) {
            return path;
        }
    }
    return {};
}

QString fakeWorkerScript() {
    return QStringLiteral(OPENSTITCH_FIXTURES_DIR "/fake_sam_worker.py");
}

}  // namespace

// SamWorkerClient contre un faux worker Python (tests/fixtures/fake_sam_worker.py,
// stdlib uniquement) : jamais de dépendance à un vrai environnement SAM 2/
// torch en CI. Le worker réel n'est exercé qu'au scénario manuel
// (docs/source). Le test entier est ignoré (QSKIP) si aucun interpréteur
// Python n'est trouvé sur PATH.
class SamWorkerClientTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void startReachesReady();
    void loadModelEmitsModelReady();
    void segmentImageEmitsProgressAndResult();
    void cancelDuringHangProfileEmitsCancelled();
    void crashIsDetectedAndAutoRestarted();

private:
    [[nodiscard]] SamWorkerConfig makeConfig(const QString& device = QStringLiteral("cpu")) const;

    QString pythonPath_;
};

void SamWorkerClientTest::initTestCase() {
    pythonPath_ = findPython();
    if (pythonPath_.isEmpty()) {
        QSKIP("Aucun interpréteur Python trouvé sur PATH -- test d'intégration ignoré.");
    }
}

SamWorkerConfig SamWorkerClientTest::makeConfig(const QString& device) const {
    SamWorkerConfig config;
    config.runtime = AiRuntimeKind::NativePython;
    config.pythonExecutable = pythonPath_;
    config.workerScriptPath = fakeWorkerScript();
    config.modelsDir = QDir::tempPath();
    config.device = device;
    return config;
}

void SamWorkerClientTest::startReachesReady() {
    SamWorkerClient client;
    client.configure(makeConfig());
    client.start();
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), SamWorkerClient::State::Ready, 5000);
    client.stop();
}

void SamWorkerClientTest::loadModelEmitsModelReady() {
    SamWorkerClient client;
    client.configure(makeConfig());
    client.start();
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), SamWorkerClient::State::Ready, 5000);

    QSignalSpy modelReadySpy(&client, &SamWorkerClient::modelReady);
    client.loadModel(openstitch::ai_segmentation::ModelId::Tiny);
    QVERIFY(modelReadySpy.wait(5000));
    QCOMPARE(modelReadySpy.count(), 1);
    const auto args = modelReadySpy.takeFirst();
    QCOMPARE(args.at(0).toString(), QStringLiteral("tiny"));
    QCOMPARE(args.at(1).toString(), QStringLiteral("cpu"));
    client.stop();
}

void SamWorkerClientTest::segmentImageEmitsProgressAndResult() {
    QTemporaryDir jobDir;
    QVERIFY(jobDir.isValid());

    SamWorkerClient client;
    client.configure(makeConfig());
    client.start();
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), SamWorkerClient::State::Ready, 5000);
    client.loadModel(openstitch::ai_segmentation::ModelId::Tiny);
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), SamWorkerClient::State::Ready, 5000);

    QSignalSpy progressSpy(&client, &SamWorkerClient::progress);
    QSignalSpy resultSpy(&client, &SamWorkerClient::segmentResult);
    const SamSegmentParams params;
    client.segmentImage(jobDir.path(), QStringLiteral("input.png"), params);
    QVERIFY(resultSpy.wait(5000));
    QVERIFY(progressSpy.count() >= 2);
    QCOMPARE(resultSpy.count(), 1);
    const auto args = resultSpy.takeFirst();
    QCOMPARE(args.at(2).toString(), QStringLiteral("masks.json"));
    QCOMPARE(args.at(3).toInt(), 0);
    client.stop();
}

void SamWorkerClientTest::cancelDuringHangProfileEmitsCancelled() {
    QTemporaryDir jobDir;
    QVERIFY(jobDir.isValid());

    SamWorkerClient client;
    client.configure(makeConfig());
    client.start();
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), SamWorkerClient::State::Ready, 5000);
    client.loadModel(openstitch::ai_segmentation::ModelId::Tiny);
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), SamWorkerClient::State::Ready, 5000);

    QSignalSpy progressSpy(&client, &SamWorkerClient::progress);
    SamSegmentParams params;
    params.profile = QStringLiteral("hang");
    const QString requestId = client.segmentImage(jobDir.path(), QStringLiteral("input.png"), params);
    QVERIFY(progressSpy.wait(2000));  // au moins "preparing_image"

    QSignalSpy cancelledSpy(&client, &SamWorkerClient::requestCancelled);
    client.cancel(requestId);
    QVERIFY(cancelledSpy.wait(5000));
    QCOMPARE(cancelledSpy.takeFirst().at(0).toString(), requestId);
    client.stop();
}

void SamWorkerClientTest::crashIsDetectedAndAutoRestarted() {
    SamWorkerClient client;
    client.configure(makeConfig(QStringLiteral("crash-after-ready")));
    QSignalSpy crashedSpy(&client, &SamWorkerClient::crashed);
    client.start();
    QVERIFY(crashedSpy.wait(5000));
    // Redémarrages automatiques bornés : le faux worker plante à chaque
    // tentative (device toujours "crash-after-ready"), donc l'état finit
    // par se stabiliser sur Unavailable une fois kMaxAutoRestarts épuisés.
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), SamWorkerClient::State::Unavailable, 15000);
    QVERIFY(crashedSpy.count() >= 1);
}

QTEST_MAIN(SamWorkerClientTest)
#include "test_sam_worker_client.moc"
