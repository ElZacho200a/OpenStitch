// SPDX-License-Identifier: Apache-2.0
#include "journey_harness.hpp"
#include <QFile>

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;
using namespace openstitch::desktop_v2::studio::e2e;

class Journey10 : public QObject {
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
        QVERIFY(h.click("fadePanelButton"));
        QVERIFY(h.type("fadeColorField", QStringLiteral("#1e65c8")));
        QVERIFY(h.click("fadeCreate"));
        QTRY_VERIFY(h.fade.hasFade());
        QVERIFY(h.settle());
        const auto created = h.bytes();
        QVERIFY(created != before);
        QVERIFY(h.type("fadeHandleX", QStringLiteral("-25")));
        QVERIFY(h.type("fadeHandleY", QStringLiteral("-2")));
        QVERIFY(h.click("fadeHandleApply"));
        QVERIFY(h.settle());
        QVERIFY(h.bytes() != created);
        QVERIFY(h.saveRoundTrip());
        QVERIFY(h.undoToBytes(created));
        QVERIFY(h.undoToBytes(before));
    }
};
namespace {
[[maybe_unused]] const bool registered = registerJourney<Journey10>("10");
}
#include "journey_10_fade.moc"
