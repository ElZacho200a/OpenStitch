// SPDX-License-Identifier: Apache-2.0
#include "journey_harness.hpp"
#include <QFile>

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;
using namespace openstitch::desktop_v2::studio::e2e;

class Journey02 : public QObject {
    Q_OBJECT
private slots:
    void run() {
        Harness h;
        QVERIFY(h.ready());
        auto empty = project_ops::create_empty_project({});
        QVERIFY(empty.has_value());
        QVERIFY(h.useFixture(std::move(*empty)));
        QVERIFY(h.prepareSavePath());
        QVERIFY(h.drawRectangle(-12, -8, 24, 16));
        QCOMPARE(h.session.project().embroidery_objects.size(), std::size_t{1});
        QVERIFY(h.bytes().size() > 512);
        const auto drawn = h.bytes();
        QVERIFY(h.saveRoundTrip());
        h.key(Qt::Key_Z, Qt::ControlModifier);
        QVERIFY(h.settle());
        QVERIFY(h.session.project().embroidery_objects.empty());
        h.key(Qt::Key_Y, Qt::ControlModifier);
        QVERIFY(h.settle());
        QCOMPARE(h.bytes(), drawn);
        QVERIFY(h.saveRoundTrip());
    }
};
namespace {
[[maybe_unused]] const bool registered = registerJourney<Journey02>("02");
}
#include "journey_02_drawing.moc"
