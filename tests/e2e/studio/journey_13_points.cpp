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

class Journey13 : public QObject {
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
        QVERIFY(h.click("pointEditPanelButton"));
        QVERIFY(h.click("pointEditToggle"));
        QVariantMap movable;
        for (int index = 0; index < h.points.pointCount(); ++index) {
            const auto point = h.points.pointInfo(index);
            if (point[QStringLiteral("movable")].toBool() &&
                h.points.pointAt(point[QStringLiteral("x")].toDouble(),
                                 point[QStringLiteral("y")].toDouble(), 0.05) == index) {
                movable = point;
                break;
            }
        }
        QVERIFY(!movable.isEmpty());
        QTest::mouseClick(
            h.window, Qt::LeftButton, {},
            h.at(movable[QStringLiteral("x")].toDouble(), movable[QStringLiteral("y")].toDouble()));
        QTRY_COMPARE(h.points.selectedCount(), 1);
        QVERIFY(h.type("pointMoveDx", QStringLiteral("0.6")));
        QVERIFY(h.type("pointMoveDy", QStringLiteral("0.3")));
        const auto depth = h.steps();
        QVERIFY(h.click("pointMove"));
        QTRY_COMPARE(h.steps(), depth + 1);
        QVERIFY(h.settle());
        QVERIFY(h.points.editedCount() > 0);
        QVERIFY(h.bytes() != before);
        QVERIFY(h.saveRoundTrip());
        QVERIFY(h.undoToBytes(before));
    }
};
namespace {
[[maybe_unused]] const bool registered = registerJourney<Journey13>("13");
}
#include "journey_13_points.moc"
