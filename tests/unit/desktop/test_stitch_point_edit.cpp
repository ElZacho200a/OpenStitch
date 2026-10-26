// SPDX-License-Identifier: Apache-2.0
// Édition de points dans le bureau (HP-EDIT-001/002/003) : sélection, suppression, insertion,
// saut, coupe, un seul pas d'annulation par geste ; Suppr retire les points, jamais l'objet.
#include <QApplication>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include "main_window.hpp"
#include "node_handle.hpp"

namespace openstitch::desktop {

class MainWindowTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir settingsDir_;

    static document::Project rectangleWithRunningStitch(ObjectId& embroideryId) {
        document::Project project;
        project.original.width = 2;
        project.original.height = 2;
        project.original.rgba.assign(2 * 2 * 4, 255);
        document::VectorObject vec;
        vec.id = project.object_ids.next();
        vec.name = "Rect";
        geometry::Path path;
        path.closed = true;
        const auto node = [](int x, int y) {
            return geometry::PathNode{Vec2um{Micrometers{x * 1000}, Micrometers{y * 1000}},
                                      geometry::NodeType::Corner, std::nullopt, std::nullopt};
        };
        path.nodes = {node(0, 0), node(40, 0), node(40, 20), node(0, 20)};
        vec.paths.push_back(geometry::PathSet{path, {}});
        document::EmbroideryObject emb;
        emb.id = project.object_ids.next();
        emb.name = "Contour";
        emb.source_vector = vec.id;
        emb.params = document::RunningStitchParams{};
        embroideryId = emb.id;
        project.vector_objects.push_back(std::move(vec));
        project.embroidery_objects.push_back(std::move(emb));
        return project;
    }

    static int countHandles(const MainWindow& window) {
        int n = 0;
        for (QGraphicsItem* item : window.scene_->items()) {
            n += dynamic_cast<NodeHandleItem*>(item) != nullptr ? 1 : 0;
        }
        return n;
    }

