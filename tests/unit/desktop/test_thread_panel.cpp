// SPDX-License-Identifier: Apache-2.0
// Panneau « Fils » (HP-THR-004/005) : présentation (ThreadPanel seul) puis intégration dans
// MainWindow (assignation à la sélection en UN pas d'annulation, film couleur).
#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include "canvas_view.hpp"
#include "main_window.hpp"
#include "thread_panel.hpp"
#include "thread_strip.hpp"

using namespace openstitch;
using openstitch::desktop::CanvasView;
using openstitch::desktop::MainWindow;
using openstitch::desktop::ThreadPanel;
using openstitch::desktop::ThreadStrip;

Q_DECLARE_METATYPE(openstitch::thread_palette::Thread)

namespace {

document::Project twoTrianglesProject() {
    document::Project project;
    project.original.width = 2;
    project.original.height = 2;
    project.original.rgba.assign(2 * 2 * 4, 255);
    const std::array<std::uint8_t, 3> colors[2] = {{200, 16, 46}, {0, 114, 206}};
    for (int i = 0; i < 2; ++i) {
        document::VectorObject vec;
        vec.id = project.object_ids.next();
        vec.name = "Triangle";
        geometry::Path tri;
        tri.closed = true;
        const std::int32_t ox = i * 5'000;
        const auto node = [](std::int32_t x, std::int32_t y) {
            return geometry::PathNode{Vec2um{Micrometers{x}, Micrometers{y}},
                                      geometry::NodeType::Corner, std::nullopt, std::nullopt};
        };
        tri.nodes = {node(ox, 0), node(ox + 1'000, 0), node(ox, 1'000)};
        vec.paths.push_back(geometry::PathSet{tri, {}});
        project.vector_objects.push_back(vec);

        document::EmbroideryObject emb;
        emb.id = project.object_ids.next();
        emb.name = "Triangle " + std::to_string(i + 1);
        emb.source_vector = vec.id;
        emb.rgb = colors[i];
        emb.params = document::RunningStitchParams{};
        project.embroidery_objects.push_back(emb);
    }
    return project;
}

} // namespace

namespace openstitch::desktop {

class MainWindowTest : public QObject {
    Q_OBJECT

    QTemporaryDir settingsDir_;

private slots:
    void initTestCase() {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
        QStandardPaths::setTestModeEnabled(true);
        qRegisterMetaType<thread_palette::Thread>();
    }

    void panelListsCatalogAndFiltersBySearch() {
        const auto library = thread_palette::ThreadLibrary::with_builtin();
        ThreadPanel panel;
        panel.setLibrary(&library);
        auto* list = panel.findChild<QListWidget*>("threadCatalogList");
        auto* search = panel.findChild<QLineEdit*>("threadSearchEdit");
        QVERIFY(list != nullptr && search != nullptr);
        QVERIFY(list->count() >= 28); // nuancier générique sélectionné par défaut
        search->setText("rouge");
        QVERIFY(list->count() >= 2);
        QVERIFY(list->count() < 28);
        search->setText("zzzz-inconnu");
        QCOMPARE(list->count(), 0);
    }

    void clickingACatalogRowAssignsThatThread() {
        const auto library = thread_palette::ThreadLibrary::with_builtin();
        ThreadPanel panel;
        panel.setLibrary(&library);
        auto* list = panel.findChild<QListWidget*>("threadCatalogList");
        QSignalSpy spy(&panel, &ThreadPanel::assignThreadRequested);
        list->setCurrentRow(1);
        emit list->itemClicked(list->item(1));
        QCOMPARE(spy.count(), 1);
        const auto thread = spy.takeFirst().at(0).value<thread_palette::Thread>();
        QCOMPARE(QString::fromStdString(thread.key.chart_id), QStringLiteral("generic"));
        QCOMPARE(QString::fromStdString(thread.key.code), QStringLiteral("G02"));
    }

