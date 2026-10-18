// SPDX-License-Identifier: Apache-2.0
// Barre Fils au clavier (HP-UX-041), pastilles et règles adaptées au DPI et à la police
// (HP-UX-043). Headless, sans pixel ni sleep.
#include <QAction>
#include <QApplication>
#include <QFont>
#include <QGraphicsScene>
#include <QImage>
#include <QMenu>
#include <QSignalSpy>
#include <QTest>

#include <cmath>
#include <vector>

#include "canvas_view.hpp"
#include "ruler.hpp"
#include "thread_strip.hpp"
#include "ui_swatch.hpp"

using openstitch::desktop::CanvasView;
using openstitch::desktop::makeSwatch;
using openstitch::desktop::Ruler;
using openstitch::desktop::ThreadStrip;

namespace {

std::vector<ThreadStrip::Swatch> makeThreads(int count) {
    std::vector<ThreadStrip::Swatch> threads;
    for (int i = 0; i < count; ++i) {
        threads.push_back(
            {QColor(10 + i * 7 % 240, 40 + i * 13 % 200, 90 + i * 29 % 160),
             QStringLiteral("Fil %1 (Madeira %2)\nglisser").arg(i + 1).arg(1000 + i)});
    }
    return threads;
}

} // namespace

class ThreadStripTest : public QObject {
    Q_OBJECT

private slots:
    void arrowsMoveTheFocusAndSpaceOrEnterSelect() {
        ThreadStrip strip;
        strip.resize(600, 40);
        strip.show();
        strip.setThreads(makeThreads(4), QStringLiteral("4 fils"));
        QVERIFY(strip.focusPolicy() != Qt::NoFocus);
        strip.setFocus();
        QSignalSpy clicked(&strip, &ThreadStrip::threadClicked);

        QCOMPARE(strip.focusedIndex(), 0);
        QTest::keyClick(&strip, Qt::Key_Right);
        QTest::keyClick(&strip, Qt::Key_Right);
        QCOMPARE(strip.focusedIndex(), 2);
        QTest::keyClick(&strip, Qt::Key_Space);
        QCOMPARE(clicked.count(), 1);
        QCOMPARE(clicked.at(0).at(0).toInt(), 2);

        QTest::keyClick(&strip, Qt::Key_Home);
        QCOMPARE(strip.focusedIndex(), 0);
        QTest::keyClick(&strip, Qt::Key_Left); // reste borné
        QCOMPARE(strip.focusedIndex(), 0);
        QTest::keyClick(&strip, Qt::Key_End);
        QCOMPARE(strip.focusedIndex(), 3);
        QTest::keyClick(&strip, Qt::Key_Return);
        QCOMPARE(clicked.count(), 2);
        QCOMPARE(clicked.at(1).at(0).toInt(), 3);
    }

    void ctrlArrowsReorderLikeADrag() {
        ThreadStrip strip;
        strip.resize(600, 40);
        strip.show();
        strip.setThreads(makeThreads(3), QStringLiteral("3 fils"));
        strip.setFocus();
        QSignalSpy moved(&strip, &ThreadStrip::threadMoved);
        QSignalSpy clicked(&strip, &ThreadStrip::threadClicked);
        QTest::keyClick(&strip, Qt::Key_Right, Qt::ControlModifier);
        QCOMPARE(moved.count(), 1);
        QCOMPARE(moved.at(0).at(0).toInt(), 0);
        QCOMPARE(moved.at(0).at(1).toInt(), 1);
        QCOMPARE(strip.focusedIndex(), 1); // le focus suit la pastille déplacée
        QCOMPARE(clicked.count(), 0);
        // Au bord : rien à réordonner.
        QTest::keyClick(&strip, Qt::Key_Right, Qt::ControlModifier);
        QTest::keyClick(&strip, Qt::Key_Right, Qt::ControlModifier);
        QCOMPARE(moved.count(), 2);
    }

