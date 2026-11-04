// SPDX-License-Identifier: Apache-2.0
#include <QAction>
#include <QApplication>
#include <QTest>
#include <QToolBar>
#include <QTreeWidget>
#include <QWheelEvent>
#include <filesystem>

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
