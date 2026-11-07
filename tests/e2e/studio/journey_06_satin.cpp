// SPDX-License-Identifier: Apache-2.0
#include "journey_harness.hpp"
#include <QFile>

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;
using namespace openstitch::desktop_v2::studio::e2e;

class Journey06 : public QObject {
    Q_OBJECT
private slots:
    void run() {
        Harness h;
        QVERIFY(h.ready());
        const auto rung = [](double x, double y0, double y1) {
            return QVariantMap{{"ax", x}, {"ay", y0}, {"bx", x}, {"by", y1}};
        };
        const auto id = h.satin.createFromRungs({rung(-10, 0, 4), rung(0, 1, 5), rung(10, 0, 4)});
        QVERIFY(!id.isEmpty());
        QVERIFY(h.settle());
        QVERIFY(h.prepareSavePath());
        const auto before = h.bytes();
        QVERIFY(h.click("satinPanelButton"));
        QVERIFY(h.type("satinNodeX", QStringLiteral("-9")));
        QVERIFY(h.type("satinNodeY", QStringLiteral("0.8")));
        const auto depth = h.steps();
        QVERIFY(h.click("satinMoveNode"));
        QTRY_COMPARE(h.steps(), depth + 1);
        QVERIFY(h.settle());
        QVERIFY(h.bytes() != before);
        QVERIFY(h.saveRoundTrip());
        QVERIFY(h.undoToBytes(before));
    }
};
namespace {
[[maybe_unused]] const bool registered = registerJourney<Journey06>("06");
}
#include "journey_06_satin.moc"
