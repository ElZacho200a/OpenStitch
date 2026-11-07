// SPDX-License-Identifier: Apache-2.0
#include "journey_harness.hpp"
#include <QFile>

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;
using namespace openstitch::desktop_v2::studio::e2e;

class Journey01 : public QObject {
    Q_OBJECT
private slots:
    void run() {
        Harness h;
        QVERIFY2(h.ready(), qPrintable(h.warnings.join('\n')));
        QVERIFY(h.openFile(QString::fromStdWString(Harness::fixture("tentabrode.png").wstring())));
        QTRY_VERIFY(h.item("importConfirm"));
        QVERIFY(h.type("importHeight", QStringLiteral("300")));
        QCOMPARE(h.item("importHeight")->property("value").toInt(), 300);
        QVERIFY(h.click("importConfirm"));
        QVERIFY(h.settle(120000));
        QVERIFY(h.session.project().segmentation.has_value());
        QVERIFY(!h.session.project().embroidery_objects.empty());
        const auto before = h.bytes();
        QVERIFY(!before.empty());
        QVERIFY(h.prepareSavePath());
        QVERIFY(h.click("prepareToggle"));
        QVERIFY(h.click("prepareTab_1"));
        QVERIFY(h.click("segPreviewButton"));
        QTRY_VERIFY_WITH_TIMEOUT(h.images.segPreviewActive(), 60000);
        QCOMPARE(h.bytes(), before);
        QVERIFY(h.click("segCancelButton"));
        QCOMPARE(h.bytes(), before);
        QVERIFY(h.saveRoundTrip());
        QVERIFY(h.warnings.isEmpty());
    }
};
namespace {
[[maybe_unused]] const bool registered = registerJourney<Journey01>("01");
}
#include "journey_01_image.moc"
