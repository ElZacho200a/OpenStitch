// SPDX-License-Identifier: Apache-2.0
#include "journey_harness.hpp"
#include <QFile>

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;
using namespace openstitch::desktop_v2::studio::e2e;

class Journey11 : public QObject {
    Q_OBJECT
private slots:
    void run() {
        Harness h;
        QVERIFY(h.ready());
        QVERIFY(h.rectangles());
        QVERIFY(h.prepareSavePath());
        h.studio.selectObject(
            QString::number(h.session.project().embroidery_objects.front().id.value));
        const auto before = h.bytes();
        QVERIFY(h.type("entry_pull_compensation_mm", QStringLiteral("0.4")));
        QVERIFY(h.settle());
        const auto compensated = h.bytes();
        QVERIFY(compensated != before);
        QVERIFY(h.click("analysisPanelButton"));
        QVERIFY(h.click("densityButton"));
        QTRY_VERIFY_WITH_TIMEOUT(h.analysis.densityReady(), 30000);
        QCOMPARE(h.bytes(), compensated);
        QVERIFY(!h.analysis.densitySummary().isEmpty());
        QVERIFY(h.saveRoundTrip());
        QVERIFY(h.undoToBytes(before));
    }
};
namespace {
[[maybe_unused]] const bool registered = registerJourney<Journey11>("11");
}
#include "journey_11_pull_density.moc"