    void filmButtonsEmitMovesAndMergeIsOnlyOfferedWithEnoughBlocks() {
        ThreadPanel panel;
        std::vector<stitch_analysis::ColorFilmBlock> film(3);
        film[0].identity.rgb = {1, 1, 1};
        film[1].identity.rgb = {2, 2, 2};
        film[2].identity.rgb = {1, 1, 1};
        for (auto& b : film) {
            b.objects = {ObjectId{1}};
            b.stitch_count = 10;
        }
        panel.setFilm(film);
        auto* list = panel.findChild<QListWidget*>("threadFilmList");
        auto* up = panel.findChild<QPushButton*>("threadFilmUpBtn");
        auto* down = panel.findChild<QPushButton*>("threadFilmDownBtn");
        auto* merge = panel.findChild<QPushButton*>("threadFilmMergeBtn");
        QCOMPARE(list->count(), 3);
        QVERIFY(merge->isEnabled());
        QSignalSpy moved(&panel, &ThreadPanel::filmBlockMoved);
        list->setCurrentRow(1);
        QVERIFY(up->isEnabled() && down->isEnabled());
        up->click();
        QCOMPARE(moved.count(), 1);
        QCOMPARE(moved.at(0).at(0).toInt(), 1);
        QCOMPARE(moved.at(0).at(1).toInt(), 0);
        list->setCurrentRow(0);
        QVERIFY(!up->isEnabled());

        panel.setFilm({film[0]});
        QVERIFY(!merge->isEnabled());
    }

    void colourButtonsEmitThreadMovesAndAreDisabledAtTheEnds() {
        ThreadPanel panel;
        std::vector<stitch_analysis::ThreadUsage> usage(3);
        usage[0].identity.rgb = {1, 1, 1};
        usage[1].identity.rgb = {2, 2, 2};
        usage[2].identity.rgb = {3, 3, 3};
        panel.setUsage(usage);
        auto* list = panel.findChild<QListWidget*>("threadUsageList");
        auto* up = panel.findChild<QPushButton*>("threadMoveUpBtn");
        auto* down = panel.findChild<QPushButton*>("threadMoveDownBtn");
        QVERIFY(list != nullptr && up != nullptr && down != nullptr);
        QCOMPARE(list->count(), 3);
        QSignalSpy moved(&panel, &ThreadPanel::threadMoved);
        list->setCurrentRow(1);
        QVERIFY(up->isEnabled() && down->isEnabled());
        down->click();
        QCOMPARE(moved.count(), 1);
        QCOMPARE(moved.at(0).at(0).toInt(), 1);
        QCOMPARE(moved.at(0).at(1).toInt(), 2);
        up->click();
        QCOMPARE(moved.at(1).at(0).toInt(), 1);
        QCOMPARE(moved.at(1).at(1).toInt(), 0);
        list->setCurrentRow(0);
        QVERIFY(!up->isEnabled());
        list->setCurrentRow(2);
        QVERIFY(!down->isEnabled());
    }

    void movingAColourReordersTheObjectsAndIsOneUndoStep() {
        MainWindow window;
        window.show();
        window.applyLoadedProject(twoTrianglesProject());
        auto* dock = window.findChild<QDockWidget*>("threadDock");
        auto* panel = window.findChild<ThreadPanel*>();
        QVERIFY(dock != nullptr && panel != nullptr);
        dock->show();
        QApplication::processEvents();

        auto* usage = panel->findChild<QListWidget*>("threadUsageList");
        auto* down = panel->findChild<QPushButton*>("threadMoveDownBtn");
        QCOMPARE(usage->count(), 2);
        QVERIFY(usage->item(0)->text().contains("#C8102E")); // rouge cousu d'abord
        usage->setCurrentRow(0);
        QVERIFY(down->isEnabled());
        down->click(); // le rouge passe après (donc au-dessus du) bleu
        QApplication::processEvents();
        QVERIFY2(usage->item(0)->text().contains("#0072CE"), qPrintable(usage->item(0)->text()));
        QVERIFY(usage->item(1)->text().contains("#C8102E"));

        window.findChild<QAction*>("action_undo")->trigger(); // un seul pas d'annulation
        QApplication::processEvents();
        QVERIFY(usage->item(0)->text().contains("#C8102E"));
        QVERIFY(usage->item(1)->text().contains("#0072CE"));
    }

