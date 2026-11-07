// SPDX-License-Identifier: Apache-2.0
#include "journey_harness.hpp"
#include <QFile>

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;
using namespace openstitch::desktop_v2::studio::e2e;

class Journey09 : public QObject {
    Q_OBJECT
private slots:
    void run() {
        Harness h;
        QVERIFY(h.ready());
        QVERIFY(h.rectangles());
        QVERIFY(h.prepareSavePath());
        const auto id = h.session.project().embroidery_objects.front().id.value;
        h.studio.selectObject(QString::number(id));
        const auto before = h.bytes();
        QVERIFY(h.click("tool_knife"));
        const auto depth = h.steps();
        const auto count = h.session.project().embroidery_objects.size();
        h.drag(h.at(-20, -14), h.at(-20, 14));
        QTRY_COMPARE(h.steps(), depth + 1);
        QVERIFY(h.settle());
        QVERIFY(h.session.project().embroidery_objects.size() > count);
        QVERIFY(h.saveRoundTrip());
        QVERIFY(h.undoToBytes(before));
        // Node editing is part of the same mandatory journey. Reach the real node tool.
        QVERIFY(h.click("tool_node.edit"));
        QVERIFY(h.tools.activeTool() == QStringLiteral("node.edit"));
        h.drag(h.at(-27.5, -5), h.at(-29, -6));
        QVERIFY(h.settle());
        QVERIFY(h.bytes() != before);
        QVERIFY(h.undoToBytes(before));
    }
};
namespace {
[[maybe_unused]] const bool registered = registerJourney<Journey09>("09");
}
#include "journey_09_nodes_knife.moc"
