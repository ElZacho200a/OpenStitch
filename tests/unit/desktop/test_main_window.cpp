// SPDX-License-Identifier: Apache-2.0
#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QGraphicsItem>
#include <QListWidget>
#include <QMessageBox>
#include <QSettings>
#include <QShortcut>
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
#include "workflow_panel.hpp"
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
    ObjectId embroideryId2{};
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

Fixture buildSatinGuideFixture(bool withStartJunction = false) {
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
    if (withStartJunction) {
        satin.topology = openstitch::document::SatinSectionTopology{
            0, 3, std::uint32_t{7}, std::nullopt};
    }
    openstitch::document::EmbroideryObject emb;
    emb.id = fx.project.object_ids.next();
    emb.name = "Satin guides";
    emb.params = satin;
    fx.embroideryId = emb.id;
    fx.project.embroidery_objects.push_back(std::move(emb));
    return fx;
}

Fixture buildSatinJunctionFixture() {
    Fixture fx = buildSatinGuideFixture(true);
    constexpr ObjectId source{77};
    auto& first = fx.project.embroidery_objects.front();
    first.source_vector = source;
    auto& firstSatin = std::get<openstitch::document::SatinParams>(first.params);
    firstSatin.rungs[1].a.x = Micrometers{2'000};
    firstSatin.rungs[1].b.x = Micrometers{2'000};
    firstSatin.topology = openstitch::document::SatinSectionTopology{
        0, 2, std::uint32_t{7}, std::nullopt};

    auto second = first;
    second.id = fx.project.object_ids.next();
    second.name = "Satin guides - branche 2";
    auto& secondSatin = std::get<openstitch::document::SatinParams>(second.params);
    secondSatin.topology = openstitch::document::SatinSectionTopology{
        1, 2, std::uint32_t{7}, std::nullopt};
    fx.embroideryId2 = second.id;
    fx.project.embroidery_objects.push_back(std::move(second));
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
    void satinJunctionGuideIsLockedInUi();
    void satinJunctionAddsInternalGuidesToEveryBranchAtomically();

    void satinLinkedGuideGroupMoveShiftDragUpdatesBothSectionsAtomically();
    void satinLinkedGuideShiftClickCreatesNoUndoCommand();
    void satinLinkedGuideGroupRemoveDeletesBothSectionsAtomically();
    void satinLinkedGuideEndpointDragWithoutShiftStaysLocal();

    // Création manuelle de formes (mission « auto-satin béton », suite : le
    // pipeline ne savait créer un VectorObject que depuis une image importée).
    void drawRectangleToolCreatesUndoableVectorObject();
    // Contrairement au test ci-dessus (qui injecte boxDrawnMm directement,
    // court-circuitant tout le mécanisme de glisser réel), celui-ci simule
    // un VRAI glisser souris sur la vue réelle de MainWindow, outil choisi
    // via le bouton de la palette -- le chemin complet emprunté en usage
    // réel, jamais exercé par les tests existants (retour utilisateur :
    // « rectangle/ellipse ne fonctionne toujours pas au clic-glisser »).
    void drawRectangleToolWithRealMouseDragOnMainWindowCreatesObject();
    void drawEllipseToolCreatesUndoableVectorObject();
    void drawEllipseWithShiftConstrainsToCircle();
    void drawingTooSmallABoxCreatesNoObject();
    void drawPolygonAccumulatesVerticesAndClosesOnDoubleClick();
    void drawPolygonWithFewerThanThreeVerticesCancelsOnDoubleClick();
    void switchingToolDuringPolygonDrawCancelsIt();
    void polygonDoubleClickDoesNotAddADuplicateVertex();
    void drawFreeformCreatesUndoableVectorObject();
    void drawFreeformWithTooFewPointsCancelsOnRelease();
    void switchingToolDuringFreeformDrawCancelsIt();

    // « Créer une colonne satin… » (createSatinObject) : brancher sur le
    // moteur squelette (auto_satin::build_satin_columns, mode Parametric)
    // plutôt que sur l'heuristique naïve rails_from_contour (audit satin
    // demandé par l'utilisateur, § docs/source/satin.md).
    void createSatinObjectOnSuitableRectangleProducesOneSatinWithStitches();

    // Panneau Workflow (audit ergonomie) : les étapes « Régions »/« Vecteurs »
    // ne doivent jamais rester « à faire » quand des objets vectoriels
    // existent déjà via un chemin qui ne remplit jamais project_.segmentation
    // (Segmenter avec l'IA -> autodigitize::auto_digitize directement).
    void workflowRegionsAndVectorsStepsReflectVectorObjectsWithoutClassicSegmentation();

    // Glisser le CORPS d'une forme sélectionnée la déplace tout entière
    // (défaut remonté en usage réel : « la manipulation des vecteurs est
    // pénible » -- seule l'édition nœud par nœud existait). VRAI glisser
    // souris sur la vue réelle (pas une injection de signal), comme
    // drawRectangleToolWithRealMouseDragOnMainWindowCreatesObject ci-dessus,
    // pour la même raison : un défaut de dragMode ne se voit qu'ainsi.
    void draggingSelectedShapeBodyWithRealMouseTranslatesWholeObject();

    // Colonne satin manuelle (outil DrawSatinColumn) : création par paires
    // alternées, rejet propre d'un point orphelin, annulation par changement
    // d'outil, remodelage d'un nœud de rail.
    void manualSatinColumnCreatesRailsAndRungsFromAlternatingPairs();
    void manualSatinColumnDropsOrphanPointOnOddCountAtFinish();
    void switchingToolDuringSatinColumnDrawCancelsIt();
    void satinRailEditModeDragsNodeAndUndoRestoresIt();

    // Réponses à l'audit UI (boutons "ne faisant rien", pas de courbes de
    // Bézier, menu contextuel pauvre, mode satin confus) : outil Bézier réel
    // (clic + clic-glisser), bouton/touche Entrée pour terminer un tracé,
    // suppression/duplication depuis le menu contextuel, mode satin unifié.
    void bezierToolClickCreatesCornerNodes();
    void bezierToolDragCreatesSmoothNodeWithSymmetricHandles();
    void finishDrawActionAndEnterKeyBothClosePolygon();
    void deleteVectorObjectRemovesShapeAndDependentEmbroidery();
    void duplicateVectorObjectOffsetsCopyAndIsUndoable();
    void satinEditModeTogglesBothUnderlyingModes();

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

    // Sélectionne la jonction structurelle du guide de section 0, déclenche
    // l'ajout coordonné (comme satinJunctionAddsInternalGuidesToEveryBranchAtomically),
    // et laisse le guide lié fraîchement créé (index 1, link_id 0) sélectionné.
    // Point de départ partagé par les tests de groupe lié ci-dessous.
    void selectJunctionAndAddLinkedGuides(MainWindow& window, const Fixture& fx);

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

void MainWindowTest::satinJunctionGuideIsLockedInUi() {
    MainWindow window;
    const Fixture fx = buildSatinGuideFixture(true);
    window.applyLoadedProject(fx.project);
    window.resize(900, 700);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto* mode = window.findChild<QAction*>(QStringLiteral("action_satinGuideMode"));
    auto* remove = window.findChild<QAction*>(QStringLiteral("action_removeSatinGuide"));
    QVERIFY(mode != nullptr);
    QVERIFY(remove != nullptr);
    window.selectedEmbroidery_ = fx.embroideryId;
    window.updateActions();
    mode->setChecked(true);

    int handleCount = 0;
    SatinGuideItem* junctionGuide = nullptr;
    for (QGraphicsItem* item : window.baseItems_) {
        if (dynamic_cast<NodeHandleItem*>(item) != nullptr) {
            ++handleCount;
        }
        auto* guide = dynamic_cast<SatinGuideItem*>(item);
        if (guide != nullptr && std::abs(guide->line().p1().x()) < 0.01) {
            junctionGuide = guide;
        }
    }
    QCOMPARE(handleCount, 4);  // aucun handle sur le guide structurel de départ
    QVERIFY(junctionGuide != nullptr);

    window.view_->resetTransform();
    window.view_->scale(40.0, 40.0);
    window.scene_->setSceneRect(QRectF(-5.0, -10.0, 20.0, 20.0));
    window.view_->centerOn(junctionGuide->line().center());
    const QPoint guidePoint = window.view_->mapFromScene(junctionGuide->line().center());
    QVERIFY(window.view_->viewport()->rect().contains(guidePoint));
    QCOMPARE(dynamic_cast<SatinGuideItem*>(window.view_->itemAt(guidePoint)), junctionGuide);
    QTest::mouseClick(window.view_->viewport(), Qt::LeftButton, Qt::NoModifier, guidePoint);
    QTRY_COMPARE(window.selectedSatinGuide_, std::optional<std::size_t>{0});
    QVERIFY(!remove->isEnabled());

    const auto before = std::get<openstitch::document::SatinParams>(
                            window.project_.findEmbroidery(fx.embroideryId)->params)
                            .rungs;
    remove->trigger();
    const auto after = std::get<openstitch::document::SatinParams>(
                           window.project_.findEmbroidery(fx.embroideryId)->params)
                           .rungs;
    QCOMPARE(after, before);
    QVERIFY(!window.undoStack_.canUndo());
    mode->setChecked(false);
    QCoreApplication::processEvents();
}

void MainWindowTest::satinJunctionAddsInternalGuidesToEveryBranchAtomically() {
    MainWindow window;
    const Fixture fx = buildSatinJunctionFixture();
    window.applyLoadedProject(fx.project);
    window.resize(900, 700);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto* mode = window.findChild<QAction*>(QStringLiteral("action_satinGuideMode"));
    auto* add = window.findChild<QAction*>(QStringLiteral("action_addSatinGuide"));
    QVERIFY(mode != nullptr);
    QVERIFY(add != nullptr);
    window.selectedEmbroidery_ = fx.embroideryId;
    window.updateActions();
    mode->setChecked(true);

    SatinGuideItem* junctionGuide = nullptr;
    for (QGraphicsItem* item : window.baseItems_) {
        auto* guide = dynamic_cast<SatinGuideItem*>(item);
        if (guide != nullptr && std::abs(guide->line().p1().x()) < 0.01) {
            junctionGuide = guide;
            break;
        }
    }
    QVERIFY(junctionGuide != nullptr);
    window.view_->resetTransform();
    window.view_->scale(40.0, 40.0);
    window.scene_->setSceneRect(QRectF(-5.0, -10.0, 20.0, 20.0));
    window.view_->centerOn(junctionGuide->line().center());
    const QPoint guidePoint = window.view_->mapFromScene(junctionGuide->line().center());
    QVERIFY(window.view_->viewport()->rect().contains(guidePoint));
    QTest::mouseClick(window.view_->viewport(), Qt::LeftButton, Qt::NoModifier, guidePoint);
    QTRY_COMPARE(window.selectedSatinGuide_, std::optional<std::size_t>{0});

    add->trigger();
    const auto rungCount = [&](ObjectId id) {
        return std::get<openstitch::document::SatinParams>(
                   window.project_.findEmbroidery(id)->params)
            .rungs.size();
    };
    QCOMPARE(rungCount(fx.embroideryId), std::size_t{4});
    QCOMPARE(rungCount(fx.embroideryId2), std::size_t{4});
    const auto guideAt = [&](ObjectId id, std::size_t index) {
        return std::get<openstitch::document::SatinParams>(
                   window.project_.findEmbroidery(id)->params)
            .rungs[index];
    };
    QCOMPARE(guideAt(fx.embroideryId, 1).a.x, Micrometers{1'000});
    QCOMPARE(guideAt(fx.embroideryId2, 1).a.x, Micrometers{1'000});
    QCOMPARE(guideAt(fx.embroideryId, 1).link_id, std::optional<std::uint32_t>{0});
    QCOMPARE(guideAt(fx.embroideryId2, 1).link_id, std::optional<std::uint32_t>{0});
    QTRY_COMPARE(window.selectedSatinGuide_, std::optional<std::size_t>{1});
    QCOMPARE(QString::fromStdString(window.undoStack_.undoName()),
             QStringLiteral("Ajouter des guides satin coordonnés"));
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("2 guides")));

    window.undo();
    QCOMPARE(rungCount(fx.embroideryId), std::size_t{3});
    QCOMPARE(rungCount(fx.embroideryId2), std::size_t{3});
    window.redo();
    QCOMPARE(rungCount(fx.embroideryId), std::size_t{4});
    QCOMPARE(rungCount(fx.embroideryId2), std::size_t{4});
    mode->setChecked(false);
    QCoreApplication::processEvents();
}

void MainWindowTest::selectJunctionAndAddLinkedGuides(MainWindow& window, const Fixture& fx) {
    auto* mode = window.findChild<QAction*>(QStringLiteral("action_satinGuideMode"));
    auto* add = window.findChild<QAction*>(QStringLiteral("action_addSatinGuide"));
    QVERIFY(mode != nullptr);
    QVERIFY(add != nullptr);
    window.selectedEmbroidery_ = fx.embroideryId;
    window.updateActions();
    mode->setChecked(true);

    window.view_->resetTransform();
    window.view_->scale(40.0, 40.0);
    window.scene_->setSceneRect(QRectF(-5.0, -10.0, 20.0, 20.0));

    SatinGuideItem* junctionGuide = nullptr;
    for (QGraphicsItem* item : window.baseItems_) {
        auto* guide = dynamic_cast<SatinGuideItem*>(item);
        if (guide != nullptr && std::abs(guide->line().p1().x()) < 0.01) {
            junctionGuide = guide;
            break;
        }
    }
    QVERIFY(junctionGuide != nullptr);
    window.view_->centerOn(junctionGuide->line().center());
    const QPoint guidePoint = window.view_->mapFromScene(junctionGuide->line().center());
    QVERIFY(window.view_->viewport()->rect().contains(guidePoint));
    QTest::mouseClick(window.view_->viewport(), Qt::LeftButton, Qt::NoModifier, guidePoint);
    QTRY_COMPARE(window.selectedSatinGuide_, std::optional<std::size_t>{0});

    add->trigger();
    QTRY_COMPARE(window.selectedSatinGuide_, std::optional<std::size_t>{1});
}

void MainWindowTest::satinLinkedGuideGroupMoveShiftDragUpdatesBothSectionsAtomically() {
    MainWindow window;
    const Fixture fx = buildSatinJunctionFixture();
    window.applyLoadedProject(fx.project);
    window.resize(900, 700);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    selectJunctionAndAddLinkedGuides(window, fx);

    const auto rungAt = [&](ObjectId id, std::size_t index) {
        return std::get<openstitch::document::SatinParams>(
                   window.project_.findEmbroidery(id)->params)
            .rungs[index];
    };
    QCOMPARE(rungAt(fx.embroideryId, 1).link_id, std::optional<std::uint32_t>{0});
    QCOMPARE(rungAt(fx.embroideryId2, 1).link_id, std::optional<std::uint32_t>{0});

    window.view_->centerOn(QPointF(1.0, -2.0));
    QList<NodeHandleItem*> handles;
    for (QGraphicsItem* item : window.baseItems_) {
        if (auto* handle = dynamic_cast<NodeHandleItem*>(item)) {
            handles.push_back(handle);
        }
    }
    auto it = std::find_if(handles.begin(), handles.end(), [](const NodeHandleItem* handle) {
        return std::abs(handle->scenePos().x() - 1.0) < 0.01 &&
               std::abs(handle->scenePos().y()) < 0.01;
    });
    QVERIFY(it != handles.end());
    auto* handle = *it; // extrémité rail A du guide lié (index 1)

    const QPoint start = window.view_->mapFromScene(handle->scenePos());
    const int availableRight = window.view_->viewport()->width() - 1 - start.x();
    const int availableLeft = start.x();
    const int direction = availableRight >= availableLeft ? 1 : -1;
    const int movement = std::min(24, std::max(availableRight, availableLeft));
    const QPoint end = start + QPoint(movement * direction, 0);
    const QPoint middle = (start + end) / 2;
    QVERIFY(window.view_->viewport()->rect().contains(start));
    QVERIFY(window.view_->viewport()->rect().contains(end));
    QVERIFY2((end - start).manhattanLength() >= QApplication::startDragDistance(),
             qPrintable(QStringLiteral("déplacement écran insuffisant: %1 px")
                            .arg((end - start).manhattanLength())));
    QCOMPARE(dynamic_cast<NodeHandleItem*>(window.view_->itemAt(start)), handle);

    // Maj+glisser : geste explicite du groupe lié (documenté dans le statut du
    // mode et l'infobulle du guide) — un glisser SANS Maj resterait local.
    QTest::mousePress(window.view_->viewport(), Qt::LeftButton, Qt::ShiftModifier, start);
    QTest::mouseMove(window.view_->viewport(), middle);
    QTest::mouseMove(window.view_->viewport(), end);
    QTest::mouseRelease(window.view_->viewport(), Qt::LeftButton, Qt::ShiftModifier, end);

    // canUndo() est déjà vrai (commande "Ajouter" du setup partagé) : on
    // attend le NOM de la commande, pas juste canUndo(), pour laisser le
    // QTimer::singleShot du glisser différé s'exécuter.
    QTRY_COMPARE(QString::fromStdString(window.undoStack_.undoName()),
                 QStringLiteral("Déplacer des guides satin coordonnés"));
    const auto movedX = rungAt(fx.embroideryId, 1).a.x;
    QVERIFY(movedX.value != 1'000); // a bougé
    // Même géométrie de section -> même delta normalisé -> même résultat exact
    // dans les deux sections (groupe déplacé de façon cohérente, pas juste la
    // section glissée).
    QCOMPARE(rungAt(fx.embroideryId2, 1).a.x, movedX);
    QCOMPARE(rungAt(fx.embroideryId, 1).a.y, Micrometers{0});
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("2 guides")));

    window.undo();
    QCOMPARE(rungAt(fx.embroideryId, 1).a.x, Micrometers{1'000});
    QCOMPARE(rungAt(fx.embroideryId2, 1).a.x, Micrometers{1'000});
    window.redo();
    QCOMPARE(rungAt(fx.embroideryId, 1).a.x, movedX);
    QCOMPARE(rungAt(fx.embroideryId2, 1).a.x, movedX);

    auto* mode = window.findChild<QAction*>(QStringLiteral("action_satinGuideMode"));
    mode->setChecked(false);
    QCoreApplication::processEvents();
}

void MainWindowTest::satinLinkedGuideShiftClickCreatesNoUndoCommand() {
    MainWindow window;
    const Fixture fx = buildSatinJunctionFixture();
    window.applyLoadedProject(fx.project);
    window.resize(900, 700);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    selectJunctionAndAddLinkedGuides(window, fx);
    const auto undoNameBefore = window.undoStack_.undoName();
    window.view_->centerOn(QPointF(1.0, -2.0));
    NodeHandleItem* handle = nullptr;
    for (QGraphicsItem* item : window.baseItems_) {
        auto* candidate = dynamic_cast<NodeHandleItem*>(item);
        if (candidate != nullptr && std::abs(candidate->scenePos().x() - 1.0) < 0.01 &&
            std::abs(candidate->scenePos().y()) < 0.01) {
            handle = candidate;
            break;
        }
    }
    QVERIFY(handle != nullptr);
    const QPoint point = window.view_->mapFromScene(handle->scenePos());
    QTest::mouseClick(window.view_->viewport(), Qt::LeftButton, Qt::ShiftModifier, point);
    QCoreApplication::processEvents();

    QCOMPARE(window.undoStack_.undoName(), undoNameBefore);
    QCOMPARE(std::get<openstitch::document::SatinParams>(
                 window.project_.findEmbroidery(fx.embroideryId)->params)
                 .rungs[1]
                 .a.x,
             Micrometers{1'000});

    auto* mode = window.findChild<QAction*>(QStringLiteral("action_satinGuideMode"));
    mode->setChecked(false);
    QCoreApplication::processEvents();
}

void MainWindowTest::satinLinkedGuideGroupRemoveDeletesBothSectionsAtomically() {
    MainWindow window;
    const Fixture fx = buildSatinJunctionFixture();
    window.applyLoadedProject(fx.project);
    window.resize(900, 700);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    selectJunctionAndAddLinkedGuides(window, fx);
    auto* remove = window.findChild<QAction*>(QStringLiteral("action_removeSatinGuide"));
    QVERIFY(remove != nullptr);
    QVERIFY(remove->isEnabled());

    const auto rungCount = [&](ObjectId id) {
        return std::get<openstitch::document::SatinParams>(
                   window.project_.findEmbroidery(id)->params)
            .rungs.size();
    };
    QCOMPARE(rungCount(fx.embroideryId), std::size_t{4});
    QCOMPARE(rungCount(fx.embroideryId2), std::size_t{4});

    remove->trigger();
    QCOMPARE(rungCount(fx.embroideryId), std::size_t{3});
    QCOMPARE(rungCount(fx.embroideryId2), std::size_t{3});
    QCOMPARE(QString::fromStdString(window.undoStack_.undoName()),
             QStringLiteral("Supprimer des guides satin coordonnés"));
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("2 guides")));
    QVERIFY(!window.selectedSatinGuide_.has_value());

    window.undo();
    QCOMPARE(rungCount(fx.embroideryId), std::size_t{4});
    QCOMPARE(rungCount(fx.embroideryId2), std::size_t{4});
    const auto guideAt = [&](ObjectId id, std::size_t index) {
        return std::get<openstitch::document::SatinParams>(
                   window.project_.findEmbroidery(id)->params)
            .rungs[index];
    };
    QCOMPARE(guideAt(fx.embroideryId, 1).link_id, std::optional<std::uint32_t>{0});
    QCOMPARE(guideAt(fx.embroideryId2, 1).link_id, std::optional<std::uint32_t>{0});

    window.redo();
    QCOMPARE(rungCount(fx.embroideryId), std::size_t{3});
    QCOMPARE(rungCount(fx.embroideryId2), std::size_t{3});

    auto* mode = window.findChild<QAction*>(QStringLiteral("action_satinGuideMode"));
    mode->setChecked(false);
    QCoreApplication::processEvents();
}

// Régression : un guide lié glissé SANS Maj reste un geste local (édition
// d'angle propre à cette section) — seule la section 0 doit bouger, la
// section 1 doit rester intacte. Couvre la non-régression du comportement
// pré-existant (guides non liés / édition locale) une fois le geste de groupe
// introduit.
void MainWindowTest::satinLinkedGuideEndpointDragWithoutShiftStaysLocal() {
    MainWindow window;
    const Fixture fx = buildSatinJunctionFixture();
    window.applyLoadedProject(fx.project);
    window.resize(900, 700);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    selectJunctionAndAddLinkedGuides(window, fx);
    const auto rungAt = [&](ObjectId id, std::size_t index) {
        return std::get<openstitch::document::SatinParams>(
                   window.project_.findEmbroidery(id)->params)
            .rungs[index];
    };

    window.view_->centerOn(QPointF(1.0, -2.0));
    QList<NodeHandleItem*> handles;
    for (QGraphicsItem* item : window.baseItems_) {
        if (auto* handle = dynamic_cast<NodeHandleItem*>(item)) {
            handles.push_back(handle);
        }
    }
    auto it = std::find_if(handles.begin(), handles.end(), [](const NodeHandleItem* handle) {
        return std::abs(handle->scenePos().x() - 1.0) < 0.01 &&
               std::abs(handle->scenePos().y()) < 0.01;
    });
    QVERIFY(it != handles.end());
    auto* handle = *it;

    const QPoint start = window.view_->mapFromScene(handle->scenePos());
    const int availableRight = window.view_->viewport()->width() - 1 - start.x();
    const int availableLeft = start.x();
    const int direction = availableRight >= availableLeft ? 1 : -1;
    const int movement = std::min(24, std::max(availableRight, availableLeft));
    const QPoint end = start + QPoint(movement * direction, 0);
    QVERIFY2((end - start).manhattanLength() >= QApplication::startDragDistance(),
             qPrintable(QStringLiteral("déplacement écran insuffisant: %1 px")
                            .arg((end - start).manhattanLength())));

    QTest::mousePress(window.view_->viewport(), Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(window.view_->viewport(), (start + end) / 2);
    QTest::mouseMove(window.view_->viewport(), end);
    QTest::mouseRelease(window.view_->viewport(), Qt::LeftButton, Qt::NoModifier, end);

    // canUndo() est déjà vrai (commande "Ajouter" du setup partagé) : on
    // attend le NOM de la commande pour laisser le glisser différé s'exécuter.
    QTRY_COMPARE(QString::fromStdString(window.undoStack_.undoName()),
                 QStringLiteral("Déplacer un guide satin"));
    QVERIFY(rungAt(fx.embroideryId, 1).a.x.value != 1'000);
    QCOMPARE(rungAt(fx.embroideryId2, 1).a.x, Micrometers{1'000}); // section 1 intacte

    auto* mode = window.findChild<QAction*>(QStringLiteral("action_satinGuideMode"));
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

// --- Création manuelle de formes (mission « auto-satin béton », suite) ------

void MainWindowTest::drawRectangleToolCreatesUndoableVectorObject() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);

    const std::size_t before = window.project_.vector_objects.size();
    window.setTool(Tool::DrawRectangle);
    // Scène Y vers le bas (ADR-003) : cadre 5 x 3 mm.
    view->boxDrawnMm(QRectF(10.0, -5.0, 5.0, 3.0), Qt::NoModifier);

    QCOMPARE(window.project_.vector_objects.size(), before + 1);
    const auto& obj = window.project_.vector_objects.back();
    QCOMPARE(obj.paths.size(), std::size_t{1});
    QCOMPARE(obj.paths[0].outer.nodes.size(), std::size_t{4});
    QVERIFY(obj.paths[0].outer.closed);
    for (const auto& n : obj.paths[0].outer.nodes) {
        QVERIFY(n.type == openstitch::geometry::NodeType::Corner);
    }
    QVERIFY(window.selectedObject_.has_value());
    QCOMPARE(*window.selectedObject_, obj.id);

    // Aire exacte : 5 x 3 mm = 15 000 000 µm².
    QCOMPARE(std::abs(openstitch::geometry::signed_area_um2(obj.paths[0].outer)), 15'000'000.0);

    // Annulable : undo retire l'objet, redo le restaure.
    window.undo();
    QCOMPARE(window.project_.vector_objects.size(), before);
    window.redo();
    QCOMPARE(window.project_.vector_objects.size(), before + 1);
}

void MainWindowTest::createSatinObjectOnSuitableRectangleProducesOneSatinWithStitches() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);

    // Rectangle allongé 40 x 5 mm : Suitable côté satinabilité (même forme
    // que la fixture "rectangle" de libs/auto_satin), une seule colonne
    // attendue -- ni décomposition en branches, ni refus.
    window.setTool(Tool::DrawRectangle);
    view->boxDrawnMm(QRectF(0.0, 0.0, 40.0, 5.0), Qt::NoModifier);
    QVERIFY(window.selectedObject_.has_value());
    const ObjectId vectorId = *window.selectedObject_;

    const std::size_t embroideryCountBefore = window.project_.embroidery_objects.size();

    // createSatinObject() ouvre une QDialog modale (densité/compensation/
    // sous-couche) : programmé avant l'appel, comme les autres tests de ce
    // fichier qui pilotent une boîte modale (cf. discardOverrides ci-dessus)
    // -- exec() pompe la boucle d'événements en interne.
    QTimer::singleShot(0, &window, [] {
        if (auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            dlg->accept();
        }
    });
    window.createSatinObject();

    QCOMPARE(window.project_.embroidery_objects.size(), embroideryCountBefore + 1);
    const auto& emb = window.project_.embroidery_objects.back();
    QCOMPARE(emb.source_vector, vectorId);
    QVERIFY(emb.is_satin());
    const auto& satin = std::get<openstitch::document::SatinParams>(emb.params);
    QVERIFY(satin.rail_a.nodes.size() >= 2);
    QVERIFY(satin.rail_b.nodes.size() >= 2);
    // Le moteur squelette pose des barreaux par défaut (correspondance
    // ladder) : sans eux, la génération retomberait sur fill_satin seul.
    QVERIFY(!satin.rungs.empty());

    window.refreshImage();
    QVERIFY(window.sequence_.has_value());
    const auto stats = openstitch::stitch::compute_stats(*window.sequence_);
    QVERIFY(stats.stitches > 0);

    // Annulable en un seul geste (AddObjectBatchCommand), comme les autres
    // créations de forme de ce fichier.
    QVERIFY(window.undoStack_.canUndo());
    window.undo();
    QCOMPARE(window.project_.embroidery_objects.size(), embroideryCountBefore);
    window.redo();
    QCOMPARE(window.project_.embroidery_objects.size(), embroideryCountBefore + 1);
}

void MainWindowTest::workflowRegionsAndVectorsStepsReflectVectorObjectsWithoutClassicSegmentation() {
    MainWindow window;
    Fixture fx = buildFixture();
    // Simule le chemin « Segmenter avec l'IA » : un objet vectoriel existe
    // (fx.project.vector_objects contient déjà "Triangle"), mais
    // project_.segmentation n'a JAMAIS été rempli -- exactement l'état
    // produit par AiSegmentationDialog + autodigitize::auto_digitize, qui ne
    // passe jamais par la segmentation classique par pixels.
    fx.project.segmentation.reset();
    window.applyLoadedProject(fx.project);
    window.updateActions();

    auto* workflow = window.findChild<WorkflowPanel*>();
    QVERIFY(workflow != nullptr);
    QCOMPARE(workflow->currentState(1), WorkflowPanel::State::Done);  // Régions
    QCOMPARE(workflow->currentState(2), WorkflowPanel::State::Done);  // Vecteurs
}

void MainWindowTest::draggingSelectedShapeBodyWithRealMouseTranslatesWholeObject() {
    MainWindow window;
    Fixture fx = buildFixture();
    // Carré 10 x 10 mm centré sur l'origine (le triangle de buildFixture,
    // 1 mm de côté, est trop petit pour cliquer sûrement en son centre sans
    // frôler un nœud) : nœuds à -5/-5, 5/-5, 5/5, -5/5 mm.
    document::VectorObject square;
    square.id = fx.project.object_ids.next();
    square.name = "Carre";
    geometry::Path outer;
    outer.closed = true;
    outer.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{-5'000}, Micrometers{-5'000}},
                                             geometry::NodeType::Corner, std::nullopt, std::nullopt});
    outer.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{5'000}, Micrometers{-5'000}},
                                             geometry::NodeType::Corner, std::nullopt, std::nullopt});
    outer.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{5'000}, Micrometers{5'000}},
                                             geometry::NodeType::Corner, std::nullopt, std::nullopt});
    outer.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{-5'000}, Micrometers{5'000}},
                                             geometry::NodeType::Corner, std::nullopt, std::nullopt});
    square.paths.push_back(geometry::PathSet{outer, {}});
    const ObjectId squareId = square.id;
    fx.project.vector_objects.push_back(square);

    window.applyLoadedProject(fx.project);
    window.selectedObject_ = squareId;  // sélectionné -> corps glissable (VectorObjectBodyItem)
    window.setTool(Tool::Select);
    window.refreshImage();

    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);
    window.resize(1600, 1000);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    view->resetTransform();
    view->scale(10.0, 10.0);
    view->centerOn(QPointF(0.0, 0.0));

    // Centre du carré (loin des 4 nœuds, ~7 mm) -> déplacement de 3 mm en
    // diagonale, largement au-delà de startDragDistance à ce zoom.
    const QPoint from = view->mapFromScene(QPointF(0.0, 0.0));
    const QPoint mid = view->mapFromScene(QPointF(1.5, -1.5));
    const QPoint to = view->mapFromScene(QPointF(3.0, -3.0));
    const QRect vpRect = view->viewport()->rect();
    QVERIFY2(vpRect.contains(from), qPrintable(QStringLiteral("from hors du viewport")));
    QVERIFY2(vpRect.contains(to), qPrintable(QStringLiteral("to hors du viewport")));
    QVERIFY2((to - from).manhattanLength() >= QApplication::startDragDistance(),
             qPrintable(QStringLiteral("déplacement écran insuffisant: %1 px")
                            .arg((to - from).manhattanLength())));

    QCOMPARE(window.project_.findObject(squareId)->paths[0].outer.nodes[0].pos,
            (Vec2um{Micrometers{-5'000}, Micrometers{-5'000}}));

    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(view->viewport(), mid);
    QTest::mouseMove(view->viewport(), to);
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, to);

    // Le glisser est validé de façon différée (QTimer::singleShot(0, ...)) :
    // laisse la boucle d'événements le traiter, comme pour les poignées de
    // nœud (cf. commentaire dans renderBase/VectorObjectBodyItem). Delta
    // scène (+3, -3) mm -> delta modèle (+3, +3) mm (sceneMmToModel : x
    // inchangé, y inversé -- donc -3 devient +3), d'où -5+3=-2 sur les deux
    // axes du premier nœud.
    QTRY_COMPARE_WITH_TIMEOUT(window.project_.findObject(squareId)->paths[0].outer.nodes[0].pos,
                              (Vec2um{Micrometers{-2'000}, Micrometers{-2'000}}), 2000);
    const auto* moved = window.project_.findObject(squareId);
    QVERIFY(moved != nullptr);
    QCOMPARE(moved->paths[0].outer.nodes[1].pos, (Vec2um{Micrometers{8'000}, Micrometers{-2'000}}));
    QCOMPARE(moved->paths[0].outer.nodes[2].pos, (Vec2um{Micrometers{8'000}, Micrometers{8'000}}));
    QCOMPARE(moved->paths[0].outer.nodes[3].pos, (Vec2um{Micrometers{-2'000}, Micrometers{8'000}}));

    QVERIFY(window.undoStack_.canUndo());
    window.undo();
    QCOMPARE(window.project_.findObject(squareId)->paths[0].outer.nodes[0].pos,
            (Vec2um{Micrometers{-5'000}, Micrometers{-5'000}}));
    window.redo();
    QCOMPARE(window.project_.findObject(squareId)->paths[0].outer.nodes[0].pos,
            (Vec2um{Micrometers{-2'000}, Micrometers{-2'000}}));
}

