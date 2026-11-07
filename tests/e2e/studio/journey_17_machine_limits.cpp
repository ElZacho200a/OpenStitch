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

class Journey17 : public QObject {
    Q_OBJECT
private slots:
    void run() {
        Harness h;
        QVERIFY(h.ready());
        QVERIFY(h.rectangles());
        const auto before = h.bytes();
        h.key(Qt::Key_E, Qt::ControlModifier);
        QTRY_VERIFY(h.item("exportCheckText"));
        QVERIFY(!h.files.exportCheck(QStringLiteral("dst")).isEmpty());
        QCOMPARE(h.bytes(), before);
        QSKIP("BLOCKED: CX06 preflight-before-write correction is on a separate family branch; "
              "base #283 only warns after writing. Run complete machine/hoop export journey after "
              "Lead integration.");
    }
};
namespace {
[[maybe_unused]] const bool registered = registerJourney<Journey17>("17");
}
#include "journey_17_machine_limits.moc"
