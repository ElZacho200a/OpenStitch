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

class Journey15 : public QObject {
    Q_OBJECT
private slots:
    void run() {
        Harness h;
        QVERIFY(h.ready());
        QVERIFY(h.rectangles());
        QVERIFY(h.prepareSavePath());
        const auto before = h.bytes();
        QVERIFY(h.click("threadsPanelButton"));
        QVERIFY(h.click("threadRow0"));
        QVERIFY(h.type("threadRecolorField", QStringLiteral("#ee3388")));
        const auto depth = h.steps();
        QVERIFY(h.click("threadRecolorButton"));
        QTRY_COMPARE(h.steps(), depth + 1);
        QVERIFY(h.settle());
        QVERIFY(h.type("colorwayNameField", QStringLiteral("E2E alternate")));
        QVERIFY(h.click("colorwayAddButton"));
        QTRY_VERIFY(!h.threads.colorways().empty());
        QVERIFY(h.saveRoundTrip());
        h.key(Qt::Key_Z, Qt::ControlModifier);
        QVERIFY(h.settle());
        QVERIFY(h.undoToBytes(before));
        // Catalog search is presentation only and must preserve machine output.
        QVERIFY(h.click("searchButton"));
        QCOMPARE(h.bytes(), before);
    }
};
namespace {
[[maybe_unused]] const bool registered = registerJourney<Journey15>("15");
}
#include "journey_15_threads.moc"