void MainWindowTest::drawRectangleToolWithRealMouseDragOnMainWindowCreatesObject() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);
    // Fenêtre large : à 900x700 (taille utilisée ailleurs dans ce fichier),
    // les docks (Document/Propriétés/Ordre/Filtres/Workflow) compriment le
    // viewport du canevas à ~68 px de large -- fenêtre réaliste ici pour ne
    // pas confondre un artefact de taille de fenêtre avec un vrai bug.
    window.resize(1600, 1000);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    // Outil choisi via le VRAI bouton de la palette (pas window.setTool()
    // appelé directement) : si le déclic du bouton lui-même était en cause,
    // ce test le révélerait alors que les autres (setTool direct) non.
    QVERIFY(window.toolDrawRectAct_ != nullptr);
    window.toolDrawRectAct_->trigger();
    QCOMPARE(window.currentTool_, Tool::DrawRectangle);
    QVERIFY(view->boxDrawMode());

    view->resetTransform();
    view->scale(10.0, 10.0);
    view->centerOn(QPointF(0.0, 0.0));

    const std::size_t before = window.project_.vector_objects.size();
    const QPoint from = view->mapFromScene(QPointF(-5.0, -5.0));
    const QPoint mid = view->mapFromScene(QPointF(0.0, 0.0));
    const QPoint to = view->mapFromScene(QPointF(10.0, 8.0));
    QVERIFY2((to - from).manhattanLength() >= QApplication::startDragDistance(),
             qPrintable(QStringLiteral("déplacement écran insuffisant: %1 px")
                            .arg((to - from).manhattanLength())));
    const QRect vpRect = view->viewport()->rect();
    QVERIFY2(vpRect.contains(from),
             qPrintable(QStringLiteral("from=(%1,%2) hors du viewport (%3,%4,%5,%6)")
                            .arg(from.x())
                            .arg(from.y())
                            .arg(vpRect.x())
                            .arg(vpRect.y())
                            .arg(vpRect.width())
                            .arg(vpRect.height())));
    QVERIFY2(vpRect.contains(to),
             qPrintable(QStringLiteral("to=(%1,%2) hors du viewport (%3,%4,%5,%6)")
                            .arg(to.x())
                            .arg(to.y())
                            .arg(vpRect.x())
                            .arg(vpRect.y())
                            .arg(vpRect.width())
                            .arg(vpRect.height())));

    QSignalSpy boxSpy(view, &CanvasView::boxDrawnMm);
    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(view->viewport(), mid);
    QTest::mouseMove(view->viewport(), to);
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, to);

    QVERIFY2(boxSpy.count() >= 1,
             qPrintable(QStringLiteral("boxDrawnMm jamais emis (count=%1) -- bug dans "
                                       "CanvasView, pas dans MainWindow::onBoxDrawn")
                            .arg(boxSpy.count())));
    QCOMPARE(window.project_.vector_objects.size(), before + 1);
    const auto& obj = window.project_.vector_objects.back();
    QCOMPARE(obj.paths[0].outer.nodes.size(), std::size_t{4});
    QVERIFY(obj.paths[0].outer.closed);
}