    void assigningAThreadToTheSelectionIsOneUndoStep() {
        MainWindow window;
        window.show();
        window.applyLoadedProject(twoTrianglesProject());
        auto* dock = window.findChild<QDockWidget*>("threadDock");
        auto* panel = window.findChild<ThreadPanel*>();
        auto* view = window.findChild<CanvasView*>();
        QVERIFY(dock != nullptr && panel != nullptr && view != nullptr);
        dock->show();
        QApplication::processEvents();

        auto* usage = panel->findChild<QListWidget*>("threadUsageList");
        auto* film = panel->findChild<QListWidget*>("threadFilmList");
        QCOMPARE(usage->count(), 2); // deux couleurs libres
        QCOMPARE(film->count(), 2);

        // Sélectionne le premier triangle (clic sur le motif), puis choisit le fil G01.
        view->canvasClickedMm(QPointF(0.25, -0.25));
        auto* catalog = panel->findChild<QListWidget*>("threadCatalogList");
        catalog->setCurrentRow(0); // G01 Blanc
        emit catalog->itemClicked(catalog->item(0));
        QCOMPARE(usage->count(), 2);
        QVERIFY2(usage->item(0)->text().contains("G01"), qPrintable(usage->item(0)->text()));
        QVERIFY(usage->item(1)->text().contains("#0072CE"));

        // Un seul Ctrl+Z rétablit la couleur libre d'origine.
        auto* undo = window.findChild<QAction*>("action_undo");
        QVERIFY(undo != nullptr);
        undo->trigger();
        QApplication::processEvents();
        QVERIFY(usage->item(0)->text().contains("#C8102E"));
        QVERIFY(!usage->item(0)->text().contains("G01"));
    }

    void threadStripShowsOneSwatchPerThreadAndReportsClicks() {
        ThreadStrip strip;
        strip.resize(600, 40);
        strip.show();
        strip.setThreads({{QColor(200, 16, 46), QStringLiteral("rouge")},
                          {QColor(0, 114, 206), QStringLiteral("bleu")},
                          {QColor(10, 10, 10), QStringLiteral("noir")}},
                         QStringLiteral("3 fils"));
        QCOMPARE(strip.threadCount(), 3);

        // Balaye la rangée : chaque pastille doit répondre par son propre indice, et aucun
        // clic ne doit signaler un indice hors plage.
        QSignalSpy spy(&strip, &ThreadStrip::threadClicked);
        for (int x = 0; x < strip.width(); x += 3) {
            QTest::mouseClick(&strip, Qt::LeftButton, Qt::NoModifier, QPoint(x, 20));
        }
        QSet<int> seen;
        for (const auto& args : spy) {
            seen.insert(args.at(0).toInt());
        }
        QCOMPARE(seen, (QSet<int>{0, 1, 2}));

        strip.setThreads({}, QString());
        QCOMPARE(strip.threadCount(), 0);
    }

    void draggingASwatchReordersAndAPlainClickStillSelects() {
        ThreadStrip strip;
        strip.resize(600, 40);
        strip.show();
        strip.setThreads({{QColor(200, 16, 46), QStringLiteral("rouge")},
                          {QColor(0, 114, 206), QStringLiteral("bleu")},
                          {QColor(10, 10, 10), QStringLiteral("noir")}},
                         QStringLiteral("3 fils"));
        QSignalSpy clicked(&strip, &ThreadStrip::threadClicked);
        QSignalSpy moved(&strip, &ThreadStrip::threadMoved);
        const auto dragTo = [&](const QPointF& pos) {
            QMouseEvent move(QEvent::MouseMove, pos, strip.mapToGlobal(pos), Qt::NoButton,
                             Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&strip, &move);
        };

        // Clic simple : sélection, aucun déplacement.
        QTest::mouseClick(&strip, Qt::LeftButton, Qt::NoModifier, strip.swatchCenter(1).toPoint());
        QCOMPARE(clicked.count(), 1);
        QCOMPARE(clicked.at(0).at(0).toInt(), 1);
        QCOMPARE(moved.count(), 0);

        // Glisser la 1re pastille jusqu'à la 3e : la couleur passe en position 2.
        QTest::mousePress(&strip, Qt::LeftButton, Qt::NoModifier, strip.swatchCenter(0).toPoint());
        dragTo(strip.swatchCenter(1));
        dragTo(strip.swatchCenter(2));
        QTest::mouseRelease(&strip, Qt::LeftButton, Qt::NoModifier,
                            strip.swatchCenter(2).toPoint());
        QCOMPARE(moved.count(), 1);
        QCOMPARE(moved.at(0).at(0).toInt(), 0);
        QCOMPARE(moved.at(0).at(1).toInt(), 2);
        QCOMPARE(clicked.count(), 1); // un glissement n'est pas un clic

        // Relâchée sur sa propre place : rien ne bouge, et ce n'est pas un clic non plus.
        QTest::mousePress(&strip, Qt::LeftButton, Qt::NoModifier, strip.swatchCenter(1).toPoint());
        dragTo(strip.swatchCenter(1) + QPointF(0, 12));
        dragTo(strip.swatchCenter(1) + QPointF(2, 12));
        QTest::mouseRelease(&strip, Qt::LeftButton, Qt::NoModifier,
                            (strip.swatchCenter(1) + QPointF(2, 12)).toPoint());
        QCOMPARE(moved.count(), 1);
        QCOMPARE(clicked.count(), 1);

        // Glisser vers la gauche, au-delà de la première pastille : borné à la position 0.
        QTest::mousePress(&strip, Qt::LeftButton, Qt::NoModifier, strip.swatchCenter(2).toPoint());
        dragTo(strip.swatchCenter(1));
        dragTo(QPointF(1, strip.swatchCenter(0).y()));
        QTest::mouseRelease(&strip, Qt::LeftButton, Qt::NoModifier, QPoint(1, 20));
        QCOMPARE(moved.count(), 2);
        QCOMPARE(moved.at(1).at(0).toInt(), 2);
        QCOMPARE(moved.at(1).at(1).toInt(), 0);
    }

