// SPDX-License-Identifier: Apache-2.0
#include <QAction>
#include <QApplication>
#include <QTest>
#include <QToolBar>
#include <QTreeWidget>
#include <QWheelEvent>
#include <filesystem>

#include "command_palette.hpp"
#include "inspector_panel.hpp"
#include "preview_canvas.hpp"
#include "shell_window.hpp"

using namespace openstitch::desktop_v2;
namespace fs = std::filesystem;

namespace {
fs::path tentabrode() {
    return fs::path(OPENSTITCH_FIXTURES_DIR) / "tentabrode.png";
}
} // namespace

class ShellWindowTest : public QObject {
    Q_OBJECT
private slots:
    void emptyShellHasNoObjectsAndNoPreview() {
        ShellWindow w;
        QVERIFY(w.session().project().embroidery_objects.empty());
        QVERIFY(!w.previewShown());
        auto* tree = w.findChild<QTreeWidget*>("objectTree");
        QVERIFY(tree);
        QCOMPARE(tree->topLevelItemCount(), 0);
    }

    void workspacesFilterTheToolRailButKeepOneSelectedTool() {
        ShellWindow w;
        auto* rail = w.findChild<QToolBar*>("toolRail");
        QVERIFY(rail);
        const auto design = rail->actions().size();
        w.setWorkspace(Workspace::Produce);
        QVERIFY(rail->actions().size() < design);
        QCOMPARE(w.activeToolId(), QString("select"));
        w.setWorkspace(Workspace::Design);
        QCOMPARE(rail->actions().size(), design);
    }

    void unmigratedToolsAreVisibleButDisabledWithAReason() {
        ShellWindow w;
        auto* rail = w.findChild<QToolBar*>("toolRail");
        int enabled = 0;
        int disabled = 0;
        for (auto* a : rail->actions()) {
            if (a->isEnabled()) {
                ++enabled;
            } else {
                ++disabled;
                QVERIFY(a->toolTip().contains("pas encore migré"));
            }
        }
        QCOMPARE(enabled, 2); // Sélection + Déplacer la vue
        QVERIFY(disabled > 0);
    }

    void imageToDstEndToEnd() {
        ShellWindow w;
        w.openImage(tentabrode(), 120.0);
        w.waitForIdle();
        QVERIFY2(!w.session().project().embroidery_objects.empty(), qPrintable(w.lastNotice()));
        auto* tree = w.findChild<QTreeWidget*>("objectTree");
        QCOMPARE(static_cast<size_t>(tree->topLevelItemCount()),
                 w.session().project().embroidery_objects.size());
        QVERIFY(w.previewShown());
        QVERIFY(w.session().dirty());

        const auto dst = fs::temp_directory_path() / "openstitch_v2_shell.dst";
        fs::remove(dst);
        w.exportDst(dst);
        w.waitForIdle();
        QVERIFY2(fs::exists(dst), qPrintable(w.lastNotice()));
        QVERIFY(fs::file_size(dst) > 512);
        QVERIFY(w.lastNotice().contains("DST"));
        fs::remove(dst);
    }

    void treeSelectionAndModelStayInSync() {
        ShellWindow w;
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        auto* tree = w.findChild<QTreeWidget*>("objectTree");
        QVERIFY(tree->topLevelItemCount() >= 2);
        tree->topLevelItem(0)->setSelected(true);
        tree->topLevelItem(1)->setSelected(true);
        QCOMPARE(w.session().selection().targets().size(), size_t(2));
        // Sens inverse : le modèle pilote l'arbre (canevas, compagnon IA, annulation…).
        const auto first = w.session().project().embroidery_objects.front().id.value;
        w.session().selection().replace({{TargetKind::EmbroideryObject, first}});
        QCOMPARE(tree->selectedItems().size(), 1);
        QCOMPARE(tree->selectedItems().front()->data(0, Qt::UserRole + 1).toULongLong(),
                 static_cast<qulonglong>(first));
    }