    // Ouvre le mode « Éditer les points » sur l'objet de broderie du projet.
    static void enterEditMode(MainWindow& window, ObjectId embroideryId) {
        window.setSelection({.region = std::nullopt, .embroidery = embroideryId, .objects = {}});
        window.stitchEditModeAct_->setChecked(true);
    }

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("StitchPointEditTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }

    void clickSelectsAndCtrlClickAddsOrRemoves() {
        MainWindow window;
        window.show();
        ObjectId emb;
        window.applyLoadedProject(rectangleWithRunningStitch(emb));
        enterEditMode(window, emb);
        QVERIFY(window.stitchPointEditActive());
        QVERIFY(countHandles(window) > 4);

        window.clickStitchPoint(2, Qt::NoModifier);
        QCOMPARE(window.stitchSelection_, (std::set<std::size_t>{2}));
        window.clickStitchPoint(5, Qt::ControlModifier);
        QCOMPARE(window.stitchSelection_, (std::set<std::size_t>{2, 5}));
        window.clickStitchPoint(2, Qt::ShiftModifier); // bascule : retire
        QCOMPARE(window.stitchSelection_, (std::set<std::size_t>{5}));
        window.clickStitchPoint(7, Qt::NoModifier); // sans modificateur : remplace
        QCOMPARE(window.stitchSelection_, (std::set<std::size_t>{7}));
    }

    void selectAllThenDeleteIsOneUndoStepAndKeepsTheObject() {
        MainWindow window;
        window.show();
        ObjectId emb;
        window.applyLoadedProject(rectangleWithRunningStitch(emb));
        enterEditMode(window, emb);
        QVERIFY(window.stitchPointEditActive());

        window.selectAllStitchPoints();
        QVERIFY(window.stitchSelection_.size() > 4);
        const std::size_t before = window.stitchSelection_.size();
        const std::size_t objects = window.project_.embroidery_objects.size();

        window.deleteSelection(); // Suppr en mode édition de points
        QCOMPARE(window.project_.embroidery_objects.size(), objects); // l'objet reste
        const auto* object = window.project_.findEmbroidery(emb);
        QVERIFY(object != nullptr);
        std::size_t deleted = 0;
        for (const auto& ov : object->overrides) {
            deleted += ov.deleted ? 1 : 0;
        }
        QCOMPARE(deleted, before);

        window.undo(); // un seul pas pour toute la sélection
        object = window.project_.findEmbroidery(emb);
        QVERIFY(object != nullptr);
        QVERIFY(object->overrides.empty());
    }

    void deleteWithNoPointSelectedKeepsTheLegacyObjectDeletion() {
        MainWindow window;
        window.show();
        ObjectId emb;
        window.applyLoadedProject(rectangleWithRunningStitch(emb));
        enterEditMode(window, emb);
        QVERIFY(window.stitchSelection_.empty());
        window.deleteSelection(); // comportement historique : supprime l'objet, annulable
        QVERIFY(window.project_.findEmbroidery(emb) == nullptr);
        QVERIFY(!window.stitchPointEditActive());
        window.undo();
        QVERIFY(window.project_.findEmbroidery(emb) != nullptr);
    }

    void clickingAHandleDoesNotDestroyIt() {
        MainWindow window;
        window.show();
        ObjectId emb;
        window.applyLoadedProject(rectangleWithRunningStitch(emb));
        enterEditMode(window, emb);
        const int handles = countHandles(window);
        window.clickStitchPoint(2, Qt::NoModifier);
        QCOMPARE(countHandles(window), handles); // restylées en place, pas recréées
        QVERIFY(window.stitchSelection_.count(2) == 1);
    }

    void jumpAndTrimActionsApplyToTheSelection() {
        MainWindow window;
        window.show();
        ObjectId emb;
        window.applyLoadedProject(rectangleWithRunningStitch(emb));
        enterEditMode(window, emb);
        window.clickStitchPoint(3, Qt::NoModifier);
        QVERIFY(window.editSelectedStitchPoints(2)); // saut
        const auto* object = window.project_.findEmbroidery(emb);
        QVERIFY(object != nullptr);
        QCOMPARE(object->overrides.size(), std::size_t{1});
        QVERIFY(object->overrides[0].forced_type.has_value());
        QVERIFY(window.editSelectedStitchPoints(4)); // coupe après
        object = window.project_.findEmbroidery(emb);
        QVERIFY(object->overrides[0].trim_after);
        window.undo();
        window.undo();
        object = window.project_.findEmbroidery(emb);
        QVERIFY(object->overrides.empty());
    }

    void doubleClickNearAStitchLineInsertsAPointAndUndoRemovesIt() {
        MainWindow window;
        window.show();
        ObjectId emb;
        window.applyLoadedProject(rectangleWithRunningStitch(emb));
        enterEditMode(window, emb);
        // Sur le bord bas du rectangle (y = 0 mm), au milieu : un trait de couture y passe.
        QVERIFY(window.insertStitchPointAt(QPointF(10.0, 0.0)));
        const auto* object = window.project_.findEmbroidery(emb);
        QVERIFY(object != nullptr);
        QCOMPARE(object->overrides.size(), std::size_t{1});
        QCOMPARE(object->overrides[0].inserted_after.size(), std::size_t{1});
        window.undo();
        object = window.project_.findEmbroidery(emb);
        QVERIFY(object->overrides.empty());
        // Loin de tout trait : rien n'est inséré.
        QVERIFY(!window.insertStitchPointAt(QPointF(500.0, 500.0)));
        QVERIFY(window.project_.findEmbroidery(emb)->overrides.empty());
    }

    void rubberBandSelectsPointsInsideAndHonoursModes() {
        MainWindow window;
        window.show();
        ObjectId emb;
        window.applyLoadedProject(rectangleWithRunningStitch(emb));
        enterEditMode(window, emb);
        window.selectAllStitchPoints();
        const std::size_t everyPoint = window.stitchSelection_.size();
        // Bande horizontale autour du bord bas (y = 0 mm) : une partie des points seulement.
        const QRectF bottom(-1.0, -1.0, 42.0, 2.0);
        window.selectStitchPointsInRect(bottom, SelectMode::Replace);
        const auto first = window.stitchSelection_;
        QVERIFY(!first.empty());
        QVERIFY(first.size() < everyPoint);
        window.selectStitchPointsInRect(QRectF(500, 500, 1, 1), SelectMode::Add);
        QCOMPARE(window.stitchSelection_, first); // cadre vide : rien ne change
        window.selectStitchPointsInRect(bottom, SelectMode::Toggle);
        QVERIFY(window.stitchSelection_.empty()); // bascule : tout est retire
        window.selectStitchPointsInRect(QRectF(-1, -21, 42, 22), SelectMode::Add);
        QVERIFY(window.stitchSelection_.size() > first.size());
    }

    void insertedPointCanBeMovedAsOneUndoStep() {
        MainWindow window;
        window.show();
        ObjectId emb;
        window.applyLoadedProject(rectangleWithRunningStitch(emb));
        enterEditMode(window, emb);
        QVERIFY(window.insertStitchPointAt(QPointF(10.0, 0.0)));
        const int handlesWithInserted = countHandles(window);
        const auto* object = window.project_.findEmbroidery(emb);
        const std::size_t base = object->overrides[0].base_index;
        window.moveInsertedStitchPoint(
            emb, base, 0, Vec2um{Micrometers{10'000}, Micrometers{3'000}},
            window.stitchEditView_->fingerprint, window.stitchEditView_->point_count);
        object = window.project_.findEmbroidery(emb);
        QCOMPARE(object->overrides[0].inserted_after[0].y.value, 3'000);
        QCOMPARE(countHandles(window), handlesWithInserted);
        window.undo();
        object = window.project_.findEmbroidery(emb);
        QCOMPARE(object->overrides[0].inserted_after[0].y.value, 0);
    }

    void editActionsAreOnlyEnabledInTheModeWithASelection() {
        MainWindow window;
        window.show();
        ObjectId emb;
        window.applyLoadedProject(rectangleWithRunningStitch(emb));
        auto* del = window.findChild<QAction*>(QStringLiteral("action_stitchPointDelete"));
        auto* all = window.findChild<QAction*>(QStringLiteral("action_stitchPointSelectAll"));
        QVERIFY(del != nullptr && all != nullptr);
        QVERIFY(!del->isEnabled());
        QVERIFY(!all->isEnabled());
        enterEditMode(window, emb);
        window.updateActions();
        QVERIFY(all->isEnabled());
        QVERIFY(!del->isEnabled()); // aucune sélection
        window.clickStitchPoint(1, Qt::NoModifier);
        QVERIFY(del->isEnabled());
    }
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_stitch_point_edit.moc"