    void draggingASwatchMovesTheColourAndIsOneUndoStep() {
        MainWindow window;
        window.show();
        window.applyLoadedProject(twoTrianglesProject());
        QApplication::processEvents();
        auto* strip = window.findChild<ThreadStrip*>();
        QVERIFY(strip != nullptr);
        QCOMPARE(strip->threadCount(), 2); // rouge puis bleu

        const auto dragTo = [&](const QPointF& pos) {
            QMouseEvent move(QEvent::MouseMove, pos, strip->mapToGlobal(pos), Qt::NoButton,
                             Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(strip, &move);
        };
        QTest::mousePress(strip, Qt::LeftButton, Qt::NoModifier, strip->swatchCenter(0).toPoint());
        dragTo(strip->swatchCenter(1));
        QTest::mouseRelease(strip, Qt::LeftButton, Qt::NoModifier,
                            strip->swatchCenter(1).toPoint());
        QApplication::processEvents();
        // Le rouge est maintenant cousu après le bleu : objets dans l'ordre bleu, rouge.
        QCOMPARE(window.project_.embroidery_objects.front().rgb,
                 (std::array<std::uint8_t, 3>{0, 114, 206}));

        window.findChild<QAction*>("action_undo")->trigger(); // un seul pas d'annulation
        QApplication::processEvents();
        QCOMPARE(window.project_.embroidery_objects.front().rgb,
                 (std::array<std::uint8_t, 3>{200, 16, 46}));
    }

    void threadStripFollowsTheThreadsOfTheLoadedPattern() {
        MainWindow window;
        window.show();
        window.applyLoadedProject(twoTrianglesProject());
        QApplication::processEvents();
        // La barre des fils vit même quand le panneau Fils est fermé : deux couleurs libres.
        auto* strip = window.findChild<ThreadStrip*>();
        QVERIFY(strip != nullptr);
        QCOMPARE(strip->threadCount(), 2);
    }

    void reducingToOneThreadMergesColoursInOneStep() {
        MainWindow window;
        window.show();
        window.applyLoadedProject(twoTrianglesProject());
        auto* dock = window.findChild<QDockWidget*>("threadDock");
        auto* panel = window.findChild<ThreadPanel*>();
        dock->show();
        QApplication::processEvents();
        auto* usage = panel->findChild<QListWidget*>("threadUsageList");
        QCOMPARE(usage->count(), 2);
        emit panel->reduceColorsRequested(1);
        QCOMPARE(usage->count(), 1);
        window.findChild<QAction*>("action_undo")->trigger();
        QApplication::processEvents();
        QCOMPARE(usage->count(), 2);
    }
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_thread_panel.moc"
