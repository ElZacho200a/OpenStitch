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

class Journey16 : public QObject {
    Q_OBJECT
private slots:
    void run() {
        Harness h;
        QVERIFY(h.ready());
        QVERIFY(h.rectangles());
        const auto before = h.bytes();
        const auto revision = h.session.revision();
        QVERIFY(h.click("simulationButton"));
        QTRY_VERIFY(h.view.simActive());
        QVERIFY(h.view.simTotal() > 0);
        QVERIFY(h.click("simNextStitch"));
        QCOMPARE(h.view.simPosition(), 1);
        QVERIFY(h.click("simToEnd"));
        QCOMPARE(h.view.simPosition(), h.view.simTotal());
        QVERIFY(h.click("simPrevColour"));
        QCOMPARE(h.view.simPosition(), 0);
        QVERIFY(h.click("simClose"));
        QVERIFY(!h.view.simActive());
        QCOMPARE(h.session.revision(), revision);
        QCOMPARE(h.bytes(), before);
    }
};
namespace {
[[maybe_unused]] const bool registered = registerJourney<Journey16>("16");
}
#include "journey_16_simulation.moc"
