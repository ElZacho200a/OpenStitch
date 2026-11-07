// SPDX-License-Identifier: Apache-2.0
#include "journey_harness.hpp"
#include <QFile>

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;
using namespace openstitch::desktop_v2::studio::e2e;

class Journey12 : public QObject {
    Q_OBJECT
private slots:
    void run() {
        Harness h;
        QVERIFY(h.ready());
        QVERIFY(h.tentabrode());
        QVERIFY(h.prepareSavePath());
        const auto before = h.bytes();
        const auto revision = h.session.revision();
        QVERIFY(h.click("analysisPanelButton"));
        QVERIFY(h.click("proposeButton"));
        QTRY_VERIFY_WITH_TIMEOUT(h.analysis.proposalReady(), 60000);
        QCOMPARE(h.session.revision(), revision);
        QCOMPARE(h.bytes(), before);
        QVERIFY(!h.analysis.proposalSummary().isEmpty());
        const auto depth = h.steps();
        QVERIFY(h.click("applyProposalButton"));
        QVERIFY(h.settle());
        QCOMPARE(h.steps(), depth + 1);
        QVERIFY(h.saveRoundTrip());
        QVERIFY(h.undoToBytes(before));
    }
};
namespace {
[[maybe_unused]] const bool registered = registerJourney<Journey12>("12");
}
#include "journey_12_order.moc"
