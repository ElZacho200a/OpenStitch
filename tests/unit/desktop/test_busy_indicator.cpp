// SPDX-License-Identifier: Apache-2.0
// HP-PERF-001 / HP-UX-006 : BusyIndicator rend la main a la boucle d'evenements pendant un calcul,
// transmet progression et annulation cooperative. Ni pixels ni sleep : les evenements sont
// declenches par des QTimer traites pendant l'attente de BusyIndicator::run.
#include <QTest>
#include <QTimer>
#include <QWidget>

#include <atomic>
#include <thread>

#include "busy_indicator.hpp"

using namespace openstitch;
using openstitch::desktop::BusyIndicator;

namespace {
// Attente bornee (en tours de scrutation) pour qu'un test defaillant ne bloque jamais la suite.
bool spin_until(const std::atomic<bool>& flag) {
    for (long i = 0; i < 400'000'000L && !flag.load(); ++i) {
        std::this_thread::yield();
    }
    return flag.load();
}
} // namespace

class TestBusyIndicator : public QObject {
    Q_OBJECT

private slots:
    void eventIsProcessedWhileTheTaskRuns() {
        QWidget parent;
        std::atomic<bool> eventSeen{false};
        BusyIndicator busy(&parent, QStringLiteral("test"));
        QTimer::singleShot(0, &parent, [&] { eventSeen = true; });
        // Le calcul ne se termine que lorsque l'evenement UI a ete traite.
        const bool done = busy.run([&] { return spin_until(eventSeen); });
        QVERIFY(done);
        QVERIFY(eventSeen.load());
    }

    void cancelRequestReachesTheTaskAndKeepsResultDiscardable() {
        QWidget parent;
        BusyIndicator busy(&parent, QStringLiteral("test"), {}, true);
        QTimer::singleShot(0, &parent, [&] { busy.requestCancel(); });
        const auto result = busy.run([](const TaskContext& task) {
            for (long i = 0; i < 400'000'000L && !task.cancelled(); ++i) {
                std::this_thread::yield();
            }
            return task.cancelled();
        });
        QVERIFY(result);
        QVERIFY(busy.cancelled());
    }

    void progressIsShownOnTheDialog() {
        QWidget parent;
        BusyIndicator busy(&parent, QStringLiteral("test"), {}, true);
        std::atomic<bool> shown{false};
        // Tant que la barre ne montre pas 3/4, le calcul attend ; le QTimer le verifie sur le
        // fil UI et libere le calcul.
        auto* poll = new QTimer(&parent);
        QObject::connect(poll, &QTimer::timeout, &parent, [&] {
            if (busy.dialog().maximum() == 4 && busy.dialog().value() == 3) {
                shown = true;
            }
        });
        poll->start(1);
        const bool ok = busy.run([&](const TaskContext& task) {
            task.report(3, 4);
            return spin_until(shown);
        });
        QVERIFY(ok);
        QCOMPARE(busy.dialog().maximum(), 4);
        QCOMPARE(busy.dialog().value(), 3);
    }

    void nonCancellableIndicatorKeepsPlainCallables() {
        QWidget parent;
        BusyIndicator busy(&parent, QStringLiteral("test"));
        QCOMPARE(busy.run([] { return 42; }), 42);
        QVERIFY(!busy.cancelled());
    }
};

QTEST_MAIN(TestBusyIndicator)
#include "test_busy_indicator.moc"