void MainWindowTest::drawEllipseToolCreatesUndoableVectorObject() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);

    const std::size_t before = window.project_.vector_objects.size();
    window.setTool(Tool::DrawEllipse);
    view->boxDrawnMm(QRectF(0.0, -10.0, 20.0, 10.0), Qt::NoModifier);  // 20 x 10 mm -> rx=10, ry=5 mm

    QCOMPARE(window.project_.vector_objects.size(), before + 1);
    const auto& obj = window.project_.vector_objects.back();
    QCOMPARE(obj.paths[0].outer.nodes.size(), std::size_t{4});
    for (const auto& n : obj.paths[0].outer.nodes) {
        QVERIFY(n.type == openstitch::geometry::NodeType::Smooth);
        QVERIFY(n.tan_in.has_value());
        QVERIFY(n.tan_out.has_value());
    }
}

void MainWindowTest::drawEllipseWithShiftConstrainsToCircle() {
    // Défaut trouvé par revue : le test « Maj = cercle » avait dû être écarté
    // à la création de cette fonctionnalité (QTest::keyPress sur une fenêtre
    // non affichée ne met pas à jour QGuiApplication::keyboardModifiers() de
    // façon fiable en offscreen). Corrigé en capturant les modificateurs
    // directement sur l'évènement de relâchement qui termine le glisser
    // (CanvasView::mouseReleaseEvent), transmis par `boxDrawnMm` — plus de
    // dépendance à l'état clavier global, donc testable directement en
    // passant Qt::ShiftModifier.
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);

    window.setTool(Tool::DrawEllipse);
    // Cadre non carré (20 x 6 mm) : sans Maj -> ellipse ; avec Maj -> cercle
    // (côté = le plus petit des deux, 6 mm).
    view->boxDrawnMm(QRectF(0.0, -10.0, 20.0, 6.0), Qt::ShiftModifier);

    const auto& obj = window.project_.vector_objects.back();
    const auto& nodes = obj.paths[0].outer.nodes;
    QCOMPARE(nodes.size(), std::size_t{4});
    // Les 4 sommets (est/nord/ouest/sud) doivent être équidistants du centre :
    // un cercle, pas une ellipse.
    Vec2um sum{Micrometers{0}, Micrometers{0}};
    for (const auto& n : nodes) {
        sum = Vec2um{Micrometers{sum.x.value + n.pos.x.value},
                     Micrometers{sum.y.value + n.pos.y.value}};
    }
    const Vec2um center{Micrometers{sum.x.value / 4}, Micrometers{sum.y.value / 4}};
    double first = -1.0;
    for (const auto& n : nodes) {
        const double d = openstitch::length_um(n.pos - center);
        if (first < 0.0) {
            first = d;
        } else {
            QVERIFY(std::abs(d - first) < 5.0);  // tolérance arrondi µm
        }
    }
    QVERIFY(first > 2'900.0 && first < 3'100.0);  // rayon attendu ~3 mm
}