    void everyThreadIsReachableBeyondTheAvailableWidth() {
        ThreadStrip strip;
        strip.resize(220, 40);
        strip.show();
        strip.setThreads(makeThreads(30), QStringLiteral("30 fils"));
        QVERIFY(strip.hiddenCount() > 0); // tout ne tient pas
        strip.setFocus();
        QSignalSpy clicked(&strip, &ThreadStrip::threadClicked);

        // La dernière case, « +N », est atteinte par le clavier et ouvre la liste COMPLÈTE.
        QTest::keyClick(&strip, Qt::Key_End);
        QCOMPARE(strip.focusedIndex(), 30 - strip.hiddenCount());
        QVERIFY(strip.overflowMenu() == nullptr);
        QTest::keyClick(&strip, Qt::Key_Return);
        QMenu* menu = strip.overflowMenu();
        QVERIFY(menu != nullptr);
        QCOMPARE(menu->actions().size(), 30);
        QVERIFY(!menu->actions().at(29)->icon().isNull());
        QVERIFY(menu->actions().at(29)->text().contains(QStringLiteral("Fil 30")));
        menu->actions().at(29)->trigger();
        QCOMPARE(clicked.count(), 1);
        QCOMPARE(clicked.at(0).at(0).toInt(), 29);
        menu->close();
    }

    void aWideStripHasNoOverflowChip() {
        ThreadStrip strip;
        strip.resize(800, 40);
        strip.show();
        strip.setThreads(makeThreads(5), QStringLiteral("5 fils"));
        QCOMPARE(strip.hiddenCount(), 0);
        strip.setFocus();
        QTest::keyClick(&strip, Qt::Key_End);
        QCOMPARE(strip.focusedIndex(), 4);
        QSignalSpy clicked(&strip, &ThreadStrip::threadClicked);
        QTest::keyClick(&strip, Qt::Key_Space);
        QCOMPARE(clicked.at(0).at(0).toInt(), 4);
        QVERIFY(strip.overflowMenu() == nullptr);
    }

    void focusAnnouncesNameAndCodeToAssistiveTechnology() {
        ThreadStrip strip;
        strip.resize(600, 40);
        strip.show();
        strip.setThreads(makeThreads(3), QStringLiteral("3 fils"));
        strip.setFocus();
        QTest::keyClick(&strip, Qt::Key_Right);
        const QString description = strip.accessibleDescription();
        QVERIFY2(description.contains(QStringLiteral("Fil 2 sur 3")), qPrintable(description));
        QVERIFY2(description.contains(QStringLiteral("Madeira 1001")), qPrintable(description));
        QVERIFY(!strip.accessibleName().isEmpty());
    }

    void swatchesCarryTheScreenPixelRatio() {
        const QPixmap automatic = makeSwatch(Qt::red, 16);
        QCOMPARE(automatic.devicePixelRatio(), qApp->devicePixelRatio());
        // Taille physique = taille logique x rapport : nette à 150 % comme à 200 %.
        for (const qreal dpr : {1.0, 1.5, 2.0}) {
            const QPixmap swatch = makeSwatch(Qt::blue, 16, dpr);
            QCOMPARE(swatch.devicePixelRatio(), dpr);
            QCOMPARE(swatch.width(), static_cast<int>(std::ceil(16 * dpr)));
            QCOMPARE(swatch.deviceIndependentSize().width(), 16.0);
        }
        // Le centre est de la couleur demandée.
        const QImage image = makeSwatch(QColor(200, 16, 46), 20, 2.0).toImage();
        QCOMPARE(image.pixelColor(image.width() / 2, image.height() / 2), QColor(200, 16, 46));
    }

    void rulerMetricsFollowTheFont() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        Ruler horizontal(Qt::Horizontal, &view);
        Ruler vertical(Qt::Vertical, &view);
        const int base = horizontal.thickness();
        QCOMPARE(horizontal.sizeHint().height(), base);
        QCOMPARE(vertical.sizeHint().width(), base);
        QVERIFY(base >= 20); // proche de l'ancienne règle fixe de 26 px

        QFont bigger = horizontal.font();
        bigger.setPointSizeF(bigger.pointSizeF() * 2.0);
        horizontal.setFont(bigger);
        QVERIFY(horizontal.rulerFont().pointSizeF() > QFont().pointSizeF());
        QVERIFY(horizontal.thickness() > base);
        QCOMPARE(horizontal.sizeHint().height(), horizontal.thickness());
    }
};

QTEST_MAIN(ThreadStripTest)
#include "test_thread_strip.moc"
