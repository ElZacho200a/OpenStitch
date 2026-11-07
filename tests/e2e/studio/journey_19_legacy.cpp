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

class Journey19 : public QObject {
    Q_OBJECT
private slots:
    void run() {
        Harness h;
        QVERIFY(h.ready());
        for (int version = 1; version <= project_io::kMaxReadableSchemaVersion; ++version) {
            const auto file = std::filesystem::path(OPENSTITCH_GOLDEN_DIR) / "osp-legacy" /
                              ("schema-v" + std::to_string(version) + ".osp");
            QFile source(QString::fromStdWString(file.wstring()));
            QVERIFY(source.open(QIODevice::ReadOnly));
            const auto sourceBytes = source.readAll();
            source.close();
            project_io::LoadInfo info;
            auto expected = project_io::load_project(file, &info);
            QVERIFY2(expected.has_value(), expected ? "" : expected.error().message.c_str());
            QCOMPARE(info.fileVersion, version);
            QVERIFY(h.openFile(QString::fromStdWString(file.wstring())));
            QVERIFY(h.settle());
            QCOMPARE(h.bytes(), Harness::bytes(*expected));
            QCOMPARE(h.session.project().embroidery_objects.front().name,
                     "Legacy satin schema v" + std::to_string(version));
            QVERIFY(h.prepareSavePath(QStringLiteral("legacy-%1.osp").arg(version)));
            h.studio.selectObject(QStringLiteral("1"));
            QVERIFY(h.click("satinPanelButton"));
            QVERIFY(h.type("satinNodeX", QStringLiteral("1")));
            QVERIFY(h.type("satinNodeY", QStringLiteral("0.7")));
            const auto depth = h.steps();
            QVERIFY(h.click("satinMoveNode"));
            QTRY_COMPARE(h.steps(), depth + 1);
            QVERIFY(h.settle());
            QVERIFY(h.saveRoundTrip());
            QVERIFY(h.undoToBytes(Harness::bytes(*expected)));
            QVERIFY(h.saveRoundTrip());
            QVERIFY(source.open(QIODevice::ReadOnly));
            QCOMPARE(source.readAll(), sourceBytes);
            source.close();
            QVERIFY(h.click("satinPanelButton"));
        }
    }
};
namespace {
[[maybe_unused]] const bool registered = registerJourney<Journey19>("19");
}
#include "journey_19_legacy.moc"