void MainWindowTest::drawingTooSmallABoxCreatesNoObject() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);

    const std::size_t before = window.project_.vector_objects.size();
    window.setTool(Tool::DrawRectangle);
    view->boxDrawnMm(QRectF(0.0, 0.0, 0.05, 0.05), Qt::NoModifier);  // très en dessous du seuil minimal

    QCOMPARE(window.project_.vector_objects.size(), before);
    QVERIFY(!window.undoStack_.canUndo());
}

void MainWindowTest::drawPolygonAccumulatesVerticesAndClosesOnDoubleClick() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);

    const std::size_t before = window.project_.vector_objects.size();
    window.setTool(Tool::DrawPolygon);
    view->canvasClickedMm(QPointF(0.0, 0.0));
    QCOMPARE(window.pendingPolygonVertices_.size(), std::size_t{1});
    view->canvasClickedMm(QPointF(10.0, 0.0));
    view->canvasClickedMm(QPointF(10.0, -10.0));
    QCOMPARE(window.pendingPolygonVertices_.size(), std::size_t{3});
    QVERIFY(window.polygonPreviewItem_ != nullptr);

    view->canvasDoubleClickedMm(QPointF(10.0, -10.0));

    QCOMPARE(window.project_.vector_objects.size(), before + 1);
    const auto& obj = window.project_.vector_objects.back();
    QCOMPARE(obj.paths[0].outer.nodes.size(), std::size_t{3});
    QVERIFY(obj.paths[0].outer.closed);
    QVERIFY(window.pendingPolygonVertices_.empty());
    QVERIFY(window.polygonPreviewItem_ == nullptr);

    window.undo();
    QCOMPARE(window.project_.vector_objects.size(), before);
}

