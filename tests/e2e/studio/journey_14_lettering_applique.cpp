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

class Journey14 : public QObject {
    Q_OBJECT
private slots:
    void run() {
        Harness h;
        QVERIFY(h.ready());
        QVERIFY(h.rectangles());
        const auto before = h.bytes();
        QVERIFY(h.click("tool_text"));
        QTest::mouseClick(h.window, Qt::LeftButton, {}, h.at(0, 20));
        QCoreApplication::processEvents();
        QCOMPARE(h.bytes(), before); // choosing an input tool cannot silently create stitches
        QSKIP("BLOCKED: base #283 has text via the command palette but no complete Studio applique "
              "creation/placement workflow; full text+applique journey needs production/UX "
              "integration.");
    }
};
namespace {
[[maybe_unused]] const bool registered = registerJourney<Journey14>("14");
}
#include "journey_14_lettering_applique.moc"
