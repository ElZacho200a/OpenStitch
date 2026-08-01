// SPDX-License-Identifier: Apache-2.0
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QListWidget>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>

#include "canvas_view.hpp"
#include "document_panel.hpp"
#include "main_window.hpp"
#include "openstitch/document/project.hpp"
#include "properties_panel.hpp"

using openstitch::Micrometers;
using openstitch::ObjectId;
using openstitch::RegionId;
using openstitch::Vec2um;
using openstitch::desktop::CanvasView;
using openstitch::desktop::DocumentPanel;
using openstitch::desktop::MainWindow;
using openstitch::desktop::PropertiesPanel;

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

private:
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

}  // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_main_window.moc"