void MainWindowTest::drawPolygonWithFewerThanThreeVerticesCancelsOnDoubleClick() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);

    const std::size_t before = window.project_.vector_objects.size();
    window.setTool(Tool::DrawPolygon);
    view->canvasClickedMm(QPointF(0.0, 0.0));
    view->canvasClickedMm(QPointF(5.0, 0.0));  // seulement deux sommets
    view->canvasDoubleClickedMm(QPointF(5.0, 0.0));

    QCOMPARE(window.project_.vector_objects.size(), before);  // rien créé
    QVERIFY(window.pendingPolygonVertices_.empty());          // état nettoyé quand même
    QVERIFY(window.polygonPreviewItem_ == nullptr);
}

void MainWindowTest::switchingToolDuringPolygonDrawCancelsIt() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);

    window.setTool(Tool::DrawPolygon);
    view->canvasClickedMm(QPointF(0.0, 0.0));
    view->canvasClickedMm(QPointF(5.0, 0.0));
    QCOMPARE(window.pendingPolygonVertices_.size(), std::size_t{2});
    QVERIFY(window.polygonPreviewItem_ != nullptr);

    window.setTool(Tool::Select);  // simule Échap / changement d'outil

    QVERIFY(window.pendingPolygonVertices_.empty());
    QVERIFY(window.polygonPreviewItem_ == nullptr);
}

