// SPDX-License-Identifier: Apache-2.0
#include "journey_harness.hpp"
#include <QFile>

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;
using namespace openstitch::desktop_v2::studio::e2e;

class Journey03 : public QObject {
    Q_OBJECT
private slots:
    void run() {
        Harness h;
        QVERIFY(h.ready());
        const auto path = h.directory.filePath(QStringLiteral("real-vector.svg"));
        QFile svg(path);
        QVERIFY(svg.open(QIODevice::WriteOnly));
        svg.write(
            R"(<svg xmlns="http://www.w3.org/2000/svg" width="40mm" height="30mm" viewBox="0 0 40 30"><path fill="#bd2163" d="M2 2 L38 2 L38 28 L2 28Z"/><circle fill="#246caf" cx="20" cy="15" r="6"/></svg>)");
        svg.close();
        QVERIFY(h.openFile(path));
        QVERIFY(h.settle());
        QVERIFY(!h.session.project().vector_objects.empty());
        QVERIFY(!h.session.project().embroidery_objects.empty());
        QVERIFY(h.prepareSavePath());
        const auto before = h.bytes();
        h.key(Qt::Key_A, Qt::ControlModifier);
        QVERIFY(!h.session.selection().targets().empty());
        h.key(Qt::Key_Right);
        QVERIFY(h.settle());
        QVERIFY(h.undoToBytes(before));
        QVERIFY(h.saveRoundTrip());
    }
};
namespace {
[[maybe_unused]] const bool registered = registerJourney<Journey03>("03");
}
#include "journey_03_svg.moc"