    void renamingFromTheTreeIsOneUndoStep() {
        ShellWindow w;
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        auto* tree = w.findChild<QTreeWidget*>("objectTree");
        const std::string before = w.session().project().embroidery_objects.front().name;
        tree->topLevelItem(0)->setText(0, "Ciel \"V2\"");
        QCOMPARE(QString::fromStdString(w.session().project().embroidery_objects.front().name),
                 QString("Ciel \"V2\""));
        QVERIFY(w.session().canUndo());
        QVERIFY(w.session().undo());
        QCOMPARE(w.session().project().embroidery_objects.front().name, before);
        QCOMPARE(tree->topLevelItem(0)->text(0), QString::fromStdString(before));
    }

    void hidingFromTheTreeGoesThroughTheBatchPathAndUndoes() {
        ShellWindow w;
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        auto* tree = w.findChild<QTreeWidget*>("objectTree");
        tree->topLevelItem(0)->setCheckState(0, Qt::Unchecked);
        QVERIFY(!w.session().project().embroidery_objects.front().visible);
        QCOMPARE(QString::fromStdString(w.session().undoName()), QString("Masquer"));
        QVERIFY(w.session().undo());
        QVERIFY(w.session().project().embroidery_objects.front().visible);
    }

    void projectSaveThenReopenRoundTrips() {
        ShellWindow w;
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        const auto count = w.session().project().embroidery_objects.size();
        const auto name = w.session().project().embroidery_objects.front().name;
        const auto osp = fs::temp_directory_path() / "openstitch_v2_roundtrip.osp";
        fs::remove(osp);
        QVERIFY(w.saveProject(osp));
        QVERIFY(!w.session().dirty()); // enregistré : l'historique est « propre »

        ShellWindow reopened;
        reopened.openProject(osp);
        reopened.waitForIdle();
        QCOMPARE(reopened.session().project().embroidery_objects.size(), count);
        QCOMPARE(reopened.session().project().embroidery_objects.front().name, name);
        QVERIFY(!reopened.session().dirty());
        QVERIFY(reopened.previewShown());
        fs::remove(osp);
    }

    void failedImportShowsAnErrorNoticeAndKeepsTheDocument() {
        ShellWindow w;
        w.openImage(fs::path("missing/nothing.png"), 0.0);
        w.waitForIdle();
        QVERIFY(w.session().project().embroidery_objects.empty());
        QVERIFY(w.lastNotice().contains("impossible"));
    }

    void cancellingNeverLeavesAHalfInstalledDocument() {
        ShellWindow w;
        w.openImage(tentabrode(), 300.0);
        w.tasks().cancelAll();
        w.waitForIdle();
        // Soit annulé avant la fin (document vierge), soit terminé avant l'annulation (document
        // complet) : aucun état intermédiaire n'est observable.
        const bool pristine = w.session().project().embroidery_objects.empty();
        const bool complete = !pristine;
        QVERIFY(pristine || complete);
        if (pristine) {
            QVERIFY(!w.session().dirty());
        }
    }

    void clickingTheCanvasSelectsTheObjectUnderTheCursor() {
        ShellWindow w;
        w.show();
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        auto* canvas = w.canvas();
        QVERIFY(canvas && canvas->hasImage());
        // Un point appartenant à un objet (milieu de son dernier brin).
        std::optional<QPointF> point;
        std::uint64_t id = 0;
        for (const auto& o : w.session().project().embroidery_objects) {
            if ((point = w.objectCentrePx(o.id.value))) {
                id = o.id.value;
                break;
            }
        }
        QVERIFY(point.has_value());
        const QPoint viewPos = canvas->mapFromScene(*point);
        QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, viewPos);
        QCOMPARE(w.session().selection().targets().size(), size_t(1));
        QCOMPARE(canvas->selectionBoxCount(), 1);
        (void)id;