void MainWindowTest::polygonDoubleClickDoesNotAddADuplicateVertex() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);

    const std::size_t before = window.project_.vector_objects.size();
    window.setTool(Tool::DrawPolygon);
    view->canvasClickedMm(QPointF(0.0, 0.0));
    view->canvasClickedMm(QPointF(10.0, 0.0));
    view->canvasClickedMm(QPointF(10.0, -10.0));
    // Séquence Qt réelle d'un double-clic : la 2e pression émet AUSSI un clic
    // simple, quasi au même point que le dernier sommet posé -- doit être
    // ignoré (cf. onCanvasClicked) pour ne pas poser un sommet fantôme.
    view->canvasClickedMm(QPointF(10.0, -10.0));
    QCOMPARE(window.pendingPolygonVertices_.size(), std::size_t{3});  // pas 4
    view->canvasDoubleClickedMm(QPointF(10.0, -10.0));

    QCOMPARE(window.project_.vector_objects.size(), before + 1);
    QCOMPARE(window.project_.vector_objects.back().paths[0].outer.nodes.size(), std::size_t{3});
}

// --- Tracé à main levée (lasso) -----------------------------------------
//
// Complète la fonctionnalité « formes dessinées à la main » (rectangle/
// ellipse/polygone) : un quatrième outil, capturant un tracé continu au
// glisser plutôt qu'un cadre élastique ou des clics successifs, simplifié
// (Douglas-Peucker) à la fermeture pour rester exploitable.

void MainWindowTest::drawFreeformCreatesUndoableVectorObject() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);

    const std::size_t before = window.project_.vector_objects.size();
    window.setTool(Tool::DrawFreeform);

    // Trace approximativement un carré de 10 x 10 mm, avec plusieurs points
    // intermédiaires colinéaires sur chaque bord (comme le ferait un glisser
    // souris réel, un point par évènement de déplacement) : la simplification
    // doit les absorber et ne garder que les coins.
    const std::vector<QPointF> stroke = {
        {0, 0},   {2, 0},   {4, 0},   {6, 0},   {8, 0},   {10, 0},
        {10, -2}, {10, -4}, {10, -6}, {10, -8}, {10, -10},
        {8, -10}, {6, -10}, {4, -10}, {2, -10}, {0, -10},
        {0, -8},  {0, -6},  {0, -4},  {0, -2},
    };
    for (const auto& p : stroke) {
        view->freeformPointMm(p);
    }
    QCOMPARE(window.pendingFreeformPoints_.size(), stroke.size());
    QVERIFY(window.freeformPreviewItem_ != nullptr);

    view->freeformStrokeFinished();

    QCOMPARE(window.project_.vector_objects.size(), before + 1);
    const auto& obj = window.project_.vector_objects.back();
    QVERIFY(obj.paths[0].outer.closed);
    // Simplifié : nettement moins que les 20 points bruts, mais un contour
    // exploitable (>= 3 sommets).
    QVERIFY(obj.paths[0].outer.nodes.size() >= 3);
    QVERIFY(obj.paths[0].outer.nodes.size() < stroke.size());
    // Aire proche de 10 x 10 mm = 100 000 000 µm² (tolérance : arrondi de
    // simplification près des coins).
    const double area = std::abs(openstitch::geometry::signed_area_um2(obj.paths[0].outer));
    QVERIFY(area > 90'000'000.0 && area < 110'000'000.0);
    QVERIFY(window.selectedObject_.has_value());
    QCOMPARE(*window.selectedObject_, obj.id);
    QVERIFY(window.pendingFreeformPoints_.empty());
    QVERIFY(window.freeformPreviewItem_ == nullptr);

    window.undo();
    QCOMPARE(window.project_.vector_objects.size(), before);
    window.redo();
    QCOMPARE(window.project_.vector_objects.size(), before + 1);
}

void MainWindowTest::drawFreeformWithTooFewPointsCancelsOnRelease() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);

    const std::size_t before = window.project_.vector_objects.size();
    window.setTool(Tool::DrawFreeform);
    view->freeformPointMm(QPointF(0.0, 0.0));
    view->freeformPointMm(QPointF(1.0, 0.0));  // seulement deux points
    view->freeformStrokeFinished();

    QCOMPARE(window.project_.vector_objects.size(), before);  // rien créé
    QVERIFY(window.pendingFreeformPoints_.empty());           // état nettoyé quand même
    QVERIFY(window.freeformPreviewItem_ == nullptr);
}

void MainWindowTest::switchingToolDuringFreeformDrawCancelsIt() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);

    window.setTool(Tool::DrawFreeform);
    view->freeformPointMm(QPointF(0.0, 0.0));
    view->freeformPointMm(QPointF(5.0, 0.0));
    QCOMPARE(window.pendingFreeformPoints_.size(), std::size_t{2});
    QVERIFY(window.freeformPreviewItem_ != nullptr);

    window.setTool(Tool::Select);  // simule Échap / changement d'outil

    QVERIFY(window.pendingFreeformPoints_.empty());
    QVERIFY(window.freeformPreviewItem_ == nullptr);
}

// ---------------------------------------------------------------------------
// Colonne satin manuelle (outil DrawSatinColumn)
// ---------------------------------------------------------------------------

