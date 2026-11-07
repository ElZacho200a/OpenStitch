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

class Journey18 : public QObject {
    Q_OBJECT
private slots:
    void run() {
        Harness h;
        QVERIFY(h.ready());
        QVERIFY(h.rectangles());
        const auto before = h.bytes();
        QVERIFY(h.click("fileMenuButton"));
        h.key(Qt::Key_Escape);
        QCOMPARE(h.bytes(), before);
        QSKIP("BLOCKED: no CompanionController/QML proposal review surface on base #283. A core "
              "LiveBridge invocation would bypass the required human preview/approval UI, so it is "
              "not substituted.");
    }
};
namespace {
[[maybe_unused]] const bool registered = registerJourney<Journey18>("18");
}
#include "journey_18_companion.moc"
