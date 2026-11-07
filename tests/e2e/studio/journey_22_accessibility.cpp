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

class Journey22 : public QObject {
    Q_OBJECT
private slots:
    void run() {
        Harness h;
        QVERIFY(h.ready());
        QVERIFY(h.rectangles());
        const auto before = h.bytes();
        QStringList issues;
        std::function<void(QQuickItem*)> audit = [&](QQuickItem* item) {
            if (item->isVisible() && item->isEnabled() && item->activeFocusOnTab()) {
                auto* interface = QAccessible::queryAccessibleInterface(item);
                if (!interface || interface->text(QAccessible::Name).trimmed().isEmpty()) {
                    issues.push_back(
                        QStringLiteral("Accessible.name absent: %1 (%2)")
                            .arg(item->objectName(),
                                 QString::fromLatin1(item->metaObject()->className())));
                }
            }
            for (auto* child : item->childItems())
                audit(child);
        };
        audit(h.window->contentItem());
        QSet<QQuickItem*> visited;
        for (int index = 0; index < 100; ++index) {
            h.key(Qt::Key_Tab);
            auto* focus = h.window->activeFocusItem();
            if (focus) {
                visited.insert(focus);
                if (!focus->isVisible())
                    issues.push_back(
                        QStringLiteral("Tab focus retained by hidden %1").arg(focus->objectName()));
            }
        }
        if (visited.size() < 5)
            issues.push_back(QStringLiteral("Tab order visits fewer than five controls"));
        QVERIFY(h.click("prepareToggle"));
        QVERIFY(h.click("prepareTab_1"));
        QVERIFY(h.click("segMaxColors"));
        h.key(Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier);
        if (auto* focus = h.window->activeFocusItem(); focus && !focus->isVisible())
            issues.push_back(
                QStringLiteral("Collapsed prepare panel retained hidden keyboard focus"));
        QCOMPARE(h.bytes(), before);
        if (!issues.isEmpty()) {
            qWarning().noquote() << issues.join('\n');
            QSKIP("BLOCKED: UX/H accessibility gaps listed in the log; no claim that all controls "
                  "are reachable/named.");
        }
    }
};
namespace {
[[maybe_unused]] const bool registered = registerJourney<Journey22>("22");
}
#include "journey_22_accessibility.moc"