void MainWindowTest::manualSatinColumnCreatesRailsAndRungsFromAlternatingPairs() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);

    const std::size_t embBefore = window.project_.embroidery_objects.size();
    const std::size_t vecBefore = window.project_.vector_objects.size();
    window.setTool(Tool::DrawSatinColumn);

    // Deux paires A/B (scène Y vers le bas -> modèle Y vers le haut, ADR-003) :
    // A1(0,0) B1(0,4) A2(10,0) B2(10,4) en mm modèle.
    view->canvasClickedMm(QPointF(0.0, 0.0));
    QCOMPARE(window.pendingSatinPoints_.size(), std::size_t{1});
    view->canvasClickedMm(QPointF(0.0, -4.0));
    view->canvasClickedMm(QPointF(10.0, 0.0));
    view->canvasClickedMm(QPointF(10.0, -4.0));
    QCOMPARE(window.pendingSatinPoints_.size(), std::size_t{4});
    QVERIFY(window.satinPreviewItem_ != nullptr);

    view->canvasDoubleClickedMm(QPointF(10.0, -4.0));

    QCOMPARE(window.project_.embroidery_objects.size(), embBefore + 1);
    QCOMPARE(window.project_.vector_objects.size(), vecBefore + 1);  // contour source synthétique
    QVERIFY(window.pendingSatinPoints_.empty());
    QVERIFY(window.satinPreviewItem_ == nullptr);

    const auto& obj = window.project_.embroidery_objects.back();
    QVERIFY(obj.is_satin());
    const auto& satin = std::get<openstitch::document::SatinParams>(obj.params);
    QCOMPARE(satin.rail_a.nodes.size(), std::size_t{2});
    QCOMPARE(satin.rail_b.nodes.size(), std::size_t{2});
    QVERIFY(!satin.rail_a.closed);
    QCOMPARE(satin.rail_a.nodes[0].pos, (Vec2um{Micrometers{0}, Micrometers{0}}));
    QCOMPARE(satin.rail_a.nodes[1].pos, (Vec2um{Micrometers{10'000}, Micrometers{0}}));
    QCOMPARE(satin.rail_b.nodes[0].pos, (Vec2um{Micrometers{0}, Micrometers{4'000}}));
    QCOMPARE(satin.rail_b.nodes[1].pos, (Vec2um{Micrometers{10'000}, Micrometers{4'000}}));
    QCOMPARE(satin.rungs.size(), std::size_t{2});
    QCOMPARE(satin.rungs[0].a, (Vec2um{Micrometers{0}, Micrometers{0}}));
    QCOMPARE(satin.rungs[0].b, (Vec2um{Micrometers{0}, Micrometers{4'000}}));
    QCOMPARE(satin.rungs[1].a, (Vec2um{Micrometers{10'000}, Micrometers{0}}));
    QCOMPARE(satin.rungs[1].b, (Vec2um{Micrometers{10'000}, Micrometers{4'000}}));

    QVERIFY(window.selectedEmbroidery_.has_value());
    QCOMPARE(window.selectedEmbroidery_->value, obj.id.value);
    QCOMPARE(QString::fromStdString(window.undoStack_.undoName()),
             QStringLiteral("Colonne satin (création manuelle)"));

    window.undo();
    QCOMPARE(window.project_.embroidery_objects.size(), embBefore);
    QCOMPARE(window.project_.vector_objects.size(), vecBefore);
    window.redo();
    QCOMPARE(window.project_.embroidery_objects.size(), embBefore + 1);
}

void MainWindowTest::manualSatinColumnDropsOrphanPointOnOddCountAtFinish() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);

    const std::size_t embBefore = window.project_.embroidery_objects.size();
    window.setTool(Tool::DrawSatinColumn);

    // Deux paires completes + un point orphelin (A3 sans B3) : la finalisation
    // doit abandonner ce dernier point plutôt que planter ou bloquer, et créer
    // l'objet avec les deux paires complètes seulement.
    view->canvasClickedMm(QPointF(0.0, 0.0));
    view->canvasClickedMm(QPointF(0.0, -4.0));
    view->canvasClickedMm(QPointF(10.0, 0.0));
    view->canvasClickedMm(QPointF(10.0, -4.0));
    view->canvasClickedMm(QPointF(20.0, 0.0));  // A3 orphelin
    QCOMPARE(window.pendingSatinPoints_.size(), std::size_t{5});

    window.finishSatinColumn();

    QCOMPARE(window.project_.embroidery_objects.size(), embBefore + 1);
    const auto& satin =
        std::get<openstitch::document::SatinParams>(window.project_.embroidery_objects.back().params);
    QCOMPARE(satin.rungs.size(), std::size_t{2});  // le point orphelin n'a pas créé de 3e paire
    QVERIFY(window.pendingSatinPoints_.empty());
}

void MainWindowTest::switchingToolDuringSatinColumnDrawCancelsIt() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);

    window.setTool(Tool::DrawSatinColumn);
    view->canvasClickedMm(QPointF(0.0, 0.0));
    view->canvasClickedMm(QPointF(0.0, -4.0));
    QCOMPARE(window.pendingSatinPoints_.size(), std::size_t{2});
    QVERIFY(window.satinPreviewItem_ != nullptr);

    window.setTool(Tool::Select);  // simule Échap / changement d'outil

    QVERIFY(window.pendingSatinPoints_.empty());
    QVERIFY(window.satinPreviewItem_ == nullptr);
    QVERIFY(!window.undoStack_.canUndo());  // rien n'a été créé
}

void MainWindowTest::satinRailEditModeDragsNodeAndUndoRestoresIt() {
    MainWindow window;
    const Fixture fx = buildSatinGuideFixture();
    window.applyLoadedProject(fx.project);
    window.resize(900, 700);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto* action = window.findChild<QAction*>(QStringLiteral("action_satinRailEditMode"));
    QVERIFY(action != nullptr);
    QVERIFY(!action->isEnabled());
    window.selectedEmbroidery_ = fx.embroideryId;
    window.updateActions();
    QVERIFY(action->isEnabled());
    action->setChecked(true);
    QVERIFY(action->isChecked());
    QCOMPARE(window.railEditTarget_->value, fx.embroideryId.value);

    // Rail A (2 noeuds) + rail B (2 noeuds) : 4 poignées.
    QList<NodeHandleItem*> handles;
    for (QGraphicsItem* item : window.baseItems_) {
        if (auto* handle = dynamic_cast<NodeHandleItem*>(item)) {
            handles.push_back(handle);
        }
    }
    QCOMPARE(handles.size(), 4);

    window.view_->resetTransform();
    window.view_->scale(40.0, 40.0);
    window.view_->centerOn(QPointF(0.0, 0.0));
    auto it = std::find_if(handles.begin(), handles.end(), [](const NodeHandleItem* handle) {
        return std::abs(handle->scenePos().x()) < 0.01 && std::abs(handle->scenePos().y()) < 0.01;
    });
    QVERIFY(it != handles.end());
    auto* first = *it;  // noeud de départ du rail A, en (0,0)

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
    QCOMPARE(QString::fromStdString(window.undoStack_.undoName()),
             QStringLiteral("Déplacement de nœud de rail satin"));
    const auto& moved = std::get<openstitch::document::SatinParams>(
        window.project_.findEmbroidery(fx.embroideryId)->params);
    QVERIFY(moved.rail_a.nodes[0].pos.x.value != 0);

    window.undo();
    const auto& restored = std::get<openstitch::document::SatinParams>(
        window.project_.findEmbroidery(fx.embroideryId)->params);
    QCOMPARE(restored.rail_a.nodes[0].pos, (Vec2um{Micrometers{0}, Micrometers{0}}));

    action->setChecked(false);
    QVERIFY(!window.railEditTarget_.has_value());
    QCoreApplication::processEvents();
}

// ---------------------------------------------------------------------------
// Réponses à l'audit UI (retour utilisateur en usage réel)
// ---------------------------------------------------------------------------

void MainWindowTest::bezierToolClickCreatesCornerNodes() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);
    window.resize(900, 700);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    const std::size_t before = window.project_.vector_objects.size();
    window.setTool(Tool::DrawBezier);
    view->resetTransform();
    view->scale(4.0, 4.0);
    view->centerOn(QPointF(0.0, 0.0));

    // Trois clics NETS (pas de glisser) : trois nœuds Coin, sans poignées.
    const auto clickAt = [&](QPointF sceneMm) {
        const QPoint vp = view->mapFromScene(sceneMm);
        QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, vp);
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, vp);
    };
    clickAt(QPointF(0.0, 0.0));
    QCOMPARE(window.pendingBezierNodes_.size(), std::size_t{1});
    clickAt(QPointF(20.0, 0.0));
    clickAt(QPointF(20.0, -20.0));
    QCOMPARE(window.pendingBezierNodes_.size(), std::size_t{3});
    for (const auto& node : window.pendingBezierNodes_) {
        QVERIFY(node.type == openstitch::geometry::NodeType::Corner);
        QVERIFY(!node.tan_in.has_value());
        QVERIFY(!node.tan_out.has_value());
    }

    window.finishBezier();
    QCOMPARE(window.project_.vector_objects.size(), before + 1);
    const auto& obj = window.project_.vector_objects.back();
    QCOMPARE(obj.paths[0].outer.nodes.size(), std::size_t{3});
    QVERIFY(obj.paths[0].outer.closed);
    QVERIFY(window.pendingBezierNodes_.empty());

    window.undo();
    QCOMPARE(window.project_.vector_objects.size(), before);
}

void MainWindowTest::bezierToolDragCreatesSmoothNodeWithSymmetricHandles() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);
    window.resize(900, 700);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.setTool(Tool::DrawBezier);
    view->resetTransform();
    view->scale(4.0, 4.0);
    view->centerOn(QPointF(0.0, 0.0));

    const QPoint anchorVp = view->mapFromScene(QPointF(0.0, 0.0));
    const QPoint handleVp = anchorVp + QPoint(40, 0);  // glisser net, bien au-dessus du seuil
    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, anchorVp);
    QTest::mouseMove(view->viewport(), anchorVp + QPoint(20, 0));
    QTest::mouseMove(view->viewport(), handleVp);
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, handleVp);

    QCOMPARE(window.pendingBezierNodes_.size(), std::size_t{1});
    const auto& node = window.pendingBezierNodes_[0];
    QVERIFY(node.type == openstitch::geometry::NodeType::Smooth);
    QVERIFY(node.tan_out.has_value());
    QVERIFY(node.tan_in.has_value());
    // Poignées symétriques : tan_in = -tan_out exactement.
    QCOMPARE(node.tan_in->x.value, -node.tan_out->x.value);
    QCOMPARE(node.tan_in->y.value, -node.tan_out->y.value);
    QVERIFY(node.tan_out->x.value > 0);  // glissé vers la droite

    window.cancelBezierDraw();
    QVERIFY(window.pendingBezierNodes_.empty());
}

void MainWindowTest::finishDrawActionAndEnterKeyBothClosePolygon() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);
    QVERIFY(window.finishDrawAct_ != nullptr);

    const std::size_t before = window.project_.vector_objects.size();
    window.setTool(Tool::DrawPolygon);
    QVERIFY(!window.finishDrawAct_->isEnabled());  // rien tracé encore

    view->canvasClickedMm(QPointF(0.0, 0.0));
    view->canvasClickedMm(QPointF(10.0, 0.0));
    view->canvasClickedMm(QPointF(10.0, -10.0));
    QVERIFY(window.finishDrawAct_->isEnabled());

    // Le bouton Terminer clôt le polygone -- pas seulement le double-clic
    // (défaut remonté en usage réel : aucun moyen visible de valider la forme).
    window.finishDrawAct_->trigger();
    QCOMPARE(window.project_.vector_objects.size(), before + 1);
    QVERIFY(window.pendingPolygonVertices_.empty());
    QVERIFY(!window.finishDrawAct_->isEnabled());
    window.undo();
    QCOMPARE(window.project_.vector_objects.size(), before);

    // Le raccourci Entrée (QShortcut dédié) fait de même -- déclenché via le
    // système de méta-objets Qt plutôt qu'un évènement clavier synthétique :
    // QTest::keyPress n'est pas fiable en offscreen pour les raccourcis
    // fenêtre (cf. commentaire de drawEllipseWithShiftConstrainsToCircle).
    window.setTool(Tool::DrawPolygon);
    view->canvasClickedMm(QPointF(0.0, 0.0));
    view->canvasClickedMm(QPointF(10.0, 0.0));
    view->canvasClickedMm(QPointF(10.0, -10.0));
    QShortcut* enterShortcut = nullptr;
    for (auto* sc : window.findChildren<QShortcut*>()) {
        if (sc->key() == QKeySequence(Qt::Key_Return)) {
            enterShortcut = sc;
            break;
        }
    }
    QVERIFY(enterShortcut != nullptr);
    QMetaObject::invokeMethod(enterShortcut, "activated");
    QCOMPARE(window.project_.vector_objects.size(), before + 1);
    QVERIFY(window.pendingPolygonVertices_.empty());
}

void MainWindowTest::deleteVectorObjectRemovesShapeAndDependentEmbroidery() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);

    const std::size_t vecBefore = window.project_.vector_objects.size();
    const std::size_t embBefore = window.project_.embroidery_objects.size();
    window.deleteVectorObject(fx.vectorId);

    QCOMPARE(window.project_.vector_objects.size(), vecBefore - 1);
    QCOMPARE(window.project_.embroidery_objects.size(), embBefore - 1);  // broderie liee partie aussi
    QVERIFY(window.project_.findObject(fx.vectorId) == nullptr);
    QVERIFY(window.project_.findEmbroidery(fx.embroideryId) == nullptr);
    QCOMPARE(QString::fromStdString(window.undoStack_.undoName()),
             QStringLiteral("Supprimer l'objet vectoriel"));

    window.undo();
    QCOMPARE(window.project_.vector_objects.size(), vecBefore);
    QCOMPARE(window.project_.embroidery_objects.size(), embBefore);
    QVERIFY(window.project_.findObject(fx.vectorId) != nullptr);
    QVERIFY(window.project_.findEmbroidery(fx.embroideryId) != nullptr);
}

