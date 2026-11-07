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

class Journey21 : public QObject {
    Q_OBJECT
private slots:
    void run() {
        Harness h;
        QVERIFY(h.ready());
        QVERIFY(h.rectangles());
        QVERIFY(h.settle());
        const auto before = h.bytes();
        h.window->update();
        auto image = h.window->grabWindow();
        QVERIFY(!image.isNull());
        QCoreApplication::processEvents();
        const bool warp = qEnvironmentVariableIsSet("STUDIO_E2E_WARP");
        if (warp) {
            if (!h.canvas->backendName().contains(QStringLiteral("Direct3D 11"))) {
                QSKIP("BLOCKED: WARP D3D11 unavailable in this session; CPU fallback is tested "
                      "separately.");
            }
        } else {
            QVERIFY(h.canvas->backendName().contains(QStringLiteral("software")));
        }
        const auto count = h.canvas->geometryBuildCount();
        const auto snapshot = h.studio.scene();
        h.key(Qt::Key_F);
        h.canvas->zoomAt(1.25, h.canvas->width() / 2, h.canvas->height() / 2);
        QVERIFY(!h.window->grabWindow().isNull());
        QCoreApplication::processEvents();
        QCOMPARE(h.studio.scene(), snapshot);
        QCOMPARE(h.canvas->geometryBuildCount(), count);
        QCOMPARE(h.bytes(), before);
    }
};
namespace {
[[maybe_unused]] const bool registered = registerJourney<Journey21>("21");
}
#include "journey_21_fallback.moc"
