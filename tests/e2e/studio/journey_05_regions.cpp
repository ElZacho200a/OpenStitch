// SPDX-License-Identifier: Apache-2.0
#include "journey_harness.hpp"
#include <QFile>

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;
using namespace openstitch::desktop_v2::studio::e2e;

class Journey05 : public QObject {
    Q_OBJECT
private slots:
    void run() {
        Harness h;
        QVERIFY(h.ready());
        QVERIFY(h.tentabrode());
        QVERIFY(h.prepareSavePath());
        const auto before = h.bytes();
        QVERIFY(h.click("prepareToggle"));
        QVERIFY(h.click("prepareTab_2"));
        auto* regionList = h.item("regionList");
        QVERIFY(regionList);
        const auto position = regionList->mapToScene(QPointF(regionList->width() / 2, 12));
        QTest::mouseClick(h.window, Qt::LeftButton, {}, position.toPoint());
        QTRY_COMPARE(h.regions.selectedCount(), 1);
        QVERIFY(h.type("regionColorField", QStringLiteral("#00aa55")));
        const auto depth = h.steps();
        QVERIFY(h.click("regionRecolor"));
        QTRY_COMPARE(h.steps(), depth + 1);
        QVERIFY(h.regions.stale());
        QVERIFY(h.click("regionRegenerate"));
        QVERIFY(h.settle(120000));
        QVERIFY(!h.regions.stale());
        QVERIFY(!h.bytes().empty());
        QVERIFY(h.saveRoundTrip());
        h.key(Qt::Key_Z, Qt::ControlModifier);
        QVERIFY(h.settle());
        h.key(Qt::Key_Z, Qt::ControlModifier);
        QVERIFY(h.settle());
        QCOMPARE(h.bytes(), before);
    }
};
namespace {
[[maybe_unused]] const bool registered = registerJourney<Journey05>("05");
}
#include "journey_05_regions.moc"