void MainWindowTest::duplicateVectorObjectOffsetsCopyAndIsUndoable() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);

    const std::size_t before = window.project_.vector_objects.size();
    const auto* original = window.project_.findObject(fx.vectorId);
    QVERIFY(original != nullptr);
    const Vec2um originalFirstNode = original->paths[0].outer.nodes[0].pos;

    window.duplicateVectorObject(fx.vectorId);
    QCOMPARE(window.project_.vector_objects.size(), before + 1);
    const auto& copy = window.project_.vector_objects.back();
    QVERIFY(copy.id.value != fx.vectorId.value);
    QVERIFY(copy.paths[0].outer.nodes[0].pos != originalFirstNode);  // décalée, pas superposée
    QVERIFY(window.selectedObject_.has_value());
    QCOMPARE(window.selectedObject_->value, copy.id.value);

    window.undo();
    QCOMPARE(window.project_.vector_objects.size(), before);
}

void MainWindowTest::satinEditModeTogglesBothUnderlyingModes() {
    MainWindow window;
    const Fixture fx = buildSatinGuideFixture();
    window.applyLoadedProject(fx.project);

    QVERIFY(window.satinEditModeAct_ != nullptr);
    window.selectedEmbroidery_ = fx.embroideryId;
    window.updateActions();
    QVERIFY(window.satinEditModeAct_->isEnabled());
    QVERIFY(!window.satinGuideModeAct_->isChecked());
    QVERIFY(!window.railEditModeAct_->isChecked());

    window.satinEditModeAct_->setChecked(true);
    QVERIFY(window.satinGuideModeAct_->isChecked());
    QVERIFY(window.railEditModeAct_->isChecked());
    QVERIFY(window.satinGuideTarget_.has_value());
    QVERIFY(window.railEditTarget_.has_value());

    window.satinEditModeAct_->setChecked(false);
    QVERIFY(!window.satinGuideModeAct_->isChecked());
    QVERIFY(!window.railEditModeAct_->isChecked());
}

}  // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_main_window.moc"
