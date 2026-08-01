// SPDX-License-Identifier: Apache-2.0
#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QGraphicsItem>
#include <QListWidget>
#include <QMessageBox>
#include <QSettings>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <algorithm>
#include <cmath>

#include "canvas_view.hpp"
#include "document_panel.hpp"
#include "main_window.hpp"
#include "node_handle.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "properties_panel.hpp"
#include "satin_guide_item.hpp"

using openstitch::Micrometers;
using openstitch::ObjectId;
using openstitch::RegionId;
using openstitch::Vec2um;
using openstitch::desktop::CanvasView;
using openstitch::desktop::DocumentPanel;
using openstitch::desktop::MainWindow;
using openstitch::desktop::NodeHandleItem;
using openstitch::desktop::PropertiesPanel;
using openstitch::desktop::SatinGuideItem;
using openstitch::stitch_generation::ObjectEditState;

namespace {

struct Fixture {
    openstitch::document::Project project;
    ObjectId vectorId{};
    ObjectId embroideryId{};
    RegionId regionId{};
};

// Un objet vectoriel triangulaire (1 mm de côté) lié à un objet de broderie
// contour, plus une région de segmentation factice — assez pour exercer
// sélection canevas/liste/inspecteur et undo/redo sans image réelle ni
// dialogue modal. Une image 2x2 minimale est incluse : sans elle,
// refreshImage() s'arrête tôt et ne rafraîchit ni le panneau Document ni la
// séquence de points (cf. MainWindow::refreshImage).
Fixture buildFixture() {
    Fixture fx;

    fx.project.original.width = 2;
    fx.project.original.height = 2;
    fx.project.original.rgba.assign(2 * 2 * 4, 255);

    openstitch::document::VectorObject vec;
    vec.id = fx.project.object_ids.next();
    vec.name = "Triangle";
    openstitch::geometry::Path tri;
    tri.closed = true;
    tri.nodes.push_back(openstitch::geometry::PathNode{
        Vec2um{Micrometers{0}, Micrometers{0}}, openstitch::geometry::NodeType::Corner,
        std::nullopt, std::nullopt});
    tri.nodes.push_back(openstitch::geometry::PathNode{
        Vec2um{Micrometers{1000}, Micrometers{0}}, openstitch::geometry::NodeType::Corner,
        std::nullopt, std::nullopt});
    tri.nodes.push_back(openstitch::geometry::PathNode{
        Vec2um{Micrometers{0}, Micrometers{1000}}, openstitch::geometry::NodeType::Corner,
        std::nullopt, std::nullopt});
    vec.paths.push_back(openstitch::geometry::PathSet{tri, {}});
    fx.vectorId = vec.id;
    fx.project.vector_objects.push_back(vec);

    openstitch::document::EmbroideryObject emb;
    emb.id = fx.project.object_ids.next();
    emb.name = "Triangle - contour";
    emb.source_vector = vec.id;
    emb.params = openstitch::document::RunningStitchParams{};
    fx.embroideryId = emb.id;
    fx.project.embroidery_objects.push_back(emb);

    openstitch::segmentation::Segmentation seg;
    seg.width = 1;
    seg.height = 1;
    seg.labels = {1};
    seg.region_slots.push_back(
        openstitch::segmentation::Region{RegionId{1}, {200, 30, 30}, 1});
    fx.regionId = RegionId{1};
    fx.project.segmentation = std::move(seg);

    return fx;
}

// tabs_/objectsList_/regionsList_ sont privés (comme dans test_document_panel.cpp) :
// on retrouve les listes par leur ordre d'ajout, seul contrat stable observé
// depuis l'extérieur.
QListWidget* objectsList(DocumentPanel& panel) {
    auto* tabs = panel.findChild<QTabWidget*>();
    return qobject_cast<QListWidget*>(tabs->widget(0));
}
QListWidget* regionsList(DocumentPanel& panel) {
    auto* tabs = panel.findChild<QTabWidget*>();
    return qobject_cast<QListWidget*>(tabs->widget(1));
}

// Carré de 10 mm (mêmes dimensions que make_running_square_project dans
// tests/unit/stitch/test_overrides.cpp) : contrairement au triangle de
// buildFixture() (1 mm de côté, sous la longueur de point par défaut), son
// périmètre produit plusieurs points TopStitch déplaçables — nécessaire pour
// exercer le mode d'édition des points (Lot 8.2), qui a besoin d'au moins une
// poignée réelle à glisser.
Fixture buildRunningSquareFixture() {
    Fixture fx;
    fx.project.original.width = 2;
    fx.project.original.height = 2;
    fx.project.original.rgba.assign(2 * 2 * 4, 255);

    openstitch::document::VectorObject vec;
    vec.id = fx.project.object_ids.next();
    vec.name = "Square";
    openstitch::geometry::Path square;
    square.closed = true;
    constexpr std::int32_t s = 10'000;  // 10 mm
    square.nodes = {
        {Vec2um{Micrometers{0}, Micrometers{0}}, openstitch::geometry::NodeType::Corner,
         std::nullopt, std::nullopt},
        {Vec2um{Micrometers{s}, Micrometers{0}}, openstitch::geometry::NodeType::Corner,
         std::nullopt, std::nullopt},
        {Vec2um{Micrometers{s}, Micrometers{s}}, openstitch::geometry::NodeType::Corner,
         std::nullopt, std::nullopt},
        {Vec2um{Micrometers{0}, Micrometers{s}}, openstitch::geometry::NodeType::Corner,
         std::nullopt, std::nullopt},
    };
    vec.paths.push_back(openstitch::geometry::PathSet{square, {}});
    fx.vectorId = vec.id;
    fx.project.vector_objects.push_back(vec);

    openstitch::document::EmbroideryObject emb;
    emb.id = fx.project.object_ids.next();
    emb.name = "Square - contour";
    emb.source_vector = vec.id;
    emb.params = openstitch::document::RunningStitchParams{};
    fx.embroideryId = emb.id;
    fx.project.embroidery_objects.push_back(emb);

    return fx;
}

Fixture buildSatinGuideFixture() {
    Fixture fx;
    fx.project.original.width = 2;
    fx.project.original.height = 2;
    fx.project.original.rgba.assign(2 * 2 * 4, 255);

    openstitch::document::SatinParams satin;
    satin.rail_a.closed = false;
    satin.rail_b.closed = false;
    satin.rail_a.nodes = {{{Micrometers{0}, Micrometers{0}}},
                          {{Micrometers{10'000}, Micrometers{0}}}};
    satin.rail_b.nodes = {{{Micrometers{0}, Micrometers{4'000}}},
                          {{Micrometers{10'000}, Micrometers{4'000}}}};
    satin.rungs = {{{Micrometers{0}, Micrometers{0}},
                    {Micrometers{0}, Micrometers{4'000}}},
                   {{Micrometers{5'000}, Micrometers{0}},
                    {Micrometers{5'000}, Micrometers{4'000}}},
                   {{Micrometers{10'000}, Micrometers{0}},
                    {Micrometers{10'000}, Micrometers{4'000}}}};
    openstitch::document::EmbroideryObject emb;
    emb.id = fx.project.object_ids.next();
    emb.name = "Satin guides";
    emb.params = satin;
    fx.embroideryId = emb.id;
    fx.project.embroidery_objects.push_back(std::move(emb));
    return fx;
}

// Seule poignée trouvée parmi les items de la couche de base : valable quand
// aucun objet vectoriel n'est sélectionné en parallèle (cf. tests ci-dessous,
// qui sélectionnent l'objet de broderie via selectedEmbroidery_ seul, jamais
// selectedObject_) -- sinon les poignées de nœuds vectoriels (même classe
// NodeHandleItem) s'y mêleraient. Prend la liste par valeur (pas MainWindow&) :
// baseItems_ est privé, seul MainWindowTest (friend) peut le lire ; cette
// fonction libre n'a pas besoin d'un accès privilégié une fois la liste en main.
NodeHandleItem* firstHandle(const QList<QGraphicsItem*>& items) {
    for (QGraphicsItem* item : items) {
        if (auto* handle = dynamic_cast<NodeHandleItem*>(item)) {
            return handle;
        }
    }
    return nullptr;
}

// Premier index de `view.raw` réellement déplaçable (entrée TopStitch/Stitch,
// cf. is_movable_point) : utilisé quand une commande d'édition de point est
// construite directement (sans passer par un vrai glisser QTest), pour ne
// jamais cibler par erreur un Jump/point de sous-couche.
std::size_t firstMovableIndex(const openstitch::stitch_generation::ObjectEditView& view) {
    for (std::size_t i = 0; i < view.raw.size(); ++i) {
        if (openstitch::stitch_generation::is_movable_point(view.raw[i])) {
            return i;
        }
    }
    return 0;
}

}  // namespace

namespace openstitch::desktop {

// Couvre des comportements de MainWindow laissés non testés par la fondation
// QTest (commit 0e396ab) : mise à jour des actions/menus selon le contexte,
// synchronisation canevas -> panneau Document -> inspecteur, rafraîchissement
// de l'UI après undo/redo, et non-fuite de sélection entre deux chargements
// de projet. MainWindow est instanciable en test grâce à deux seams
// minimaux : QSettings() par défaut (redirigée ici vers un fichier temporaire,
// jamais le registre réel) et applyLoadedProject() (applique un projet sans
// QFileDialog) — privée en production, accessible ici via `friend class
// MainWindowTest` (déclaré dans main_window.hpp) plutôt que par une méthode
// publique ajoutée uniquement pour les tests.
class MainWindowTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void clickingVectorObjectSyncsDocumentPanelAndInspector();
    void regionAndVectorSelectionToggleContextActionsOppositely();
    void undoRedoRestoresDeletedRegionAndRefreshesDocumentPanel();
    void embroiderySelectionDoesNotLeakAcrossProjectLoadWithReusedId();

    // Lot 8.2 (mode d'édition des points) — revue corrective.
    void stitchEditModeGatingTracksSelectionAndDirtyState();
    void draggingStitchHandleAtDefaultZoomMovesPointOnce();
    void draggingStitchHandleAfterZoomingInMovesPointOnce();
    void clickingStitchHandleWithoutMovingCreatesNoCommand();
    void loadingNewProjectExitsStitchEditModeAndBumpsGeneration();
    void draggingHandleThenLoadingNewProjectDoesNotMutateIt();
    void discardingOverridesOnDirtyObjectIsUndoable();
    void stitchEditModeRefusesWhenTooManyMovablePoints();
    void satinGuideModeMovesEndpointOnRailAndUndoRestoresIt();
    void satinGuideSelectionAddsAndRemovesWithUndoRedo();

private:
    // Active le mode d'édition (sélection directe via selectedEmbroidery_,
    // pas via le signal DocumentPanel::embroiderySelected -- qui sélectionne
    // aussi l'objet vectoriel source et ferait apparaître les poignées de
    // nœuds vectoriels, une classe NodeHandleItem elle aussi, dans la même
    // couche), glisse la première poignée trouvée d'un décalage donné en
    // pixels vue, et attend l'exécution de la commande différée
    // (QTimer::singleShot). Partagée par les deux tests de glisser (deux
    // niveaux de zoom) pour ne pas dupliquer la mécanique QTest.
    void dragFirstStitchHandle(MainWindow& window, ObjectId embroideryId, QPoint delta);

    QTemporaryDir settingsDir_;
};

void MainWindowTest::initTestCase() {
    QVERIFY(settingsDir_.isValid());
    // Isole les QSettings de MainWindow (géométrie/état de fenêtre) du
    // registre réel de l'utilisateur : organisation/application dédiées à ce
    // binaire de test, stockage forcé en fichier INI temporaire.
    QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
    QCoreApplication::setApplicationName(QStringLiteral("MainWindowTest"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    // Le stockage réel utilisé par QSettings() appartient bien au répertoire
    // temporaire (pas au profil utilisateur) : on le prouve en le lisant.
    QSettings probe;
    QVERIFY(probe.fileName().startsWith(settingsDir_.path()));
}

void MainWindowTest::clickingVectorObjectSyncsDocumentPanelAndInspector() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);

    auto* view = window.findChild<CanvasView*>();
    auto* docPanel = window.findChild<DocumentPanel*>();
    auto* propsPanel = window.findChild<PropertiesPanel*>();
    QVERIFY(view != nullptr);
    QVERIFY(docPanel != nullptr);
    QVERIFY(propsPanel != nullptr);

    // Avant sélection : rien à inspecter (pas de spin box de paramètres).
    QCOMPARE(propsPanel->findChildren<QDoubleSpinBox*>().size(), 0);

    // Point intérieur au triangle (0,0)-(1,0)-(0,1) mm ; la scène est en Y
    // vers le bas (ADR-003), d'où le Y négatif.
    view->canvasClickedMm(QPointF(0.25, -0.25));

    // Le clic sélectionne l'objet vectoriel ; MainWindow retrouve le point de
    // contour qui lui est rattaché et synchronise liste + inspecteur dessus.
    QCOMPARE(objectsList(*docPanel)->currentRow(), 0);
    QVERIFY(!propsPanel->findChildren<QDoubleSpinBox*>().isEmpty());

    auto* createStitch = window.findChild<QAction*>(QStringLiteral("action_createStitch"));
    QVERIFY(createStitch != nullptr);
    QVERIFY(createStitch->isEnabled());  // un objet vectoriel est sélectionné
}

void MainWindowTest::regionAndVectorSelectionToggleContextActionsOppositely() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);

    auto* docPanel = window.findChild<DocumentPanel*>();
    auto* view = window.findChild<CanvasView*>();
    auto* deleteRegion = window.findChild<QAction*>(QStringLiteral("action_deleteRegion"));
    auto* createStitch = window.findChild<QAction*>(QStringLiteral("action_createStitch"));
    QVERIFY(docPanel != nullptr);
    QVERIFY(view != nullptr);
    QVERIFY(deleteRegion != nullptr);
    QVERIFY(createStitch != nullptr);

    QVERIFY(!deleteRegion->isEnabled());  // rien de sélectionné au départ
    QVERIFY(!createStitch->isEnabled());

    docPanel->regionSelected(fx.regionId);
    QVERIFY(deleteRegion->isEnabled());
    QVERIFY(!createStitch->isEnabled());  // une région, pas un objet vectoriel

    view->canvasClickedMm(QPointF(0.25, -0.25));  // sélectionne le triangle
    QVERIFY(createStitch->isEnabled());
    QVERIFY(!deleteRegion->isEnabled());  // la sélection au canevas prime (cf. onCanvasClicked)
}

void MainWindowTest::undoRedoRestoresDeletedRegionAndRefreshesDocumentPanel() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);

    auto* docPanel = window.findChild<DocumentPanel*>();
    auto* deleteRegion = window.findChild<QAction*>(QStringLiteral("action_deleteRegion"));
    auto* undoAction = window.findChild<QAction*>(QStringLiteral("action_undo"));
    auto* redoAction = window.findChild<QAction*>(QStringLiteral("action_redo"));
    QVERIFY(docPanel != nullptr);
    QVERIFY(deleteRegion != nullptr);
    QVERIFY(undoAction != nullptr);
    QVERIFY(redoAction != nullptr);

    docPanel->regionSelected(fx.regionId);
    QCOMPARE(regionsList(*docPanel)->count(), 1);
    QVERIFY(!undoAction->isEnabled());

    deleteRegion->trigger();
    QCOMPARE(regionsList(*docPanel)->count(), 0);  // la région a disparu de la liste
    QVERIFY(undoAction->isEnabled());
    QVERIFY(!redoAction->isEnabled());

    undoAction->trigger();
    // L'UI reflète la restauration du modèle, pas seulement la pile undo.
    QCOMPARE(regionsList(*docPanel)->count(), 1);
    QVERIFY(!undoAction->isEnabled());
    QVERIFY(redoAction->isEnabled());

    redoAction->trigger();
    QCOMPARE(regionsList(*docPanel)->count(), 0);
}

// Régression : applyLoadedProject() n'oubliait de réinitialiser que
// selectedEmbroidery_ (selectedRegion_ et selectedObject_ l'étaient déjà).
// Deux projets construits par buildFixture() partent chacun d'un
// document::Project{} par défaut, donc allouent les mêmes ObjectId — le cas
// piège où un ID de broderie est recyclé entre deux documents distincts.
void MainWindowTest::embroiderySelectionDoesNotLeakAcrossProjectLoadWithReusedId() {
    MainWindow window;
    const Fixture fx1 = buildFixture();
    window.applyLoadedProject(fx1.project);

    auto* docPanel = window.findChild<DocumentPanel*>();
    auto* propsPanel = window.findChild<PropertiesPanel*>();
    QVERIFY(docPanel != nullptr);
    QVERIFY(propsPanel != nullptr);

    docPanel->embroiderySelected(fx1.embroideryId);
    QCOMPARE(objectsList(*docPanel)->currentRow(), 0);
    QVERIFY(!propsPanel->findChildren<QDoubleSpinBox*>().isEmpty());  // inspecteur montre la broderie

    const Fixture fx2 = buildFixture();
    QCOMPARE(fx2.embroideryId.value, fx1.embroideryId.value);  // même ID recyclé, autre document
    window.applyLoadedProject(fx2.project);

    // Rien n'a été sélectionné explicitement dans le nouveau projet : ni le
    // panneau Document ni l'inspecteur ne doivent refléter la broderie
    // choisie dans le projet précédent.
    QCOMPARE(objectsList(*docPanel)->currentRow(), -1);
    QVERIFY(propsPanel->findChildren<QDoubleSpinBox*>().isEmpty());
}

// ---------------------------------------------------------------------------
// Lot 8.2 : mode d'édition des points générés (revue corrective)
// ---------------------------------------------------------------------------

void MainWindowTest::stitchEditModeGatingTracksSelectionAndDirtyState() {
    MainWindow window;
    const Fixture fx = buildRunningSquareFixture();
    window.applyLoadedProject(fx.project);

    auto* editAct = window.findChild<QAction*>(QStringLiteral("action_stitchEditMode"));
    QVERIFY(editAct != nullptr);
    QSignalSpy toggledSpy(editAct, &QAction::toggled);

    QVERIFY(!editAct->isEnabled());  // rien de sélectionné au départ
    QVERIFY(!editAct->isChecked());

    window.selectedEmbroidery_ = fx.embroideryId;
    window.updateActions();
    QVERIFY(editAct->isEnabled());

    editAct->setChecked(true);
    QCOMPARE(toggledSpy.count(), 1);
    QVERIFY(editAct->isChecked());
    QVERIFY(window.stitchEditTarget_.has_value());
    QCOMPARE(window.stitchEditTarget_->value, fx.embroideryId.value);
    QVERIFY(window.stitchEditView_.has_value());

    // Perte de sélection : sortie propre (updateActions) via QSignalBlocker
    // côté production -- aucun second toggled() émis, seulement des lectures.
    window.selectedEmbroidery_.reset();
    window.updateActions();
    QCOMPARE(toggledSpy.count(), 1);
    QVERIFY(!editAct->isChecked());
    QVERIFY(!editAct->isEnabled());
    QVERIFY(!window.stitchEditTarget_.has_value());
    QVERIFY(!window.stitchEditView_.has_value());

    // Réactiver, retoucher un point, puis rendre l'objet Dirty par un
    // changement de paramètres (sans toucher à la sélection) : le mode doit
    // ressortir de lui-même.
    window.selectedEmbroidery_ = fx.embroideryId;
    window.updateActions();
    editAct->setChecked(true);
    QVERIFY(editAct->isChecked());
    QVERIFY(window.stitchEditView_.has_value());

    const auto baseIndex = firstMovableIndex(*window.stitchEditView_);
    window.undoStack_.execute(
        std::make_unique<openstitch::commands::MoveStitchPointCommand>(
            fx.embroideryId, baseIndex, Vec2um{Micrometers{1'234}, Micrometers{5'678}},
            window.stitchEditView_->fingerprint, window.stitchEditView_->point_count),
        window.project_);
    window.refreshImage();
    window.updateActions();
    QCOMPARE(window.editStateOf(fx.embroideryId), ObjectEditState::ManuallyEdited);
    QVERIFY(editAct->isChecked());  // ManuallyEdited reste éditable

    window.undoStack_.execute(
        std::make_unique<openstitch::commands::SetStitchParamsCommand>(
            fx.embroideryId, openstitch::document::RunningStitchParams{Micrometers{500}}),
        window.project_);
    window.refreshImage();
    window.updateActions();

    QCOMPARE(window.editStateOf(fx.embroideryId), ObjectEditState::Dirty);
    QVERIFY(!editAct->isChecked());   // sorti proprement, sans action utilisateur
    QVERIFY(!editAct->isEnabled());   // reste désactivé tant que Dirty
    QVERIFY(!window.stitchEditTarget_.has_value());
}

void MainWindowTest::satinGuideModeMovesEndpointOnRailAndUndoRestoresIt() {
    MainWindow window;
    const Fixture fx = buildSatinGuideFixture();
    window.applyLoadedProject(fx.project);
    window.resize(900, 700);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto* action = window.findChild<QAction*>(QStringLiteral("action_satinGuideMode"));
    QVERIFY(action != nullptr);
    QVERIFY(!action->isEnabled());
    window.selectedEmbroidery_ = fx.embroideryId;
    window.updateActions();
    QVERIFY(action->isEnabled());
    action->setChecked(true);
    QVERIFY(action->isChecked());
    QCOMPARE(window.satinGuideTarget_->value, fx.embroideryId.value);

    QList<NodeHandleItem*> handles;
    for (QGraphicsItem* item : window.baseItems_) {
        if (auto* handle = dynamic_cast<NodeHandleItem*>(item)) {
            handles.push_back(handle);
        }
    }
    QCOMPARE(handles.size(), 6);  // deux extrémités pour chacun des trois guides
    // Cadre de vue déterministe : la minuscule image factice ferait sinon
    // arrondir 1 mm à 0 px selon la géométrie de fenêtre restaurée.
    window.view_->resetTransform();
    window.view_->scale(40.0, 40.0);
    window.view_->centerOn(QPointF(5.0, -2.0));
    auto it = std::find_if(handles.begin(), handles.end(), [](const NodeHandleItem* handle) {
        return std::abs(handle->scenePos().x() - 5.0) < 0.01 &&
               std::abs(handle->scenePos().y()) < 0.01;
    });
    QVERIFY(it != handles.end());
    auto* first = *it;  // extrémité A du guide central, loin des bords de vue
    const QPoint start = window.view_->mapFromScene(first->scenePos());
    const int availableRight = window.view_->viewport()->width() - 1 - start.x();
    const int availableLeft = start.x();
    const int direction = availableRight >= availableLeft ? 1 : -1;
    const int movement = std::min(40, std::max(availableRight, availableLeft));
    const QPoint end = start + QPoint(movement * direction, 0);
    const QPoint middle = (start + end) / 2;
    QVERIFY(window.view_->viewport()->rect().contains(start));
    QVERIFY(window.view_->viewport()->rect().contains(end));
    QVERIFY2((end - start).manhattanLength() >= QApplication::startDragDistance(),
             qPrintable(QStringLiteral("déplacement écran insuffisant: %1 px")
                            .arg((end - start).manhattanLength())));
    QCOMPARE(dynamic_cast<NodeHandleItem*>(window.view_->itemAt(start)), first);
    QTest::mousePress(window.view_->viewport(), Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(window.view_->viewport(), middle);
    QTest::mouseMove(window.view_->viewport(), end);
    QTest::mouseRelease(window.view_->viewport(), Qt::LeftButton, Qt::NoModifier, end);

    QTRY_VERIFY_WITH_TIMEOUT(window.undoStack_.canUndo(), 1000);
    const auto& moved = std::get<openstitch::document::SatinParams>(
        window.project_.findEmbroidery(fx.embroideryId)->params);
    QVERIFY(moved.rungs[1].a.x.value != 5'000);
    QCOMPARE(moved.rungs[1].a.y.value, 0);
    QCOMPARE(QString::fromStdString(window.undoStack_.undoName()),
             QStringLiteral("Déplacer un guide satin"));

    window.undo();
    const auto& restored = std::get<openstitch::document::SatinParams>(
        window.project_.findEmbroidery(fx.embroideryId)->params);
    QCOMPARE(restored.rungs[1].a.x.value, 5'000);
    QCOMPARE(restored.rungs[1].a.y.value, 0);
    action->setChecked(false);
    QVERIFY(!window.satinGuideTarget_.has_value());
    QCoreApplication::processEvents();
}

void MainWindowTest::satinGuideSelectionAddsAndRemovesWithUndoRedo() {
    MainWindow window;
    const Fixture fx = buildSatinGuideFixture();
    window.applyLoadedProject(fx.project);
    window.resize(900, 700);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto* mode = window.findChild<QAction*>(QStringLiteral("action_satinGuideMode"));
    auto* add = window.findChild<QAction*>(QStringLiteral("action_addSatinGuide"));
    auto* remove = window.findChild<QAction*>(QStringLiteral("action_removeSatinGuide"));
    QVERIFY(mode != nullptr);
    QVERIFY(add != nullptr);
    QVERIFY(remove != nullptr);

    window.selectedEmbroidery_ = fx.embroideryId;
    window.updateActions();
    mode->setChecked(true);
    QVERIFY(mode->isChecked());
    QVERIFY(add->isEnabled());
    QVERIFY(!remove->isEnabled());

    // Sélectionne le guide central par le même hit-test que l'utilisateur. Le
    // repère de vue explicite évite toute dépendance à la résolution ou aux
    // QSettings de géométrie de fenêtre.
    window.view_->resetTransform();
    window.view_->scale(40.0, 40.0);
    window.view_->centerOn(QPointF(5.0, -2.0));
    SatinGuideItem* middleGuide = nullptr;
    for (QGraphicsItem* item : window.baseItems_) {
        auto* guide = dynamic_cast<SatinGuideItem*>(item);
        if (guide != nullptr && std::abs(guide->line().p1().x() - 5.0) < 0.01) {
            middleGuide = guide;
            break;
        }
    }
    QVERIFY(middleGuide != nullptr);
    const QPoint guidePoint = window.view_->mapFromScene(middleGuide->line().center());
    QVERIFY(window.view_->viewport()->rect().contains(guidePoint));
    QCOMPARE(dynamic_cast<SatinGuideItem*>(window.view_->itemAt(guidePoint)), middleGuide);
    QTest::mouseClick(window.view_->viewport(), Qt::LeftButton, Qt::NoModifier, guidePoint);
    QTRY_COMPARE(window.selectedSatinGuide_, std::optional<std::size_t>{1});
    QVERIFY(remove->isEnabled());

    remove->trigger();
    const auto* afterRemove = window.project_.findEmbroidery(fx.embroideryId);
    QVERIFY(afterRemove != nullptr);
    QCOMPARE(std::get<openstitch::document::SatinParams>(afterRemove->params).rungs.size(),
             std::size_t{2});
    QCOMPARE(QString::fromStdString(window.undoStack_.undoName()),
             QStringLiteral("Supprimer un guide satin"));
    QVERIFY(!remove->isEnabled());

    window.undo();
    const auto* afterRemoveUndo = window.project_.findEmbroidery(fx.embroideryId);
    QCOMPARE(std::get<openstitch::document::SatinParams>(afterRemoveUndo->params).rungs,
             std::get<openstitch::document::SatinParams>(
                 fx.project.findEmbroidery(fx.embroideryId)->params)
                 .rungs);

    add->trigger();
    const auto* afterAdd = window.project_.findEmbroidery(fx.embroideryId);
    const auto& added = std::get<openstitch::document::SatinParams>(afterAdd->params);
    QCOMPARE(added.rungs.size(), std::size_t{4});
    QCOMPARE(added.rungs[1].a.x.value, 2'500);
    QCOMPARE(added.rungs[1].b.x.value, 2'500);
    QTRY_COMPARE(window.selectedSatinGuide_, std::optional<std::size_t>{1});
    QCOMPARE(QString::fromStdString(window.undoStack_.undoName()),
             QStringLiteral("Ajouter un guide satin"));
    QVERIFY(remove->isEnabled());

    window.undo();
    const auto* afterAddUndo = window.project_.findEmbroidery(fx.embroideryId);
    QCOMPARE(std::get<openstitch::document::SatinParams>(afterAddUndo->params).rungs.size(),
             std::size_t{3});
    window.redo();
    const auto* afterAddRedo = window.project_.findEmbroidery(fx.embroideryId);
    QCOMPARE(std::get<openstitch::document::SatinParams>(afterAddRedo->params).rungs.size(),
             std::size_t{4});
    mode->setChecked(false);
    QCoreApplication::processEvents();
}

void MainWindowTest::dragFirstStitchHandle(MainWindow& window, ObjectId embroideryId, QPoint delta) {
    window.selectedEmbroidery_ = embroideryId;
    window.updateActions();

    auto* editAct = window.findChild<QAction*>(QStringLiteral("action_stitchEditMode"));
    QVERIFY(editAct != nullptr);
    QVERIFY(editAct->isEnabled());
    editAct->setChecked(true);
    QVERIFY(editAct->isChecked());
    QVERIFY(window.stitchEditView_.has_value());

    auto* handle = firstHandle(window.baseItems_);
    QVERIFY(handle != nullptr);

    const QPoint startVp = window.view_->mapFromScene(handle->pos());
    const QPoint endVp = startVp + delta;

    QVERIFY(!window.undoStack_.canUndo());
    QTest::mousePress(window.view_->viewport(), Qt::LeftButton, Qt::NoModifier, startVp);
    QTest::mouseMove(window.view_->viewport(), endVp);
    QTest::mouseRelease(window.view_->viewport(), Qt::LeftButton, Qt::NoModifier, endVp);

    // La commande est différée (QTimer::singleShot(0), cf. renderBase) pour ne
    // pas détruire la poignée pendant son propre événement souris.
    QTRY_VERIFY(window.undoStack_.canUndo());
    QCOMPARE(window.undoStack_.undoName(), std::string("Déplacement de point"));

    const auto* obj = window.project_.findEmbroidery(embroideryId);
    QVERIFY(obj != nullptr);
    QCOMPARE(obj->overrides.size(), std::size_t(1));  // une seule commande pour tout le glisser
    QVERIFY(obj->overrides[0].moved_to.has_value());
    const auto droppedOverride = obj->overrides[0];  // copie : comparée après undo/redo

    auto* docPanel = window.findChild<DocumentPanel*>();
    QVERIFY(docPanel != nullptr);
    QVERIFY(objectsList(*docPanel)->item(0)->toolTip().contains(QStringLiteral("Retouché")));

    // Undo/redo exact : annuler retire la retouche entièrement (aucun résidu
    // partiel), rétablir restaure très exactement la même entrée.
    QVERIFY(window.undoStack_.undo(window.project_));
    window.refreshImage();
    window.updateActions();
    const auto* objAfterUndo = window.project_.findEmbroidery(embroideryId);
    QVERIFY(objAfterUndo != nullptr);
    QVERIFY(objAfterUndo->overrides.empty());
    QVERIFY(!window.undoStack_.canUndo());  // une seule commande existait
    QVERIFY(window.undoStack_.canRedo());
    QCOMPARE(window.editStateOf(embroideryId), ObjectEditState::Clean);

    QVERIFY(window.undoStack_.redo(window.project_));
    window.refreshImage();
    window.updateActions();
    const auto* objAfterRedo = window.project_.findEmbroidery(embroideryId);
    QVERIFY(objAfterRedo != nullptr);
    QCOMPARE(objAfterRedo->overrides.size(), std::size_t(1));
    QCOMPARE(objAfterRedo->overrides[0].base_index, droppedOverride.base_index);
    QVERIFY(objAfterRedo->overrides[0].moved_to.has_value());
    QVERIFY(*objAfterRedo->overrides[0].moved_to == *droppedOverride.moved_to);  // exact, pas approx
    QCOMPARE(window.editStateOf(embroideryId), ObjectEditState::ManuallyEdited);
}

void MainWindowTest::draggingStitchHandleAtDefaultZoomMovesPointOnce() {
    MainWindow window;
    const Fixture fx = buildRunningSquareFixture();
    window.applyLoadedProject(fx.project);
    window.view_->fitCanvas();
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    dragFirstStitchHandle(window, fx.embroideryId, QPoint(20, 15));
    QVERIFY(!QTest::currentTestFailed());
}

void MainWindowTest::draggingStitchHandleAfterZoomingInMovesPointOnce() {
    MainWindow window;
    const Fixture fx = buildRunningSquareFixture();
    window.applyLoadedProject(fx.project);
    window.view_->fitCanvas();
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    // Second niveau de zoom : la conversion écran <-> mm change d'échelle,
    // seule façon de distinguer un bug de placement/mapping d'un hasard au
    // niveau de zoom par défaut.
    window.view_->zoomIn();
    window.view_->zoomIn();

    dragFirstStitchHandle(window, fx.embroideryId, QPoint(20, 15));
    QVERIFY(!QTest::currentTestFailed());
}

void MainWindowTest::clickingStitchHandleWithoutMovingCreatesNoCommand() {
    MainWindow window;
    const Fixture fx = buildRunningSquareFixture();
    window.applyLoadedProject(fx.project);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.selectedEmbroidery_ = fx.embroideryId;
    window.updateActions();
    auto* editAct = window.findChild<QAction*>(QStringLiteral("action_stitchEditMode"));
    QVERIFY(editAct != nullptr);
    editAct->setChecked(true);
    QVERIFY(window.stitchEditView_.has_value());

    auto* handle = firstHandle(window.baseItems_);
    QVERIFY(handle != nullptr);
    const QPoint clickVp = window.view_->mapFromScene(handle->pos());

    QVERIFY(!window.undoStack_.canUndo());
    QTest::mouseClick(window.view_->viewport(), Qt::LeftButton, Qt::NoModifier, clickVp);
    // Rien à attendre : le callback de relâchement retourne avant même de
    // programmer un QTimer quand la position n'a pas changé (cf. renderBase,
    // garde `newPos == pos`) -- aucune commande différée en vol ici.
    QTest::qWait(20);
    QVERIFY(!window.undoStack_.canUndo());
}

void MainWindowTest::loadingNewProjectExitsStitchEditModeAndBumpsGeneration() {
    MainWindow window;
    const Fixture fx = buildRunningSquareFixture();
    window.applyLoadedProject(fx.project);
    const auto generationAfterFirstLoad = window.documentGeneration_;

    window.selectedEmbroidery_ = fx.embroideryId;
    window.updateActions();
    auto* editAct = window.findChild<QAction*>(QStringLiteral("action_stitchEditMode"));
    QVERIFY(editAct != nullptr);
    editAct->setChecked(true);
    QVERIFY(editAct->isChecked());
    QVERIFY(window.stitchEditTarget_.has_value());

    const Fixture fx2 = buildRunningSquareFixture();
    window.applyLoadedProject(fx2.project);

    QVERIFY(window.documentGeneration_ != generationAfterFirstLoad);
    QVERIFY(!editAct->isChecked());
    QVERIFY(!window.stitchEditTarget_.has_value());
    QVERIFY(!window.stitchEditView_.has_value());
}

// Régression (revue corrective Lot 8.2, point 2) : la commande de glisser est
// différée d'un cycle d'événements (QTimer::singleShot(0)) pour ne pas
// détruire sa propre poignée. Si un nouveau projet est chargé dans cette
// fenêtre pendant cette fenêtre de temps, la commande différée doit être
// abandonnée -- jamais exécutée sur, ni pire mutant, un projet qui n'est plus
// celui pour lequel elle a été construite.
void MainWindowTest::draggingHandleThenLoadingNewProjectDoesNotMutateIt() {
    MainWindow window;
    const Fixture fx1 = buildRunningSquareFixture();
    window.applyLoadedProject(fx1.project);
    window.view_->fitCanvas();
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.selectedEmbroidery_ = fx1.embroideryId;
    window.updateActions();
    auto* editAct = window.findChild<QAction*>(QStringLiteral("action_stitchEditMode"));
    QVERIFY(editAct != nullptr);
    editAct->setChecked(true);
    QVERIFY(window.stitchEditView_.has_value());

    auto* handle = firstHandle(window.baseItems_);
    QVERIFY(handle != nullptr);
    const QPoint startVp = window.view_->mapFromScene(handle->pos());
    const QPoint endVp = startVp + QPoint(20, 15);

    QTest::mousePress(window.view_->viewport(), Qt::LeftButton, Qt::NoModifier, startVp);
    QTest::mouseMove(window.view_->viewport(), endVp);
    QTest::mouseRelease(window.view_->viewport(), Qt::LeftButton, Qt::NoModifier, endVp);
    // À cet instant, la commande différée est en file d'attente mais ne s'est
    // pas encore exécutée : on simule ici un changement de projet, avant
    // qu'elle ait pu tourner.
    const Fixture fx2 = buildRunningSquareFixture();
    QCOMPARE(fx2.embroideryId.value, fx1.embroideryId.value);  // même id recyclé, autre document
    window.applyLoadedProject(fx2.project);

    QTest::qWait(50);  // laisse le QTimer(0) en file se déclencher s'il le peut

    const auto* obj = window.project_.findEmbroidery(fx2.embroideryId);
    QVERIFY(obj != nullptr);
    QVERIFY(obj->overrides.empty());        // la commande différée a été abandonnée
    QVERIFY(!window.undoStack_.canUndo());  // aucune commande fantôme empilée
}

void MainWindowTest::discardingOverridesOnDirtyObjectIsUndoable() {
    MainWindow window;
    const Fixture fx = buildRunningSquareFixture();
    window.applyLoadedProject(fx.project);

    window.selectedEmbroidery_ = fx.embroideryId;
    window.updateActions();
    auto* editAct = window.findChild<QAction*>(QStringLiteral("action_stitchEditMode"));
    QVERIFY(editAct != nullptr);
    editAct->setChecked(true);
    QVERIFY(window.stitchEditView_.has_value());

    const auto baseIndex = firstMovableIndex(*window.stitchEditView_);
    const Vec2um droppedPos{Micrometers{1'234}, Micrometers{5'678}};
    window.undoStack_.execute(
        std::make_unique<openstitch::commands::MoveStitchPointCommand>(
            fx.embroideryId, baseIndex, droppedPos, window.stitchEditView_->fingerprint,
            window.stitchEditView_->point_count),
        window.project_);
    window.refreshImage();
    window.updateActions();
    QCOMPARE(window.editStateOf(fx.embroideryId), ObjectEditState::ManuallyEdited);

    // Changement de paramètres : invalide l'empreinte stockée -> Dirty.
    window.undoStack_.execute(
        std::make_unique<openstitch::commands::SetStitchParamsCommand>(
            fx.embroideryId, openstitch::document::RunningStitchParams{Micrometers{500}}),
        window.project_);
    window.refreshImage();
    window.updateActions();
    QCOMPARE(window.editStateOf(fx.embroideryId), ObjectEditState::Dirty);

    // « Abandonner les retouches » demande confirmation (QMessageBox modale) :
    // on l'auto-accepte, comme le ferait un utilisateur cliquant Oui --
    // programmé avant l'appel, puisque exec() pompe la boucle d'événements en
    // interne.
    QTimer::singleShot(0, &window, [] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            box->button(QMessageBox::Yes)->click();
        }
    });
    window.discardOverrides(fx.embroideryId);

    const auto* objAfterDiscard = window.project_.findEmbroidery(fx.embroideryId);
    QVERIFY(objAfterDiscard != nullptr);
    QVERIFY(objAfterDiscard->overrides.empty());
    QCOMPARE(window.editStateOf(fx.embroideryId), ObjectEditState::Clean);
    QVERIFY(window.undoStack_.canUndo());
    QCOMPARE(window.undoStack_.undoName(), std::string("Abandonner les retouches"));

    // Annuler l'abandon restaure exactement les retouches et l'état Dirty
    // d'avant (aucune perte : c'est le contrat de DiscardOverridesCommand).
    QVERIFY(window.undoStack_.undo(window.project_));
    window.refreshImage();
    window.updateActions();
    const auto* objAfterUndo = window.project_.findEmbroidery(fx.embroideryId);
    QVERIFY(objAfterUndo != nullptr);
    QCOMPARE(objAfterUndo->overrides.size(), std::size_t(1));
    QCOMPARE(objAfterUndo->overrides[0].base_index, baseIndex);
    QVERIFY(objAfterUndo->overrides[0].moved_to.has_value());
    QVERIFY(*objAfterUndo->overrides[0].moved_to == droppedPos);
    QCOMPARE(window.editStateOf(fx.embroideryId), ObjectEditState::Dirty);
}

// Régression (revue corrective Lot 8.2, point 3) : le seuil doit porter sur
// les points réellement déplaçables, pas sur la taille totale de la vue
// brute -- ce test l'exerce avec un objet bien au-delà de la limite.
void MainWindowTest::stitchEditModeRefusesWhenTooManyMovablePoints() {
    MainWindow window;
    Fixture fx = buildRunningSquareFixture();
    // Densité extrême (10 µm/point, fusion désactivée) sur un périmètre de
    // 40 mm : plusieurs milliers de points, bien au-delà de
    // kMaxEditableStitchHandles (2000).
    fx.project.embroidery_objects[0].params =
        openstitch::document::RunningStitchParams{Micrometers{10}, Micrometers{1}};
    window.applyLoadedProject(fx.project);

    window.selectedEmbroidery_ = fx.embroideryId;
    window.updateActions();
    auto* editAct = window.findChild<QAction*>(QStringLiteral("action_stitchEditMode"));
    QVERIFY(editAct != nullptr);
    QVERIFY(editAct->isEnabled());  // Clean, sélectionné : activable a priori

    editAct->setChecked(true);

    // Refusé avec explication (barre de statut), jamais laissé actif sans
    // aucune poignée -- cf. updateActions, bloc stitchEditTooManyPoints.
    QVERIFY(!editAct->isChecked());
    QVERIFY(!window.stitchEditTarget_.has_value());
    QVERIFY(!window.stitchEditView_.has_value());
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("2000")));
    QVERIFY(firstHandle(window.baseItems_) == nullptr);  // aucune poignée affichée
}

}  // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_main_window.moc"
