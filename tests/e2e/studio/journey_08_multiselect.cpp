// SPDX-License-Identifier: Apache-2.0
#include "journey_harness.hpp"
#include <QFile>

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;
using namespace openstitch::desktop_v2::studio::e2e;

class Journey08 : public QObject {
    Q_OBJECT
private slots:
    void run() {
        Harness h;
        QVERIFY(h.ready());
        QVERIFY(h.rectangles());
        QVERIFY(h.prepareSavePath());
        const auto before = h.bytes();
        h.key(Qt::Key_A, Qt::ControlModifier);
        QCOMPARE(h.session.selection().targets().size(), std::size_t{2});
        const auto depth = h.steps();
        h.key(Qt::Key_Right);
        QTRY_COMPARE(h.steps(), depth + 1);
        QVERIFY(h.settle());
        QVERIFY(h.click("editOpenButton"));
        QVERIFY(h.click("mirrorHButton"));
        QVERIFY(h.settle());
        QVERIFY(h.saveRoundTrip());
        h.key(Qt::Key_Z, Qt::ControlModifier);
        QVERIFY(h.settle());
        QVERIFY(h.undoToBytes(before));
        QCOMPARE(h.session.selection().targets().size(), std::size_t{2});
    }
};
namespace {
[[maybe_unused]] const bool registered = registerJourney<Journey08>("08");
}
#include "journey_08_multiselect.moc"