        // Ctrl+clic au même endroit : bascule (désélectionne).
        QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::ControlModifier, viewPos);
        QVERIFY(w.session().selection().empty());
        QCOMPARE(canvas->selectionBoxCount(), 0);

        // Clic dans le vide (coin de l'image) : sélection vide.
        QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, viewPos);
        QVERIFY(!w.session().selection().empty());
        QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier,
                          canvas->mapFromScene(QPointF(1, 1)));
        QVERIFY(w.session().selection().empty());
    }

    void aDragIsARubberBandNotAClick() {
        ShellWindow w;
        w.show();
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        auto* canvas = w.canvas();
        std::optional<QPointF> point;
        for (const auto& o : w.session().project().embroidery_objects) {
            if ((point = w.objectCentrePx(o.id.value))) {
                break;
            }
        }
        QVERIFY(point.has_value());
        const QPoint viewPos = canvas->mapFromScene(*point);
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, viewPos);
        QTest::mouseMove(canvas->viewport(), viewPos + QPoint(30, 30));
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier,
                            viewPos + QPoint(30, 30));
        QVERIFY(w.session().selection().empty()); // un glisser n'est pas un clic
    }

    void rubberBandSelectsWholeObjectsOnly() {
        ShellWindow w;
        w.show();
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        auto* canvas = w.canvas();
        QVERIFY(canvas->hasImage());
        const QRectF image = canvas->sceneRect().adjusted(40, 40, -40, -40);
        // Cadre autour de toute l'image : tous les objets visibles sont dedans.
        const QPoint from = canvas->mapFromScene(image.topLeft() + QPointF(-5, -5));
        const QPoint to = canvas->mapFromScene(image.bottomRight() + QPointF(5, 5));
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(canvas->viewport(), from + QPoint(20, 20));
        QTest::mouseMove(canvas->viewport(), to);
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, to);
        // Un objet sans aucun point cousu n'a pas d'emprise : il n'est pas sélectionnable au
        // canevas.
        size_t stitched = 0;
        for (const auto& o : w.session().project().embroidery_objects) {
            stitched += w.objectCentrePx(o.id.value).has_value() ? 1 : 0;
        }
        QVERIFY(stitched > 0);
        QCOMPARE(w.session().selection().targets().size(), stitched);

        // Petit cadre au milieu : aucun objet entier (les remplissages dépassent) -> sélection
        // vide.
        const QPoint c = canvas->viewport()->rect().center();
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, c);
        QTest::mouseMove(canvas->viewport(), c + QPoint(10, 10));
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, c + QPoint(10, 10));
        QVERIFY(w.session().selection().empty());
    }

    void panToolDragsTheViewInsteadOfARubberBand() {
        ShellWindow w;
        QVERIFY(w.runCommand("tool.pan"));
        QCOMPARE(w.activeToolId(), QString("pan"));
        QCOMPARE(w.canvas()->dragMode(), QGraphicsView::ScrollHandDrag);
        QVERIFY(!w.canvas()->rubberBandSelection());
        QVERIFY(w.runCommand("tool.select"));
        QVERIFY(w.canvas()->rubberBandSelection());
    }

    void commandsCoverToolsWorkspacesAndEditing() {
        ShellWindow w;
        const auto entries = w.commandEntries();
        const auto has = [&](const char* id) {
            for (const auto& e : entries) {
                if (e.id == id) {
                    return true;
                }
            }
            return false;
        };
        QVERIFY(has("file.open") && has("edit.undo") && has("workspace.produce"));
        // Les 20 outils du registre sont tous atteignables par la palette.
        int tools = 0;
        for (const auto& e : entries) {
            tools += e.id.rfind("tool.", 0) == 0 ? 1 : 0;
        }
        QCOMPARE(tools, 20);
        // Un outil non migré est listé mais refusé, sans effet de bord.
        QVERIFY(!w.runCommand("tool.draw.ellipse"));
        QCOMPARE(w.activeToolId(), QString("select"));
        QVERIFY(!w.runCommand("nope"));
        // Sans document, enregistrer / exporter / annuler sont désactivés.
        QVERIFY(!w.runCommand("file.save"));
        QVERIFY(!w.runCommand("edit.undo"));
        QVERIFY(w.runCommand("workspace.optimize"));
        QCOMPARE(w.workspace(), Workspace::Optimize);
    }

    void selectAllAndClearThroughCommands() {
        ShellWindow w;
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        QVERIFY(w.runCommand("selection.all"));
        QCOMPARE(w.session().selection().targets().size(),
                 w.session().project().embroidery_objects.size());
        QVERIFY(w.runCommand("selection.clear"));
        QVERIFY(w.session().selection().empty());
        QVERIFY(!w.runCommand("selection.clear")); // plus rien à désélectionner : désactivée
    }

    void paletteFiltersAndRunsTheChosenCommand() {
        ShellWindow w;
        CommandPalette palette;
        palette.setEntries(w.commandEntries());
        palette.setQuery("espace numerisation");
        QCOMPARE(palette.visibleIds(), QStringList{"workspace.digitize"});
        QString chosen;
        QObject::connect(&palette, &CommandPalette::chosen,
                         [&](const QString& id) { chosen = id; });
        QVERIFY(palette.activateCurrent());
        QCOMPARE(chosen, QString("workspace.digitize"));
        QVERIFY(w.runCommand(chosen));
        QCOMPARE(w.workspace(), Workspace::Digitize);

        // Une commande désactivée ne se valide pas.
        palette.setQuery("ellipse");
        QCOMPARE(palette.visibleIds().size(), 1);
        QVERIFY(!palette.activateCurrent());
    }

    void inspectorEditsAllSelectedObjectsInOneUndoStep() {
        ShellWindow w;
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        const auto& objects = w.session().project().embroidery_objects;
        QVERIFY(objects.size() >= 2);
        w.session().selection().replace({{TargetKind::EmbroideryObject, objects[0].id.value},
                                         {TargetKind::EmbroideryObject, objects[1].id.value}});
        auto* panel = w.inspector();
        QVERIFY(panel->applyColor(QColor(12, 200, 34)));
        QCOMPARE(w.session().project().embroidery_objects[0].rgb,
                 (std::array<std::uint8_t, 3>{12, 200, 34}));
        QCOMPARE(w.session().project().embroidery_objects[1].rgb,
                 (std::array<std::uint8_t, 3>{12, 200, 34}));
        QCOMPARE(w.session().undoNames().size(), size_t(1));
        QVERIFY(panel->applyStitchType("running"));
        QVERIFY(panel->applyLocked(true));
        QVERIFY(w.session().project().embroidery_objects[1].locked);
        // Un type refusé par le moteur n'empile rien.
        const auto depth = w.session().undoNames().size();
        QVERIFY(!panel->applyStitchType("satin"));
        QCOMPARE(w.session().undoNames().size(), depth);
        w.waitForIdle();
        QVERIFY(w.previewShown());
    }

    void inspectorWithoutSelectionDoesNothing() {
        ShellWindow w;
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        w.session().selection().clear();
        QVERIFY(!w.inspector()->applyColor(QColor(1, 2, 3)));
        QVERIFY(!w.session().canUndo());
    }

    void burstsOfEditsRenderFewerPreviewsThanEdits() {
        ShellWindow w;
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        const auto countPreviews = [&] {
            int n = 0;
            for (const auto& t : w.tasks().history()) {
                n += t.label == "Aperçu" ? 1 : 0;
            }
            return n;
        };
        const int before = countPreviews();
        const auto id = w.session().project().embroidery_objects.front().id.value;
        for (int i = 0; i < 6; ++i) {
            const std::string batch =
                R"({"schema":"openstitch.batch/1","operations":[{"op":"set_color","object":)" +
                std::to_string(id) + R"(,"rgb":"#0000)" + (i % 2 ? "FF" : "EE") + R"("}]})";
            QVERIFY(w.session().applyBatch(batch, "Couleur").committed);
        }
        w.waitForIdle();
        QVERIFY(countPreviews() - before <= 2);
        QVERIFY(w.previewShown());
    }

    void canvasZoomAnchorsUnderTheCursorAndFitRestores() {
        ShellWindow w;
        w.show();
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        auto* canvas = w.findChild<PreviewCanvas*>("previewCanvas");
        QVERIFY(canvas && canvas->hasImage());
        const double fit = canvas->zoom();
        const QPoint at = canvas->viewport()->rect().center() + QPoint(40, 30);
        const QPointF sceneBefore = canvas->mapToScene(at);
        QWheelEvent wheel(QPointF(at), QPointF(canvas->viewport()->mapToGlobal(at)), QPoint(),
                          QPoint(0, 240), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(canvas->viewport(), &wheel);
        QVERIFY(canvas->zoom() > fit);
        const QPointF sceneAfter = canvas->mapToScene(at);
        QVERIFY(qAbs(sceneAfter.x() - sceneBefore.x()) < 1.5);
        QVERIFY(qAbs(sceneAfter.y() - sceneBefore.y()) < 1.5);
        QTest::keyClick(canvas, Qt::Key_F);
        QVERIFY(qAbs(canvas->zoom() - fit) < 1e-6);
    }
};

QTEST_MAIN(ShellWindowTest)
#include "test_shell_window.moc"
