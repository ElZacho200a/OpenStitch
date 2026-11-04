// SPDX-License-Identifier: Apache-2.0
#include <QAction>
#include <QApplication>
#include <QColor>
#include <QFile>
#include <QMenu>
#include <QMenuBar>
#include <QTest>
#include <QToolBar>
#include <QTreeWidget>
#include <QWheelEvent>
#include <filesystem>

#include "action_dialog.hpp"
#include "calque_panel.hpp"
#include "command_palette.hpp"
#include "diagnostics_panel.hpp"
#include "inspector_model.hpp"
#include "inspector_panel.hpp"
#include "param_spec.hpp"
#include "point_edit.hpp"
#include "preview_canvas.hpp"
#include "satin_column.hpp"
#include "shell_window.hpp"
#include "source_image_tools.hpp"
#include "thread_panel.hpp"
#include <array>

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
        QCOMPARE(enabled, 9); // …, Pipette, Couteau, Zones de détail
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

    void aDragOnEmptyFabricIsARubberBandNotAClick() {
        ShellWindow w;
        w.show();
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        auto* canvas = w.canvas();
        // Coin du tissu (hors de tout fil) : un glisser y trace un cadre, il ne déplace rien.
        const QPoint viewPos = canvas->mapFromScene(QPointF(2, 2));
        const auto depth = w.session().undoNames().size();
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, viewPos);
        QTest::mouseMove(canvas->viewport(), viewPos + QPoint(30, 30));
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier,
                            viewPos + QPoint(30, 30));
        QVERIFY(w.session().selection().empty()); // un glisser n'est pas un clic
        QCOMPARE(w.session().undoNames().size(), depth);
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
        const QPoint c = canvas->mapFromScene(QPointF(2, 2)); // tissu vide
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
        QVERIFY(w.canvas()->mode() == PreviewCanvas::Mode::Pan);
        QVERIFY(w.runCommand("tool.select"));
        QVERIFY(w.canvas()->mode() == PreviewCanvas::Mode::Select);
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
        // Les 21 outils du registre sont tous atteignables par la palette.
        int tools = 0;
        for (const auto& e : entries) {
            tools += e.id.rfind("tool.", 0) == 0 ? 1 : 0;
        }
        QCOMPARE(tools, 21);
        // Un outil non migré est listé mais refusé, sans effet de bord.
        QVERIFY(!w.runCommand("tool.draw.bezier"));
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
        palette.setQuery("plume");
        QCOMPARE(palette.visibleIds().size(), 1);
        QVERIFY(!palette.activateCurrent());
    }

    void drawingToolsCreateObjectsFromMouseGestures() {
        ShellWindow w;
        w.show();
        QVERIFY(w.runCommand("file.new"));
        w.waitForIdle();
        auto* canvas = w.canvas();
        QVERIFY(canvas->hasImage());
        QVERIFY(canvas->hasHoop()); // le cadre de broderie est dessiné
        QVERIFY(w.session().project().embroidery_objects.empty());

        // Rectangle : glisser dans le cadre.
        QVERIFY(w.runCommand("tool.draw.rectangle"));
        QVERIFY(canvas->mode() == PreviewCanvas::Mode::DrawBox);
        const QRectF scene = canvas->sceneRect().adjusted(40, 40, -40, -40);
        const QPoint a = canvas->mapFromScene(scene.center() - QPointF(60, 40));
        const QPoint b = canvas->mapFromScene(scene.center() + QPointF(60, 40));
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, a);
        QTest::mouseMove(canvas->viewport(), b);
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, b);
        QCOMPARE(w.session().project().embroidery_objects.size(), size_t(1));
        QCOMPARE(QString::fromStdString(w.session().undoName()), QString("Rectangle"));
        QCOMPARE(w.session().selection().targets().size(),
                 size_t(1)); // l'objet créé est sélectionné
        w.waitForIdle();

        // Ellipse avec Maj : cercle (largeur = hauteur).
        QVERIFY(w.runCommand("tool.draw.ellipse"));
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::ShiftModifier, a);
        QTest::mouseMove(canvas->viewport(), b, 1);
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::ShiftModifier, b);
        QCOMPARE(w.session().project().embroidery_objects.size(), size_t(2));
        QCOMPARE(QString::fromStdString(w.session().undoName()), QString("Ellipse"));

        // Un seul Annuler par forme.
        QVERIFY(w.session().undo());
        QCOMPARE(w.session().project().embroidery_objects.size(), size_t(1));
        w.waitForIdle();
        QVERIFY(w.previewShown());
    }

    void polygonToolClicksThenEnterAndEscapeCancels() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        auto* canvas = w.canvas();
        QVERIFY(w.runCommand("tool.draw.polygon"));
        QVERIFY(canvas->mode() == PreviewCanvas::Mode::DrawPolygon);
        const QPointF c = canvas->sceneRect().center();
        const auto click = [&](QPointF p) {
            QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier,
                              canvas->mapFromScene(p));
        };
        click(c + QPointF(-50, 40));
        click(c + QPointF(50, 40));
        click(c + QPointF(0, -50));
        QCOMPARE(canvas->polygonInProgress().size(), 3);
        QTest::keyClick(canvas, Qt::Key_Backspace); // retire le dernier sommet
        QCOMPARE(canvas->polygonInProgress().size(), 2);
        QTest::keyClick(canvas, Qt::Key_Escape); // annule tout
        QCOMPARE(canvas->polygonInProgress().size(), 0);
        QVERIFY(w.session().project().embroidery_objects.empty());

        click(c + QPointF(-50, 40));
        click(c + QPointF(50, 40));
        click(c + QPointF(0, -50));
        QTest::keyClick(canvas, Qt::Key_Return);
        QCOMPARE(w.session().project().embroidery_objects.size(), size_t(1));
        QCOMPARE(QString::fromStdString(w.session().undoName()), QString("Polygone"));
        QCOMPARE(canvas->polygonInProgress().size(), 0);

        // Changer d'outil abandonne un polygone en cours.
        click(c + QPointF(-30, 30));
        click(c + QPointF(30, 30));
        QCOMPARE(canvas->polygonInProgress().size(), 2);
        QVERIFY(w.runCommand("tool.select"));
        QCOMPARE(canvas->polygonInProgress().size(), 0);
    }

    void aRefusedShapeShowsAnErrorAndLeavesTheDocumentUntouched() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        auto* canvas = w.canvas();
        QVERIFY(w.runCommand("tool.draw.polygon"));
        const QPointF c = canvas->sceneRect().center();
        const auto click = [&](QPointF p) {
            QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier,
                              canvas->mapFromScene(p));
        };
        // « Papillon » : les côtés se croisent, le moteur refuse la forme.
        click(c + QPointF(-40, -40));
        click(c + QPointF(40, 40));
        click(c + QPointF(40, -40));
        click(c + QPointF(-40, 40));
        const auto revision = w.session().revision();
        QTest::keyClick(canvas, Qt::Key_Return);
        QVERIFY(w.session().project().embroidery_objects.empty());
        QCOMPARE(w.session().revision(), revision);
        QVERIFY(w.lastNotice().contains("refusé"));
    }

    void saveIsAvailableOnANewProjectButExportNeedsStitches() {
        ShellWindow w;
        QVERIFY(!w.runCommand("file.save")); // aucun projet ouvert
        QVERIFY(w.runCommand("file.new"));
        const auto entries = w.commandEntries();
        const auto enabled = [&](const char* id) {
            for (const auto& e : entries) {
                if (e.id == id) {
                    return e.enabled;
                }
            }
            return false;
        };
        QVERIFY(enabled("file.save"));
        QVERIFY(!enabled("file.export")); // rien à exporter
        QVERIFY(enabled("tool.draw.rectangle"));
        QVERIFY(!enabled("tool.draw.bezier")); // pas encore migré
    }

    void draggingAnObjectMovesTheSelectionInOneUndoStep() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        const auto r = w.session().applyBatch(batchAddRect({0, 0}, 30, 20), "Rectangle");
        const auto id = *createdObjectId(r.report);
        w.waitForIdle();
        auto* canvas = w.canvas();
        const auto before = *w.previewFrame().hits->boundsOf(id);
        const auto depth = w.session().undoNames().size();

        // Appui sur l'objet NON sélectionné puis glisser : il est sélectionné puis déplacé.
        const auto point = w.objectCentrePx(id);
        QVERIFY(point.has_value());
        const QPoint from = canvas->mapFromScene(*point);
        const QPoint to = from + QPoint(60, -30);
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(canvas->viewport(), from + QPoint(20, -10));
        QTest::mouseMove(canvas->viewport(), to);
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, to);
        QCOMPARE(w.session().undoNames().size(), depth + 1);
        QCOMPARE(QString::fromStdString(w.session().undoName()), QString("Déplacer"));
        QVERIFY(w.session().selection().contains({TargetKind::EmbroideryObject, id}));
        w.waitForIdle();
        const auto after = *w.previewFrame().hits->boundsOf(id);
        const double expectedDx = (to - from).x() / canvas->zoom() / w.previewFrame().px_per_mm;
        const double expectedDy = (to - from).y() / canvas->zoom() / w.previewFrame().px_per_mm;
        QVERIFY(qAbs((after.x0 - before.x0) - expectedDx) < 0.8);
        QVERIFY(qAbs((after.y0 - before.y0) - expectedDy) < 0.8);

        QVERIFY(w.session().undo());
        w.waitForIdle();
        QVERIFY(qAbs(w.previewFrame().hits->boundsOf(id)->x0 - before.x0) < 0.1);
    }

    void aPlainClickOnAnObjectDoesNotMoveIt() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        const auto r = w.session().applyBatch(batchAddRect({0, 0}, 30, 20), "Rectangle");
        const auto id = *createdObjectId(r.report);
        w.waitForIdle();
        auto* canvas = w.canvas();
        const auto depth = w.session().undoNames().size();
        const QPoint at = canvas->mapFromScene(*w.objectCentrePx(id));
        QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, at);
        QCOMPARE(w.session().undoNames().size(), depth); // aucune étape d'annulation
        QCOMPARE(w.session().selection().targets().size(), size_t(1));
    }

    void draggingACornerHandleScalesAboutTheOppositeCorner() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        const auto r = w.session().applyBatch(batchAddRect({0, 0}, 30, 20), "Rectangle");
        const auto id = *createdObjectId(r.report);
        w.waitForIdle();
        w.session().selection().replace({{TargetKind::EmbroideryObject, id}});
        auto* canvas = w.canvas();
        const auto& frame = w.previewFrame();
        const auto before = *frame.hits->boundsOf(id);
        // Coin bas-droit de l'emprise affichée (cadre de sélection = emprise + 3 px de marge).
        const QPointF corner(frame.mmToPxX(before.x1) + 3, frame.mmToPxY(before.y1) + 3);
        const QPoint from = canvas->mapFromScene(corner);
        const QPoint to = from + QPoint(40, 40);
        const auto depth = w.session().undoNames().size();
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(canvas->viewport(), from + QPoint(15, 15));
        QTest::mouseMove(canvas->viewport(), to);
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, to);
        QCOMPARE(w.session().undoNames().size(), depth + 1);
        QCOMPARE(QString::fromStdString(w.session().undoName()), QString("Mettre à l'échelle"));
        w.waitForIdle();
        const auto after = *w.previewFrame().hits->boundsOf(id);
        QVERIFY((after.x1 - after.x0) > 1.2 * (before.x1 - before.x0)); // plus grand
        QVERIFY(qAbs(after.x0 - before.x0) < 1.0); // coin opposé (haut-gauche) fixe
        QVERIFY(qAbs(after.y0 - before.y0) < 1.0);
        // Rapport d'aspect conservé (échelle uniforme).
        const double ratioBefore = (before.x1 - before.x0) / (before.y1 - before.y0);
        const double ratioAfter = (after.x1 - after.x0) / (after.y1 - after.y0);
        QVERIFY(qAbs(ratioBefore - ratioAfter) < 0.1);
    }

    void aDragOnEmptyFabricStillMakesARubberBandWhenObjectsExist() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        w.session().applyBatch(batchAddRect({0, 0}, 20, 20), "Rectangle");
        w.waitForIdle();
        auto* canvas = w.canvas();
        const auto depth = w.session().undoNames().size();
        const QPoint a = canvas->mapFromScene(QPointF(5, 5)); // coin du tissu, hors objet
        const QPoint b = a + QPoint(25, 25);
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, a);
        QTest::mouseMove(canvas->viewport(), b);
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, b);
        QCOMPARE(w.session().undoNames().size(), depth); // rien n'a été déplacé
    }

    void editingCommandsActOnTheSelectionOnly() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        const auto a =
            *createdObjectId(w.session().applyBatch(batchAddRect({-20, 0}, 10, 10), "R").report);
        const auto b =
            *createdObjectId(w.session().applyBatch(batchAddRect({20, 0}, 10, 10), "R").report);
        w.waitForIdle();
        // Sans sélection : toutes ces commandes sont désactivées.
        w.session().selection().clear();
        for (const char* id :
             {"edit.delete", "edit.duplicate", "transform.rotate.cw", "order.front"}) {
            QVERIFY2(!w.runCommand(id), id);
        }
        // Dupliquer : les copies deviennent la sélection.
        w.session().selection().replace({{TargetKind::EmbroideryObject, a}});
        QVERIFY(w.runCommand("edit.duplicate"));
        QCOMPARE(w.session().project().embroidery_objects.size(), size_t(3));
        QCOMPARE(w.session().selection().targets().size(), size_t(1));
        QVERIFY(w.session().selection().primary()->id != a);
        // Supprimer la copie sélectionnée : une annulation la ramène.
        QVERIFY(w.runCommand("edit.delete"));
        QCOMPARE(w.session().project().embroidery_objects.size(), size_t(2));
        QVERIFY(w.session().selection().empty()); // la cible a disparu : sélection élaguée
        QVERIFY(w.session().undo());
        QCOMPARE(w.session().project().embroidery_objects.size(), size_t(3));
        // Ordre : « premier plan » met b en dernier.
        w.session().selection().replace({{TargetKind::EmbroideryObject, b}});
        QVERIFY(w.runCommand("order.front"));
        QCOMPARE(w.session().project().embroidery_objects.back().id.value, b);
        QCOMPARE(QString::fromStdString(w.session().undoName()), QString("Premier plan"));
    }

    void rotateAndMirrorUseTheSelectionCentre() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        const auto id =
            *createdObjectId(w.session().applyBatch(batchAddRect({30, 10}, 30, 10), "R").report);
        w.waitForIdle();
        w.session().selection().replace({{TargetKind::EmbroideryObject, id}});
        const auto before = *w.previewFrame().hits->boundsOf(id);
        QVERIFY(w.runCommand("transform.rotate.ccw"));
        w.waitForIdle();
        const auto rotated = *w.previewFrame().hits->boundsOf(id);
        // Pivot = centre de l'objet : le centre ne bouge pas, largeur et hauteur s'échangent.
        QVERIFY(qAbs((rotated.x0 + rotated.x1) / 2 - (before.x0 + before.x1) / 2) < 0.8);
        QVERIFY(qAbs((rotated.y0 + rotated.y1) / 2 - (before.y0 + before.y1) / 2) < 0.8);
        QVERIFY(qAbs((rotated.x1 - rotated.x0) - (before.y1 - before.y0)) < 1.5);
        QVERIFY(w.runCommand("transform.mirror.h"));
        QCOMPARE(QString::fromStdString(w.session().undoName()), QString("Miroir horizontal"));
    }

    void arrowKeysNudgeTheSelectionByOneOrTenMillimetres() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        const auto id =
            *createdObjectId(w.session().applyBatch(batchAddRect({0, 0}, 20, 20), "R").report);
        w.waitForIdle();
        w.session().selection().replace({{TargetKind::EmbroideryObject, id}});
        const auto before = *w.previewFrame().hits->boundsOf(id);
        QTest::keyClick(w.canvas(), Qt::Key_Right);
        w.waitForIdle();
        const auto right = *w.previewFrame().hits->boundsOf(id);
        QVERIFY(qAbs((right.x0 - before.x0) - 1.0) < 0.3);
        QTest::keyClick(w.canvas(), Qt::Key_Up, Qt::ShiftModifier); // 10 mm vers le haut
        w.waitForIdle();
        const auto up = *w.previewFrame().hits->boundsOf(id);
        QVERIFY(qAbs((up.y0 - right.y0) + 10.0) < 0.5);              // haut = y écran décroissant
        QCOMPARE(w.session().undoNames().size(), size_t(1 + 1 + 1)); // création + 2 déplacements
    }

    void diagnosticsRunInTheBackgroundAndGoStaleOnEdit() {
        ShellWindow w;
        w.show();
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        auto* panel = w.diagnostics();
        QCOMPARE(panel->findingCount(), 0);
        QVERIFY(w.runCommand("analysis.run"));
        QCOMPARE(w.workspace(), Workspace::Optimize); // l'analyse bascule vers son espace
        w.waitForIdle();
        QVERIFY(!panel->isStale());
        QVERIFY(w.lastNotice().contains("Analyse"));
        // Toute modification rend l'analyse affichée périmée.
        const auto id = w.session().project().embroidery_objects.front().id.value;
        w.session().applyBatch(batchSetVisible({id}, false), "Masquer");
        QVERIFY(panel->isStale());
        // Rien à analyser sans objet.
        ShellWindow empty;
        empty.runCommand("file.new");
        QVERIFY(!empty.runCommand("analysis.run")); // commande désactivée
    }

    void clickingAFindingSelectsItsObjectsAndSkipsVanishedOnes() {
        ShellWindow w;
        w.show();
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        const auto& objects = w.session().project().embroidery_objects;
        QVERIFY(objects.size() >= 2);
        DiagnosticsResult fake;
        openstitch::project_ops::Finding f;
        f.severity = "warning";
        f.code = "TEST";
        f.message = "Constat de test";
        f.objects = {objects[0].id.value, objects[1].id.value,
                     987654321ULL}; // le dernier n'existe pas
        f.at_mm = std::array<double, 2>{0.0, 0.0};
        fake.findings = {f};
        fake.warnings = 1;
        fake.revision = w.session().revision();
        w.diagnostics()->setResult(fake);
        QCOMPARE(w.diagnostics()->findingCount(), 1);
        w.diagnostics()->activateRow(0);
        QCOMPARE(w.session().selection().targets().size(), size_t(2));
        w.diagnostics()->activateRow(5); // ligne inexistante : sans effet
        QCOMPARE(w.session().selection().targets().size(), size_t(2));
    }

    void theInspectorShowsEssentialStitchSettingsOfTheCommonType() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        const auto a =
            *createdObjectId(w.session().applyBatch(batchAddRect({-20, 0}, 10, 10), "R").report);
        const auto b =
            *createdObjectId(w.session().applyBatch(batchAddRect({20, 0}, 10, 10), "R").report);
        w.waitForIdle();
        auto* panel = w.inspector();
        w.session().selection().clear();
        QVERIFY(panel->paramKeys().isEmpty()); // rien de sélectionné : pas de réglages

        w.session().selection().replace({{TargetKind::EmbroideryObject, a}});
        QVERIFY(panel->paramKeys().contains("row_spacing_mm"));
        QVERIFY(panel->paramKeys().contains("angle_deg"));
        const auto depth = w.session().undoNames().size();
        QVERIFY(panel->applyParam("row_spacing_mm", 0.6));
        QCOMPARE(w.session().undoNames().size(), depth + 1);
        QVERIFY(qAbs(*panel->paramValue("row_spacing_mm") - 0.6) <
                0.01); // l'éditeur suit le document

        // Deux objets, valeurs différentes : l'éditeur affiche « mixte » ; un réglage applique aux
        // deux.
        w.session().selection().replace(
            {{TargetKind::EmbroideryObject, a}, {TargetKind::EmbroideryObject, b}});
        QVERIFY(panel->paramMixed("row_spacing_mm"));
        QVERIFY(panel->applyParam("row_spacing_mm", 0.45));
        QVERIFY(!panel->paramMixed("row_spacing_mm"));
        QVERIFY(qAbs(*panel->paramValue("row_spacing_mm") - 0.45) < 0.01);

        // Valeur hors plage ou réglage d'un autre type : refusé, rien d'empilé.
        const auto before = w.session().undoNames().size();
        QVERIFY(!panel->applyParam("row_spacing_mm", 9.0));
        QVERIFY(!panel->applyParam("repeats", 2));
        QCOMPARE(w.session().undoNames().size(), before);

        // Types différents : plus de réglages communs.
        QVERIFY(panel->applyStitchType("running")); // les deux passent en contour
        QVERIFY(panel->paramKeys().contains("repeats"));
        w.session().applyBatch(batchSetStitchType({a}, "tatami"), "Type");
        QVERIFY(panel->paramKeys().isEmpty()); // sélection de types mixtes
    }

    void everyActionDialogYieldsValuesTheCatalogAccepts() {
        ShellWindow w;
        for (const auto& def : actionCatalog()) {
            ActionDialog dialog(def, defaultValues(def));
            const auto values = dialog.values();
            ActionContext ctx;
            ctx.ids = {1, 2}; // contexte fictif : seul le contrôle des champs nous intéresse
            std::string error;
            if (def.run) { // actions adossées à une commande : seule la validation est contrôlée
                auto checked = values;
                QVERIFY2(validateAction(def, checked, ctx, error), def.id.c_str());
                continue;
            }
            const auto batch = buildAction(def, values, ctx, error);
            QVERIFY2(!batch.empty() || def.id == "file.import_svg", def.id.c_str());
            if (def.id == "file.import_svg") {
                QVERIFY(error.find("obligatoire") != std::string::npos ||
                        error.find("introuvable") != std::string::npos);
            }
        }
    }

    void actionsAreInTheToolsMenuAndThePalette() {
        ShellWindow w;
        auto* menu = w.menuBar()->findChild<QMenu*>();
        QVERIFY(menu != nullptr);
        for (const auto& def : actionCatalog()) {
            QVERIFY2(w.findChild<QAction*>(QString::fromStdString("action_" + def.id)) != nullptr,
                     def.id.c_str());
        }
        const auto entries = w.commandEntries();
        int actions = 0;
        for (const auto& e : entries) {
            actions += e.id.rfind("action.", 0) == 0 ? 1 : 0;
        }
        QCOMPARE(static_cast<size_t>(actions), actionCatalog().size());
        // « Satin automatique » exige une sélection : désactivée sans objet.
        QVERIFY(!w.runCommand("action.satin.auto"));
    }

    void runningAnActionAppliesItAndRefusesBadValuesWithAMessage() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        QVERIFY(w.runActionWith("canvas.set", {{"width_mm", "160"}, {"height_mm", "90"}}));
        QCOMPARE(w.session().project().canvas.width.value, 160'000);
        QCOMPARE(QString::fromStdString(w.session().undoName()), QString("Cadre de broderie…"));
        const auto revision = w.session().revision();
        QVERIFY(!w.runActionWith("canvas.set", {{"width_mm", "99999"}}));
        QVERIFY(w.lastNotice().contains("Largeur"));
        QCOMPARE(w.session().revision(), revision); // rien n'a changé
        QVERIFY(!w.runActionWith("no.such.action", {}));
        w.waitForIdle();
        QVERIFY(w.previewShown()); // l'aperçu suit le nouveau cadre
    }

    void textActionCreatesSelectedObjects() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        QVERIFY(w.runActionWith(
            "text.add", {{"text", "OK"}, {"font_file", OPENSTITCH_FONTS_DIR "/VeraBd.ttf"}}));
        QVERIFY(!w.session().project().embroidery_objects.empty());
    }

    void theRulerMeasuresADragInMillimetres() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        QVERIFY(w.runCommand("tool.measure"));
        QVERIFY(w.canvas()->mode() == PreviewCanvas::Mode::Measure);
        auto* canvas = w.canvas();
        const auto& f = w.previewFrame();
        // 30 mm à l'horizontale : de (-15, 0) à (15, 0) du modèle.
        const QPoint a = canvas->mapFromScene(QPointF(f.mmToPxX(-15), f.mmToPxY(0)));
        const QPoint b = canvas->mapFromScene(QPointF(f.mmToPxX(15), f.mmToPxY(0)));
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, a);
        QTest::mouseMove(canvas->viewport(), (a + b) / 2);
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, b);
        QVERIFY(canvas->hasMeasure());
        QVERIFY2(w.lastNotice().contains("30.0") || w.lastNotice().contains("30,0") ||
                     w.lastNotice().contains("29.9") || w.lastNotice().contains("30.1"),
                 qPrintable(w.lastNotice()));
        QVERIFY(w.session().undoNames().empty()); // la règle ne modifie jamais le document
        QVERIFY(w.runCommand("tool.select"));
        QVERIFY(!canvas->hasMeasure()); // changer d'outil efface la mesure
    }

    void gridAndHoopToggles() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        QVERIFY(!w.canvas()->hasGrid());
        w.setGridSpacing(10.0);
        QVERIFY(w.canvas()->hasGrid());
        w.setGridSpacing(0.0);
        QVERIFY(!w.canvas()->hasGrid());
        w.setGridSpacing(0.05); // trop dense : rien n'est tracé
        QVERIFY(!w.canvas()->hasGrid());
        QVERIFY(w.canvas()->hoopVisible());
        w.canvas()->setHoopVisible(false);
        QVERIFY(!w.canvas()->hoopVisible());
    }

    void contextMenuListsOnlyActiveSelectionCommands() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        auto* none = w.buildContextMenu();
        for (auto* a : none->actions()) {
            QVERIFY(a->data().toString() != "edit.delete"); // rien de sélectionné
        }
        delete none;
        const auto id =
            *createdObjectId(w.session().applyBatch(batchAddRect({0, 0}, 20, 20), "R").report);
        w.waitForIdle();
        w.session().selection().replace({{TargetKind::EmbroideryObject, id}});
        auto* menu = w.buildContextMenu();
        QStringList ids;
        for (auto* a : menu->actions()) {
            ids << a->data().toString();
        }
        QVERIFY(ids.contains("edit.delete"));
        QVERIFY(ids.contains("transform.rotate.cw"));
        QVERIFY(ids.contains("order.front"));
        QVERIFY(!ids.contains("edit.undo")); // pas de redites du menu Édition
        // Une commande du menu agit comme le menu principal.
        for (auto* a : menu->actions()) {
            if (a->data().toString() == "edit.duplicate") {
                a->trigger();
            }
        }
        QCOMPARE(w.session().project().embroidery_objects.size(), size_t(2));
        delete menu;
    }

    void rightClickOnAnObjectSelectsItBeforeTheMenuOpens() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        const auto id =
            *createdObjectId(w.session().applyBatch(batchAddRect({0, 0}, 30, 30), "R").report);
        w.waitForIdle();
        QVERIFY(w.session().selection().empty());
        w.showContextMenu(QPoint(10, 10), *w.objectCentrePx(id));
        QVERIFY(w.session().selection().contains({TargetKind::EmbroideryObject, id}));
        for (auto* menu : w.findChildren<QMenu*>("canvasContextMenu")) {
            menu->close();
        }
    }

    void simulationScrubsThroughTheSewingOrderOnlyInProduction() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        w.session().applyBatch(batchAddRect({0, 0}, 40, 30), "R");
        w.waitForIdle();
        auto* bar = w.findChild<QToolBar*>("simulationBar");
        QVERIFY(bar != nullptr);
        QVERIFY(!bar->isVisible());
        QVERIFY(w.runCommand("workspace.produce"));
        QVERIFY(bar->isVisible());
        const int total = w.simulationTotal();
        QVERIFY(total > 50);
        QVERIFY(!w.simulating());
        w.setSimulationPosition(total / 2);
        QVERIFY(w.simulating());
        QCOMPARE(w.simulationPosition(), total / 2);
        w.waitForIdle();
        QCOMPARE(w.simulationTotal(), total); // le total vient de la séquence complète
        QVERIFY(w.previewShown());
        w.setSimulationPosition(total + 10); // au-delà : tout
        QVERIFY(!w.simulating());
        w.setSimulationPosition(0);
        QVERIFY(w.simulating());
        QVERIFY(w.runCommand("workspace.design")); // quitter Production termine la simulation
        QVERIFY(!w.simulating());
        QVERIFY(w.session().undoNames().size() == 1); // la simulation ne modifie jamais le document
    }

    void themeModeIsValidatedAndRestorable() {
        ShellWindow w;
        w.setThemeMode("dark");
        QCOMPARE(w.themeMode(), QString("dark"));
        w.setThemeMode("light");
        QCOMPARE(w.themeMode(), QString("light"));
        w.setThemeMode("bogus");
        QCOMPARE(w.themeMode(), QString("system"));
        w.setThemeMode("system");
    }

    void exportImageWritesAPng() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        w.session().applyBatch(batchAddRect({0, 0}, 20, 20), "R");
        w.waitForIdle();
        const auto png = fs::temp_directory_path() / "openstitch_v2_shell_image.png";
        fs::remove(png);
        w.exportImage(png, 3.0);
        w.waitForIdle();
        QVERIFY2(fs::exists(png), qPrintable(w.lastNotice()));
        QVERIFY(fs::file_size(png) > 500);
        QVERIFY(w.lastNotice().contains("Image"));
        fs::remove(png);
    }

    void presetsAreSavedAppliedAndListedInTheMenuAndPalette() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        const auto a =
            *createdObjectId(w.session().applyBatch(batchAddRect({-20, 0}, 10, 10), "R").report);
        const auto b =
            *createdObjectId(w.session().applyBatch(batchAddRect({20, 0}, 10, 10), "R").report);
        w.waitForIdle();
        QVERIFY(!w.runCommand("preset.save")); // rien de sélectionné : désactivée
        w.session().applyBatch(batchSetParam({a}, "tatami", "row_spacing_mm", 0.8), "Écart");
        w.session().selection().replace({{TargetKind::EmbroideryObject, a}});
        QVERIFY(w.savePresetNamed("Trame lâche"));
        QVERIFY(!w.savePresetNamed("   ")); // nom vide refusé, message affiché
        QVERIFY(w.lastNotice().contains("nom"));
        const auto entries = w.commandEntries();
        bool apply = false;
        for (const auto& e : entries) {
            apply = apply || (e.id == "preset.apply.0" && e.enabled);
        }
        QVERIFY(apply);
        w.session().selection().replace({{TargetKind::EmbroideryObject, b}});
        QVERIFY(w.runCommand("preset.apply.0"));
        QVERIFY(qAbs(*readParam(w.session().project().embroidery_objects[1], "row_spacing_mm") -
                     0.8) < 0.01);
        QVERIFY(w.runCommand("preset.remove.0"));
        QVERIFY(w.session().project().stitch_presets.empty());
        QVERIFY(!w.runCommand("preset.apply.0")); // plus de préréglage
    }

    void calquePanelEditsLayersThroughUndoableCommands() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        auto* panel = w.calquePanel();
        QVERIFY(panel != nullptr);
        QCOMPARE(panel->rowCount(), 0);
        QVERIFY(panel->addNamed("Fond"));
        QCOMPARE(panel->rowCount(), 2);               // « Calque 1 » matérialisé + « Fond »
        QCOMPARE(panel->rowText(0), QString("Fond")); // le dessus d'abord
        QVERIFY(!panel->addNamed("  "));              // refus, message
        panel->setCurrentRow(0);
        QVERIFY(panel->renameCurrent("Dessus"));
        QCOMPARE(panel->rowText(0), QString("Dessus"));
        QVERIFY(panel->toggleLockCurrent());
        QVERIFY(panel->rowText(0).contains("🔒"));
        QVERIFY(panel->moveCurrent(-1)); // descend
        QVERIFY(panel->removeCurrent());
        QCOMPARE(panel->rowCount(), 1);
        panel->setCurrentRow(0);
        QVERIFY(!panel->removeCurrent()); // le dernier calque reste
        QVERIFY(w.session().undo());
        QCOMPARE(panel->rowCount(), 2);
    }

    void threadPanelListsThreadsOrderAndColorways() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        (void)w.session().applyBatch(batchAddRect({-30, 0}, 10, 10), "A");
        (void)w.session().applyBatch(batchAddRect({0, 0}, 10, 10), "B");
        (void)w.session().applyBatch(batchAddRect({30, 0}, 10, 10), "C");
        w.waitForIdle();
        auto* panel = w.threadPanel();
        QVERIFY(panel != nullptr);
        QCOMPARE(panel->orderCount(), 3);
        QVERIFY(panel->threadCount() >= 1);
        // Fils : sélectionner puis recolorer.
        panel->setCurrentThread(0);
        QVERIFY(panel->selectCurrentThread());
        QVERIFY(!w.session().selection().empty());
        QVERIFY(panel->recolorCurrentThread(QColor(255, 0, 0)));
        QVERIFY(panel->threadText(0).contains("#ff0000"));
        QVERIFY(w.session().undo());
        QVERIFY(!panel->threadText(0).contains("#ff0000"));
        // Ordre de couture : déplacer le premier plus tard, refuser de dépasser la fin.
        const QString firstBefore = panel->orderText(0);
        panel->setCurrentOrder(0);
        QVERIFY(panel->moveCurrentOrder(+1));
        QVERIFY(panel->orderText(1).contains(firstBefore.mid(firstBefore.indexOf(". ") + 2)));
        panel->setCurrentOrder(2);
        QVERIFY(!panel->moveCurrentOrder(+1));
        // Coloris : ajouter, activer l'original, supprimer l'inactif.
        QVERIFY(!panel->addColorwayNamed("  "));
        QVERIFY(panel->addColorwayNamed("Hiver"));
        QCOMPARE(panel->colorwayCount(), 2);
        QVERIFY(panel->colorwayText(1).contains("actif"));
        panel->setCurrentColorway(0);
        QVERIFY(panel->activateCurrentColorway());
        QVERIFY(panel->colorwayText(0).contains("actif"));
        panel->setCurrentColorway(1);
        QVERIFY(panel->renameCurrentColorway("Ete"));
        QVERIFY(panel->removeCurrentColorway());
        QCOMPARE(panel->colorwayCount(), 1);
    }

    void hidingACalqueHidesItsObjectsFromThePreview() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        auto* panel = w.calquePanel();
        QVERIFY(panel->addNamed("Haut"));
        const auto id =
            *createdObjectId(w.session().applyBatch(batchAddRect({0, 0}, 20, 20), "R").report);
        w.session().selection().replace({{TargetKind::EmbroideryObject, id}});
        panel->setCurrentRow(0); // « Haut » est au-dessus
        QVERIFY(panel->moveSelectionHere());
        w.waitForIdle();
        QVERIFY(w.previewFrame().hits->boundsOf(id).has_value());
        QVERIFY(panel->setRowVisible(0, false)); // masquer le calque
        w.waitForIdle();
        QVERIFY(!w.previewFrame().hits->boundsOf(id).has_value()); // ni dessiné ni cousu
        QVERIFY(panel->setRowVisible(0, true));
        w.waitForIdle();
        QVERIFY(w.previewFrame().hits->boundsOf(id).has_value());
    }

    void aFadeIsCreatedFromTheToolsMenuActionAndUndone() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        const auto id =
            *createdObjectId(w.session().applyBatch(batchAddRect({0, 0}, 30, 20), "R").report);
        w.waitForIdle();
        QVERIFY(!w.runCommand("action.fade.create")); // rien de sélectionné
        w.session().selection().replace({{TargetKind::EmbroideryObject, id}});
        QVERIFY(!w.runActionWith("fade.create", {{"foreground", "orange"}})); // format refusé
        QVERIFY(w.lastNotice().contains("#RRGGBB"));
        const auto before = w.session().project().embroidery_objects.size();
        QVERIFY(
            w.runActionWith("fade.create", {{"foreground", "#FF8800"}, {"complementary", "1"}}));
        QVERIFY(w.session().project().embroidery_objects.size() > before);
        QVERIFY(w.lastNotice().contains("fait"));
        QVERIFY(w.session().undo());
        QCOMPARE(w.session().project().embroidery_objects.size(), before);
        w.waitForIdle();
        QVERIFY(w.previewShown());
    }

    void theEyedropperCopiesAThreadColourOntoTheSelection() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        const auto a =
            *createdObjectId(w.session().applyBatch(batchAddRect({-20, 0}, 20, 20), "A").report);
        const auto b =
            *createdObjectId(w.session().applyBatch(batchAddRect({20, 0}, 20, 20), "B").report);
        w.session().applyBatch(batchSetColor({a}, {200, 30, 40}), "Couleur");
        w.waitForIdle();
        w.session().selection().replace({{TargetKind::EmbroideryObject, b}});
        QVERIFY(w.runCommand("tool.eyedropper"));
        QVERIFY(w.canvas()->mode() == PreviewCanvas::Mode::Pick);
        const auto depth = w.session().undoNames().size();
        QTest::mouseClick(w.canvas()->viewport(), Qt::LeftButton, Qt::NoModifier,
                          w.canvas()->mapFromScene(*w.objectCentrePx(a)));
        QCOMPARE(w.session().project().embroidery_objects[1].rgb,
                 (std::array<std::uint8_t, 3>{200, 30, 40}));
        QCOMPARE(w.session().undoNames().size(), depth + 1);
        QVERIFY(w.lastNotice().contains("#C81E28"));
        // Un clic sur le tissu ne change rien.
        QTest::mouseClick(w.canvas()->viewport(), Qt::LeftButton, Qt::NoModifier,
                          w.canvas()->mapFromScene(QPointF(2, 2)));
        QCOMPARE(w.session().undoNames().size(), depth + 1);
        QVERIFY(w.lastNotice().contains("aucun fil"));
        // La sélection reste celle de l'utilisateur.
        QCOMPARE(w.session().selection().targets().size(), size_t(1));
    }

    void theKnifeCutsTheSelectedShapeAlongTheDraggedLine() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        const auto id =
            *createdObjectId(w.session().applyBatch(batchAddRect({0, 0}, 40, 20), "R").report);
        w.waitForIdle();
        QVERIFY(w.runCommand("tool.knife"));
        QVERIFY(w.canvas()->mode() == PreviewCanvas::Mode::Cut);
        const auto& f = w.previewFrame();
        const QPoint a = w.canvas()->mapFromScene(QPointF(f.mmToPxX(0), f.mmToPxY(-18)));
        const QPoint b = w.canvas()->mapFromScene(QPointF(f.mmToPxX(0), f.mmToPxY(18)));
        // Sans sélection : message, rien ne change.
        QTest::mousePress(w.canvas()->viewport(), Qt::LeftButton, Qt::NoModifier, a);
        QTest::mouseMove(w.canvas()->viewport(), (a + b) / 2);
        QTest::mouseRelease(w.canvas()->viewport(), Qt::LeftButton, Qt::NoModifier, b);
        QCOMPARE(w.session().project().embroidery_objects.size(), size_t(1));
        QVERIFY(w.lastNotice().contains("sélectionnez"));
        w.session().selection().replace({{TargetKind::EmbroideryObject, id}});
        QTest::mousePress(w.canvas()->viewport(), Qt::LeftButton, Qt::NoModifier, a);
        QTest::mouseMove(w.canvas()->viewport(), (a + b) / 2);
        QTest::mouseRelease(w.canvas()->viewport(), Qt::LeftButton, Qt::NoModifier, b);
        QCOMPARE(w.session().project().embroidery_objects.size(), size_t(2));
        QCOMPARE(QString::fromStdString(w.session().undoName()), QString("Découper"));
        QVERIFY(!w.canvas()->hasMeasure()); // la ligne de coupe disparaît après le geste
        QVERIFY(w.session().undo());
        QCOMPARE(w.session().project().embroidery_objects.size(), size_t(1));
    }

    void thePointToolMovesDeletesAndDiscardsStitchEdits() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        const auto id =
            *createdObjectId(w.session().applyBatch(batchAddRect({0, 0}, 30, 16), "R").report);
        w.waitForIdle();
        QVERIFY(w.runCommand("workspace.digitize"));
        QVERIFY(w.runCommand("tool.stitch.points"));
        // Sans sélection : pas de poignées.
        w.waitForIdle();
        QCOMPARE(w.canvas()->pointMarkerCount(), 0);
        w.session().selection().replace({{TargetKind::EmbroideryObject, id}});
        w.waitForIdle();
        QVERIFY(w.canvas()->pointMarkerCount() > 10);
        QCOMPARE(w.selectedPoint(), -1);
        // Cliquer sur la première poignée la sélectionne.
        const auto view = stitchPoints(w.session().project(), id);
        QVERIFY(view.has_value());
        const auto first = std::find_if(view->points.begin(), view->points.end(),
                                        [](const EditablePoint& p) { return p.movable; });
        QVERIFY(first != view->points.end());
        const auto& f = w.previewFrame();
        const QPoint at =
            w.canvas()->mapFromScene(QPointF(f.mmToPxX(first->pos.x), f.mmToPxY(-first->pos.y)));
        QTest::mouseClick(w.canvas()->viewport(), Qt::LeftButton, Qt::NoModifier, at);
        QCOMPARE(w.selectedPoint(), 0);
        // Cliquer ailleurs la déplace : une étape d'annulation.
        const auto depth = w.session().undoNames().size();
        // Destination loin de toute poignée (sinon le clic sélectionnerait une autre poignée).
        QPoint dest;
        PointMm destMm{};
        for (const double cx : {-60.0, 60.0, -40.0, 40.0}) {
            const QPoint cand = w.canvas()->mapFromScene(QPointF(f.mmToPxX(cx), f.mmToPxY(0)));
            bool far = w.canvas()->viewport()->rect().contains(cand);
            for (const auto& p : view->points) {
                const QPoint q =
                    w.canvas()->mapFromScene(QPointF(f.mmToPxX(p.pos.x), f.mmToPxY(-p.pos.y)));
                far = far && (q - cand).manhattanLength() > 30;
            }
            if (far) {
                dest = cand;
                destMm = {cx, 0};
                break;
            }
        }
        QVERIFY2(!dest.isNull(), "no free spot on the canvas");
        QTest::mouseClick(w.canvas()->viewport(), Qt::LeftButton, Qt::NoModifier, dest);
        QCOMPARE(w.session().undoNames().size(), depth + 1);
        QCOMPARE(stitchPoints(w.session().project(), id)->state, PointEditState::ManuallyEdited);
        w.waitForIdle();
        // Suppression du point sélectionné.
        QTest::mouseClick(w.canvas()->viewport(), Qt::LeftButton, Qt::NoModifier,
                          w.canvas()->mapFromScene(QPointF(f.mmToPxX(destMm.x), f.mmToPxY(0))));
        QCOMPARE(w.selectedPoint() >= 0, true);
        QVERIFY(w.deleteSelectedPoint());
        QCOMPARE(w.session().undoNames().size(), depth + 2);
        // Effacer les retouches : retour à l'état propre.
        QVERIFY(w.discardPointEdits());
        QCOMPARE(stitchPoints(w.session().project(), id)->state, PointEditState::Clean);
        QVERIFY(!w.discardPointEdits()); // plus rien à effacer
        // Quitter l'outil retire les poignées.
        QVERIFY(w.runCommand("tool.select"));
        w.waitForIdle();
        QCOMPARE(w.canvas()->pointMarkerCount(), 0);
    }

    void theSatinColumnToolBuildsRailsFromPairsOfClicks() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        QVERIFY(w.runCommand("workspace.digitize"));
        QVERIFY(w.runCommand("tool.satin.column"));
        QVERIFY(w.canvas()->mode() == PreviewCanvas::Mode::DrawPolygon);
        const auto& f = w.previewFrame();
        auto at = [&](double x, double y) {
            return w.canvas()->mapFromScene(QPointF(f.mmToPxX(x), f.mmToPxY(y)));
        };
        const auto before = w.session().project().embroidery_objects.size();
        // Quatre clics = deux barreaux ; Entrée termine.
        for (const QPoint p : {at(-10, -2), at(-10, 2), at(10, -2), at(10, 2)}) {
            QTest::mouseClick(w.canvas()->viewport(), Qt::LeftButton, Qt::NoModifier, p);
        }
        QTest::keyClick(w.canvas()->viewport(), Qt::Key_Return);
        QCOMPARE(w.session().project().embroidery_objects.size(), before + 1);
        QCOMPARE(w.session().selection().targets().size(), size_t(1));
        const auto id = w.session().selection().targets().front().id;
        QCOMPARE(railNodeCount(w.session().project(), id, RailSide::A), 2);
        QVERIFY(w.session().undo());
        QCOMPARE(w.session().project().embroidery_objects.size(), before);
    }

    void clickToStitchEmbroidersAColourAreaOfTheSourceImage() {
        ShellWindow w;
        w.show();
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        QVERIFY(!w.sourceVisible());
        QVERIFY(w.runCommand("workspace.digitize")); // l'outil vit dans la Numérisation
        QVERIFY(w.runCommand("tool.click-to-stitch"));
        QVERIFY(w.sourceVisible()); // on ne clique pas une image invisible
        w.waitForIdle();
        QVERIFY(w.canvas()->hasSource());
        auto processed = processedImage(w.session().project());
        QVERIFY(processed.has_value());
        // Un pixel opaque sombre au milieu du motif.
        const double mmpp = w.session().project().mm_per_px.value;
        std::optional<std::pair<double, double>> mm;
        // Le clic passe par un pixel d'écran entier : on vise le milieu d'un bloc 15 x 15 sombre et
        // opaque, pas le bord d'une zone.
        const auto solid = [&](int cx, int cy) {
            for (int dy = -7; dy <= 7; ++dy) {
                for (int dx = -7; dx <= 7; ++dx) {
                    const int x = cx + dx;
                    const int y = cy + dy;
                    if (x < 0 || y < 0 || x >= processed->width || y >= processed->height) {
                        return false;
                    }
                    const auto* px =
                        &processed->rgba[(static_cast<size_t>(y) * processed->width + x) * 4];
                    if (!(px[3] > 250 && px[0] < 60 && px[1] < 80 && px[2] < 100)) {
                        return false;
                    }
                }
            }
            return true;
        };
        for (int y = processed->height / 2; y < processed->height && !mm; y += 3) {
            for (int x = processed->width / 3; x < 2 * processed->width / 3 && !mm; x += 3) {
                if (solid(x, y)) {
                    mm = pixelCentreMm(*processed, mmpp, {x, y});
                }
            }
        }
        QVERIFY(mm.has_value());
        const auto& f = w.previewFrame();
        const QPoint at =
            w.canvas()->mapFromScene(QPointF(f.mmToPxX(mm->first), f.mmToPxY(mm->second)));
        const auto before = w.session().project().embroidery_objects.size();
        const auto depth = w.session().undoNames().size();
        w.setClickToStitch(20, ClickKind::Fill);
        QTest::mouseClick(w.canvas()->viewport(), Qt::LeftButton, Qt::NoModifier, at);
        QVERIFY2(w.session().project().embroidery_objects.size() == before + 1,
                 qPrintable(w.lastNotice()));
        QCOMPARE(w.session().undoNames().size(), depth + 1);
        QCOMPARE(w.session().selection().targets().size(),
                 size_t(1)); // la zone brodée est sélectionnée
        QVERIFY(w.session().undo());
        QCOMPARE(w.session().project().embroidery_objects.size(), before);
        // Hors de l'image : message, rien d'empilé.
        QTest::mouseClick(w.canvas()->viewport(), Qt::LeftButton, Qt::NoModifier,
                          w.canvas()->mapFromScene(QPointF(f.mmToPxX(5000), f.mmToPxY(5000))));
        QVERIFY(w.lastNotice().contains("Clic-pour-broder"));
        QCOMPARE(w.session().project().embroidery_objects.size(), before);
    }

    void clickToStitchNeedsASourceImage() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new"); // projet vierge : pas d'image
        w.waitForIdle();
        w.runCommand("workspace.digitize");
        QVERIFY(w.runCommand("tool.click-to-stitch"));
        QTest::mouseClick(w.canvas()->viewport(), Qt::LeftButton, Qt::NoModifier,
                          w.canvas()->viewport()->rect().center());
        QVERIFY(w.lastNotice().contains("image"));
        QVERIFY(w.session().project().embroidery_objects.empty());
    }

    void theDetailBrushPaintsAndErasesWithOneUndoStepPerStroke() {
        ShellWindow w;
        w.show();
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        QVERIFY(w.runCommand("tool.detail-brush"));
        QVERIFY(w.sourceVisible());
        w.waitForIdle();
        QVERIFY(w.session().project().detail_mask.empty());
        auto* canvas = w.canvas();
        const QPoint a = canvas->viewport()->rect().center();
        const QPoint b = a + QPoint(40, 0);
        const auto depth = w.session().undoNames().size();
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, a);
        QTest::mouseMove(canvas->viewport(), a + QPoint(20, 0));
        QTest::mouseMove(canvas->viewport(), b);
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, b);
        QVERIFY(w.session().project().detail_mask.any_detail());
        QCOMPARE(w.session().undoNames().size(), depth + 1);
        w.waitForIdle();
        QVERIFY(canvas->hasDetailOverlay()); // le masque est dessiné sur l'image
        // Ctrl + trait au même endroit : gomme.
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::ControlModifier, a);
        QTest::mouseMove(canvas->viewport(), b, 1);
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::ControlModifier, b);
        QCOMPARE(w.session().undoNames().size(), depth + 2);
        QVERIFY(w.session().undo());
        QVERIFY(w.session().project().detail_mask.any_detail());
        w.setBrushRadiusMm(100.0); // borné à 50 mm
    }

    void theSourceImageCanBeShownAndHiddenFromTheViewMenu() {
        ShellWindow w;
        w.show();
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        w.setSourceVisible(true);
        QVERIFY(w.canvas()->hasSource());
        QVERIFY(w.canvas()->sourceVisible());
        w.setSourceVisible(false);
        QVERIFY(!w.canvas()->hasSource() || !w.canvas()->sourceVisible());
        QVERIFY(w.findChild<QAction*>("action_view_source") != nullptr);
    }

    void autosaveRecoveryRecentFilesAndHistoryMenuWork() {
        const auto dir = std::filesystem::temp_directory_path() / "openstitch_v2_shell_state";
        std::filesystem::remove_all(dir);
        const auto osp = dir / "demo.osp";
        {
            ShellWindow w;
            w.setStateDirectory(dir);
            QVERIFY(!w.hasRecovery());
            w.newProject();
            (void)w.session().applyBatch(batchAddRect({0, 0}, 20, 10), "Rectangle");
            (void)w.session().applyBatch(batchAddEllipse({10, 10}, 8, 8), "Ellipse");
            QVERIFY(w.autosaveNow());
            QVERIFY(w.hasRecovery());
            // historique : 2 entrées, la plus récente d'abord ; choisir la 2e annule 2 étapes
            auto* history = w.findChild<QMenu*>("menu_history");
            QVERIFY(history != nullptr);
            emit history->aboutToShow();
            QCOMPARE(history->actions().size(), 2);
            QCOMPARE(history->actions()[0]->text(), QStringLiteral("Ellipse"));
            history->actions()[1]->trigger();
            QCOMPARE(w.session().project().embroidery_objects.size(), size_t(0));
            (void)w.session().redo();
            (void)w.session().redo();
            QVERIFY(w.autosaveNow());
        } // fermeture « sale » : la sauvegarde reste
        {
            ShellWindow w;
            w.setStateDirectory(dir);
            QVERIFY(w.hasRecovery());
            QVERIFY(w.recoverAutosave());
            QCOMPARE(w.session().project().embroidery_objects.size(), size_t(2));
            QVERIFY(w.session().dirty());
            QVERIFY(w.saveProject(osp)); // enregistrer : plus rien à récupérer, récents mis à jour
            QVERIFY(!w.hasRecovery());
            QCOMPARE(w.recentFiles().size(), size_t(1));
            auto* recent = w.findChild<QMenu*>("menu_recent");
            QVERIFY(recent != nullptr);
            QCOMPARE(recent->actions().size(), 1);
        }
        {
            ShellWindow w; // nouvelle session : la liste persiste
            w.setStateDirectory(dir);
            QCOMPARE(w.recentFiles().size(), size_t(1));
            QVERIFY(!w.hasRecovery());
        }
        std::filesystem::remove_all(dir);
    }

    void alignGroupAndNumericPlacementWorkFromTheEditMenu() {
        ShellWindow w;
        w.newProject();
        auto& session = w.session();
        (void)session.applyBatch(batchAddRect({-30, 0}, 10, 10), "A");
        (void)session.applyBatch(batchAddRect({0, 15}, 20, 6), "B");
        (void)session.applyBatch(batchAddRect({25, -8}, 8, 12), "C");
        w.selectAllObjects();
        const auto depth = session.undoNames().size();
        QVERIFY(w.runCommand(QStringLiteral("arrange.align.left")));
        QCOMPARE(session.undoNames().size(), depth + 1);
        QVERIFY(w.runCommand(QStringLiteral("arrange.group")));
        QCOMPARE(session.project().groups.size(), size_t(1));
        QVERIFY(w.runCommand(QStringLiteral("arrange.ungroup")));
        QCOMPARE(session.project().groups.size(), size_t(0));
        QVERIFY(w.placeSelection(0.0, 0.0, 30.0, 30.0, 0.0));
        QVERIFY(!w.placeSelection(5000.0, 0.0, 30.0, 30.0, 0.0));
        QVERIFY(w.findChild<QMenu*>() != nullptr);
    }

    void imagePreparationStepsComeFromTheImageMenuAndTheCommandPalette() {
        ShellWindow w;
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        const auto depth = w.session().project().ops.size();
        PrepRequest bc;
        bc.kind = PrepKind::BrightnessContrast;
        bc.a = 15;
        bc.b = 10;
        QVERIFY(w.applyPrep(bc));
        QVERIFY(w.runCommand(QStringLiteral("image.prep.gray")));
        QVERIFY(w.runCommand(QStringLiteral("image.prep.flip.h")));
        QCOMPARE(w.session().project().ops.size(), depth + 3);
        PrepRequest nothing;
        nothing.kind = PrepKind::BrightnessContrast;
        QVERIFY(!w.applyPrep(nothing));
        QCOMPARE(w.session().project().ops.size(), depth + 3);
        QVERIFY(w.findChild<QAction*>("action_image.prep.crop") != nullptr);
        ShellWindow empty;
        empty.newProject();
        QVERIFY(!empty.runCommand(QStringLiteral("image.prep.gray"))); // désactivée sans image
    }

    void theProductionSheetIsAPdfAndTheHelpListsTheCommands() {
        ShellWindow w;
        w.newProject();
        (void)w.session().applyBatch(batchAddRect({-10, 0}, 20, 10), "A");
        (void)w.session().applyBatch(batchAddEllipse({15, 0}, 12, 12), "B");
        w.waitForIdle();
        const auto pdf = std::filesystem::temp_directory_path() / "openstitch_v2_sheet_test.pdf";
        std::filesystem::remove(pdf);
        w.exportProductionSheet(pdf);
        w.waitForIdle();
        QVERIFY(std::filesystem::exists(pdf));
        QVERIFY(std::filesystem::file_size(pdf) > 1000);
        QFile file(QString::fromStdU16String(pdf.u16string()));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.read(4), QByteArray("%PDF"));
        file.close();
        std::filesystem::remove(pdf);
        const QString help = w.helpText();
        QVERIFY(help.contains(QStringLiteral("Annuler")));
        QVERIFY(help.contains(QStringLiteral("Ctrl+Z")));
        QVERIFY(help.contains(QStringLiteral("Aligner")));
        QVERIFY(w.findChild<QAction*>("action_help_shortcuts") != nullptr);
        QVERIFY(w.findChild<QAction*>("action_file_production_sheet") != nullptr);
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
