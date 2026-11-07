// SPDX-License-Identifier: Apache-2.0
#include "journey_harness.hpp"
#include <QFile>

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;
using namespace openstitch::desktop_v2::studio::e2e;

class Journey07 : public QObject {
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
        const auto depth = h.steps();
        QVERIFY(h.type("entry_row_spacing_mm", QStringLiteral("0.55")));
        QTRY_COMPARE(h.steps(), depth + 1);
        QVERIFY(h.settle());
        QVERIFY(h.bytes() != before);
        QVERIFY(h.saveRoundTrip());
        QVERIFY(h.undoToBytes(before));
        const auto revision = h.session.revision();
        QVERIFY(h.type("entry_row_spacing_mm", QStringLiteral("99")));
        QCOMPARE(h.session.revision(), revision);
        QCOMPARE(h.bytes(), before);
    }
};
namespace {
[[maybe_unused]] const bool registered = registerJourney<Journey07>("07");
}
#include "journey_07_parameters.moc"
