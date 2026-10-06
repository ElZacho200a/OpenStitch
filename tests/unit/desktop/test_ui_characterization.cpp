// SPDX-License-Identifier: Apache-2.0
// Tests de caractérisation de la fenêtre principale (lot L0 « filet de sécurité » de la
// modernisation UI, docs/ui-audit-2026-10.md §4) : ils figent le comportement ACTUEL
// avant les refontes L1..L5. Les défauts connus de l'audit sont déclarés avec
// QEXPECT_FAIL(Continue) : la suite reste verte, et dès qu'un lot corrige le défaut le
// test échoue en « XPASS » jusqu'à ce que le marqueur soit retiré (même convention que
// test_ui_invariants.cpp). Aucune comparaison de pixels, aucun sleep.
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDockWidget>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>
#include <QToolButton>

#include <functional>
#include <utility>
#include <vector>

#include "canvas_view.hpp"
#include "main_window.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/segmentation/segmentation.hpp"

using openstitch::Micrometers;
using openstitch::ObjectId;
using openstitch::RegionId;
using openstitch::Vec2um;
using openstitch::desktop::CanvasView;
using openstitch::desktop::MainWindow;

namespace {

namespace doc = openstitch::document;
namespace geo = openstitch::geometry;

// Projet minimal : image 2x2 (sans elle refreshImage() sort tôt et ne régénère rien).
doc::Project imageOnlyProject() {
    doc::Project project;
    project.original.width = 2;
    project.original.height = 2;
    project.original.rgba.assign(2 * 2 * 4, 255);
    return project;
}

void addRegion(doc::Project& project) {
    openstitch::segmentation::Segmentation seg;
    seg.width = 1;
    seg.height = 1;
    seg.labels = {1};
    seg.region_slots.push_back(openstitch::segmentation::Region{RegionId{1}, {200, 30, 30}, 1});
    project.segmentation = std::move(seg);
}

// Carré de 10 mm (assez long pour produire de vrais points) ; renvoie l'id du vecteur.
ObjectId addSquareVector(doc::Project& project) {
    doc::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.name = "Square";
    geo::Path square;
    square.closed = true;
    constexpr std::int32_t s = 10'000;
    for (const auto& [x, y] : std::vector<std::pair<std::int32_t, std::int32_t>>{
             {0, 0}, {s, 0}, {s, s}, {0, s}}) {
        square.nodes.push_back(geo::PathNode{Vec2um{Micrometers{x}, Micrometers{y}},
                                             geo::NodeType::Corner, std::nullopt, std::nullopt});
    }
    vec.paths.push_back(geo::PathSet{square, {}});
    const ObjectId id = vec.id;
    project.vector_objects.push_back(std::move(vec));
    return id;
}

ObjectId addEmbroidery(doc::Project& project, ObjectId source) {
    doc::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.name = "Square - contour";
    emb.source_vector = source;
    emb.params = doc::RunningStitchParams{};
    const ObjectId id = emb.id;
    project.embroidery_objects.push_back(std::move(emb));
    return id;
}

// Colonne satin manuelle (2 rails, 3 barreaux) sans vecteur source : sélectionnable
// et éditable (rails + guides), comme buildSatinGuideFixture de test_main_window.cpp.
ObjectId addSatinColumn(doc::Project& project) {
    doc::SatinParams satin;
    satin.rail_a.closed = false;
    satin.rail_b.closed = false;
    satin.rail_a.nodes = {{{Micrometers{0}, Micrometers{0}}},
                          {{Micrometers{10'000}, Micrometers{0}}}};
    satin.rail_b.nodes = {{{Micrometers{0}, Micrometers{4'000}}},
                          {{Micrometers{10'000}, Micrometers{4'000}}}};
    satin.rungs = {
        {{Micrometers{0}, Micrometers{0}}, {Micrometers{0}, Micrometers{4'000}}},
        {{Micrometers{5'000}, Micrometers{0}}, {Micrometers{5'000}, Micrometers{4'000}}},
        {{Micrometers{10'000}, Micrometers{0}}, {Micrometers{10'000}, Micrometers{4'000}}}};
    doc::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.name = "Satin";
    emb.params = satin;
    const ObjectId id = emb.id;
    project.embroidery_objects.push_back(std::move(emb));
    return id;
}

QAction* findMenuAction(const MainWindow& window, const QString& textPart) {
    const std::function<QAction*(const QMenu*)> search = [&](const QMenu* menu) -> QAction* {
        for (QAction* action : menu->actions()) {
            if (action->menu() != nullptr) {
                if (QAction* found = search(action->menu())) {
                    return found;
                }
            } else if (action->text().contains(textPart)) {
                return action;
            }
        }
        return nullptr;
    };
    for (const QAction* top : window.menuBar()->actions()) {
        if (top->menu() != nullptr) {
            if (QAction* found = search(top->menu())) {
                return found;
            }
        }
    }
    return nullptr;
}

} // namespace

namespace openstitch::desktop {

// Nom imposé par `friend class MainWindowTest` (main_window.hpp) : accès aux membres
// privés (actions, sélections, docks) sans ajouter de seam de production.
class MainWindowTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("UiCharacterizationTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }

    // ---- (a) matrice d'activation des actions -------------------------------------

    void actionMatrixEmptyWindow() {
        MainWindow window;
        QCOMPARE(snapshot(window), QStringLiteral("EMPTY"));
    }

    void actionMatrixImageLoaded() {
        MainWindow window;
        window.applyLoadedProject(imageOnlyProject());
        QCOMPARE(snapshot(window), QStringLiteral("IMAGE"));
    }

    void actionMatrixSegmentationPresentNoSelection() {
        MainWindow window;
        doc::Project project = imageOnlyProject();
        addRegion(project);
        window.applyLoadedProject(project);
        QCOMPARE(snapshot(window), QStringLiteral("SEG"));
    }

    void actionMatrixRegionSelected() {
        MainWindow window;
        doc::Project project = imageOnlyProject();
        addRegion(project);
        window.applyLoadedProject(project);
        window.selectedRegion_ = RegionId{1};
        window.updateActions();
        QCOMPARE(snapshot(window), QStringLiteral("REGION"));
    }

    void actionMatrixVectorObjectSelectedWithoutEmbroidery() {
        MainWindow window;
        doc::Project project = imageOnlyProject();
        const ObjectId vec = addSquareVector(project);
        window.applyLoadedProject(project);
        window.selectedObject_ = vec;
        window.updateActions();
        QCOMPARE(snapshot(window), QStringLiteral("VECTOR"));
    }

    // Un objet vectoriel dont l'objet de broderie existe : resolveSelectedEmbroidery()
    // retombe sur la broderie liée, donc les actions « broderie » s'activent aussi.
    void actionMatrixVectorObjectSelectedWithLinkedEmbroidery() {
        MainWindow window;
        doc::Project project = imageOnlyProject();
        const ObjectId vec = addSquareVector(project);
        addEmbroidery(project, vec);
        window.applyLoadedProject(project);
        window.selectedObject_ = vec;
        window.updateActions();
        QCOMPARE(snapshot(window), QStringLiteral("VECTOR_LINKED"));
    }

    void actionMatrixEmbroiderySelected() {
        MainWindow window;
        doc::Project project = imageOnlyProject();
        const ObjectId vec = addSquareVector(project);
        const ObjectId emb = addEmbroidery(project, vec);
        window.applyLoadedProject(project);
        window.selectedEmbroidery_ = emb;
        window.updateActions();
        QCOMPARE(snapshot(window), QStringLiteral("EMBROIDERY"));
    }

    void actionMatrixSatinEmbroiderySelected() {
        MainWindow window;
        doc::Project project = imageOnlyProject();
        const ObjectId emb = addSatinColumn(project);
        window.applyLoadedProject(project);
        window.selectedEmbroidery_ = emb;
        window.updateActions();
        QCOMPARE(snapshot(window), QStringLiteral("SATIN"));
    }

    // Audit 2026-10 (gravité Basse) : les actions « document requis » restent actives
    // sur une fenêtre vide. Le test ci-dessus fige l'état actuel pour le reste ; ici
    // l'état SOUHAITÉ de ces quatre actions, en échec attendu.
    void documentRequiredActionsAreDisabledWithoutDocument() {
        MainWindow window;
        const QList<std::pair<QString, QAction*>> actions = {
            {QStringLiteral("Enregistrer"), window.findChild<QAction*>("action_saveProject")},
            {QStringLiteral("Enregistrer sous"),
             window.findChild<QAction*>("action_saveProjectAs")},
            {QStringLiteral("Exporter DXF"), findMenuAction(window, QStringLiteral("DXF"))},
            {QStringLiteral("Analyser"), window.analyzeAct_},
        };
        for (const auto& [name, action] : actions) {
            QVERIFY2(action != nullptr, qPrintable(name));
        }
        // Exporter en DXF : on cherche « Exporter » + « DXF » (l'import contient « DXF » aussi).
        QAction* exportDxf = nullptr;
        for (const QAction* top : window.menuBar()->actions()) {
            for (QAction* act : top->menu()->actions()) {
                if (act->text().contains(QStringLiteral("Exporter")) &&
                    act->text().contains(QStringLiteral("XF"))) {
                    exportDxf = act;
                }
            }
        }
        QVERIFY(exportDxf != nullptr);
        QAction* saveAct = window.findChild<QAction*>("action_saveProject");
        QAction* saveAsAct = window.findChild<QAction*>("action_saveProjectAs");
        QAction* analyzeAct = window.analyzeAct_;

        QEXPECT_FAIL("", "Enregistrer actif sans document (audit UI 2026-10, Basse)", Continue);
        QVERIFY(!saveAct->isEnabled());
        QEXPECT_FAIL("", "Enregistrer sous actif sans document (audit UI 2026-10, Basse)", Continue);
        QVERIFY(!saveAsAct->isEnabled());
        QEXPECT_FAIL("", "Exporter en DXF actif sans document (audit UI 2026-10, Basse)", Continue);
        QVERIFY(!exportDxf->isEnabled());
        QEXPECT_FAIL("", "Analyser actif sans document (audit UI 2026-10, Basse)", Continue);
        QVERIFY(!analyzeAct->isEnabled());
    }

    // Les actions déjà conformes : sans document, Exporter DST/Statistiques/Annuler/
    // Rétablir sont désactivées (garde-fou : ne doit pas régresser avec L1).
    void sequenceDependentActionsAreDisabledWithoutDocument() {
        MainWindow window;
        QVERIFY(!window.exportDstAct_->isEnabled());
        QVERIFY(!window.statsAct_->isEnabled());
        QVERIFY(!window.undoAct_->isEnabled());
        QVERIFY(!window.redoAct_->isEnabled());
    }

    // ---- (b) touche Suppr par type d'objet ----------------------------------------

    void deleteKeyDeletesSelectedRegion() {
        MainWindow window;
        doc::Project project = imageOnlyProject();
        addRegion(project);
        window.applyLoadedProject(project);
        window.selectedRegion_ = RegionId{1};
        window.updateActions();
        activate(window);

        QVERIFY(window.project_.segmentation->find(RegionId{1}) != nullptr);
        QTest::keyClick(&window, Qt::Key_Delete);
        QVERIFY(window.project_.segmentation->find(RegionId{1}) == nullptr);
        QVERIFY(window.undoStack_.canUndo());
    }

    void deleteKeyOnVectorObjectDoesNotDeleteIt() {
        MainWindow window;
        doc::Project project = imageOnlyProject();
        const ObjectId vec = addSquareVector(project);
        window.applyLoadedProject(project);
        window.selectedObject_ = vec;
        window.updateActions();
        activate(window);

        QTest::keyClick(&window, Qt::Key_Delete);
        // Comportement actuel : rien ne se passe, l'objet reste (audit 2026-10, Moyenne).
        QVERIFY(!window.undoStack_.canUndo());
        QEXPECT_FAIL("", "Suppr ne supprime pas un objet vectoriel (audit UI 2026-10)", Continue);
        QVERIFY(window.project_.findObject(vec) == nullptr);
    }

    void deleteKeyOnEmbroideryObjectDoesNotDeleteIt() {
        MainWindow window;
        doc::Project project = imageOnlyProject();
        const ObjectId vec = addSquareVector(project);
        const ObjectId emb = addEmbroidery(project, vec);
        window.applyLoadedProject(project);
        window.selectedEmbroidery_ = emb;
        window.updateActions();
        activate(window);

        QTest::keyClick(&window, Qt::Key_Delete);
        QVERIFY(!window.undoStack_.canUndo());
        QEXPECT_FAIL("", "Suppr ne supprime pas un objet de broderie (audit UI 2026-10)",
                     Continue);
        QVERIFY(window.project_.findEmbroidery(emb) == nullptr);
    }

    // ---- (c) exclusivité des modes d'édition --------------------------------------

    // Sens « rails -> points » : activer les rails alors que « Éditer les points » est
    // actif coupe l'édition des points (comportement actuel, conforme).
    void enablingRailEditUnchecksStitchEdit() {
        MainWindow window;
        doc::Project project = imageOnlyProject();
        const ObjectId emb = addSatinColumn(project);
        window.applyLoadedProject(project);
        window.selectedEmbroidery_ = emb;
        window.updateActions();

        window.stitchEditModeAct_->setChecked(true);
        QVERIFY(window.stitchEditModeAct_->isChecked());
        window.railEditModeAct_->setChecked(true);
        QVERIFY(window.railEditModeAct_->isChecked());
        QVERIFY(!window.stitchEditModeAct_->isChecked());
    }

    // Sens « points -> rails » : « Éditer les points » ne désactive pas le mode rails
    // (audit 2026-10, Moyenne) : deux jeux de poignées coexistent.
    void enablingStitchEditWhileRailEditIsOnLeavesBothChecked() {
        MainWindow window;
        doc::Project project = imageOnlyProject();
        const ObjectId emb = addSatinColumn(project);
        window.applyLoadedProject(project);
        window.selectedEmbroidery_ = emb;
        window.updateActions();

        window.railEditModeAct_->setChecked(true);
        QVERIFY(window.railEditModeAct_->isChecked());
        QVERIFY(window.stitchEditModeAct_->isEnabled());
        window.stitchEditModeAct_->setChecked(true);
        QVERIFY(window.stitchEditModeAct_->isChecked());
        QEXPECT_FAIL("", "Éditer les points laisse le mode rails actif (audit UI 2026-10)",
                     Continue);
        QVERIFY(!window.railEditModeAct_->isChecked());
    }

    // ---- (d) docks : « Masquer les panneaux » contre les rafraîchissements --------

    void refreshingPanelsWhileHidePanelsModeIsOnReShowsTheDocks() {
        MainWindow window;
        doc::Project project = imageOnlyProject();
        const ObjectId vec = addSquareVector(project);
        addEmbroidery(project, vec);
        window.applyLoadedProject(project);
        window.resize(1400, 900);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.documentDock_->isVisible());
        QVERIFY(window.orderDock_->isVisible());

        QAction* hideAct = findMenuAction(window, QStringLiteral("Masquer les panneaux"));
        QVERIFY(hideAct != nullptr);
        hideAct->setChecked(true);
        QVERIFY(!window.documentDock_->isVisible());
        QVERIFY(!window.orderDock_->isVisible());
        QVERIFY(!window.filterDock_->isVisible());

        // Un simple rafraîchissement (sélection, undo, régénération...) réaffiche les
        // panneaux alors que le mode « canevas seul » est toujours coché.
        window.refreshDocumentPanel();
        window.refreshOrderPanel();
        window.refreshFilterPanel();
        QVERIFY(hideAct->isChecked());
        QEXPECT_FAIL("", "refreshDocumentPanel réaffiche le dock malgré Masquer (audit 2026-10)",
                     Continue);
        QVERIFY(!window.documentDock_->isVisible());
        QEXPECT_FAIL("", "refreshOrderPanel réaffiche le dock malgré Masquer (audit 2026-10)",
                     Continue);
        QVERIFY(!window.orderDock_->isVisible());
        QEXPECT_FAIL("", "refreshFilterPanel réaffiche le dock malgré Masquer (audit 2026-10)",
                     Continue);
        QVERIFY(!window.filterDock_->isVisible());
    }

    // Même défaut côté Analyse : runAnalysis() force show() sur le dock.
    void runningAnalysisWhileHidePanelsModeIsOnReShowsTheAnalysisDock() {
        MainWindow window;
        doc::Project project = imageOnlyProject();
        const ObjectId vec = addSquareVector(project);
        addEmbroidery(project, vec);
        window.applyLoadedProject(project);
        window.resize(1400, 900);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.sequence_.has_value());

        QAction* hideAct = findMenuAction(window, QStringLiteral("Masquer les panneaux"));
        QVERIFY(hideAct != nullptr);
        hideAct->setChecked(true);
        QVERIFY(!window.analysisDock_->isVisible());

        window.runAnalysis();
        QVERIFY(hideAct->isChecked());
        QEXPECT_FAIL("", "runAnalysis réaffiche le dock malgré Masquer (audit UI 2026-10)",
                     Continue);
        QVERIFY(!window.analysisDock_->isVisible());
    }

    // Le mode « Masquer » lui-même puis son annulation restaurent bien les docks.
    void hidePanelsToggleRestoresPreviouslyVisibleDocks() {
        MainWindow window;
        doc::Project project = imageOnlyProject();
        const ObjectId vec = addSquareVector(project);
        addEmbroidery(project, vec);
        window.applyLoadedProject(project);
        window.resize(1400, 900);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const bool docVisible = window.documentDock_->isVisible();
        const bool propsVisible = window.propertiesDock_->isVisible();
        QVERIFY(docVisible);

        QAction* hideAct = findMenuAction(window, QStringLiteral("Masquer les panneaux"));
        QVERIFY(hideAct != nullptr);
        hideAct->setChecked(true);
        QVERIFY(!window.documentDock_->isVisible());
        hideAct->setChecked(false);
        QCOMPARE(window.documentDock_->isVisible(), docVisible);
        QCOMPARE(window.propertiesDock_->isVisible(), propsVisible);
    }

    // ---- (e) fuite de la barre d'outils contextuelle -------------------------------

    // Chaque reconstruction (clear()) laisse les anciennes actions/widgets enfants de la
    // barre : le nombre d'enfants croît avec le nombre de changements de sélection.
    void contextToolbarChildCountIsStableAcrossSelectionChanges() {
        MainWindow window;
        doc::Project project = imageOnlyProject();
        addRegion(project);
        const ObjectId vec = addSquareVector(project);
        const ObjectId emb = addEmbroidery(project, vec);
        window.applyLoadedProject(project);

        const auto cycle = [&] {
            window.selectedRegion_ = RegionId{1};
            window.selectedObject_.reset();
            window.selectedEmbroidery_.reset();
            window.updateActions();
            window.selectedRegion_.reset();
            window.selectedObject_ = vec;
            window.updateActions();
            window.selectedObject_.reset();
            window.selectedEmbroidery_ = emb;
            window.updateActions();
            window.selectedEmbroidery_.reset();
            window.updateActions();
        };
        const auto childCount = [&] {
            return window.contextToolbar_->findChildren<QAction*>().size() +
                   window.contextToolbar_->findChildren<QToolButton*>().size() +
                   window.contextToolbar_->findChildren<QLabel*>().size();
        };

        cycle(); // premier passage : amorce (actions partagées, premières créations)
        const int afterFirst = childCount();
        QVERIFY(afterFirst > 0);
        for (int i = 0; i < 5; ++i) {
            cycle();
        }
        const int afterMany = childCount();
        QEXPECT_FAIL("", "la barre contextuelle accumule ses enfants (audit UI 2026-10, Moyenne)",
                     Continue);
        QCOMPARE(afterMany, afterFirst);
    }

    // Garde-fou : une sélection inchangée ne reconstruit pas la barre (signature).
    void contextToolbarIsNotRebuiltWhenSelectionIsUnchanged() {
        MainWindow window;
        doc::Project project = imageOnlyProject();
        const ObjectId vec = addSquareVector(project);
        window.applyLoadedProject(project);
        window.selectedObject_ = vec;
        window.updateActions();
        const int before = window.contextToolbar_->findChildren<QObject*>().size();
        for (int i = 0; i < 5; ++i) {
            window.updateActions();
        }
        QCOMPARE(window.contextToolbar_->findChildren<QObject*>().size(), before);
    }

private:
    // Fenêtre visible et active : les raccourcis (contexte fenêtre) ne sont livrés qu'à
    // une fenêtre active.
    static void activate(MainWindow& window) {
        window.resize(1400, 900);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
    }

    // État d'activation des actions suivies, « nom=0/1 » ; comparé à une table dans la
    // valeur attendue. Les quatre actions « document requis » (défaut connu) sont
    // exclues : elles ont leur propre test XFAIL.
    static QString snapshot(MainWindow& window) {
        const auto named = [&](const char* name) { return window.findChild<QAction*>(name); };
        const std::vector<std::pair<const char*, QAction*>> actions = {
            {"newProject", named("action_newProject")},
            {"deleteRegion", named("action_deleteRegion")},
            {"segmentWithAi", named("action_segmentWithAi")},
            {"generationOptions", named("action_generationOptions")},
            {"undo", window.undoAct_},
            {"redo", window.redoAct_},
            {"createStitch", window.createStitchAct_},
            {"createTatami", window.createTatamiAct_},
            {"createSatin", window.createSatinAct_},
            {"autoSatin", window.autoSatinAct_},
            {"fillAngle", window.fillAngleAct_},
            {"convertSatin", window.convertSatinAct_},
            {"stats", window.statsAct_},
            {"exportDst", window.exportDstAct_},
            {"stitchEdit", window.stitchEditModeAct_},
            {"satinEdit", window.satinEditModeAct_},
            {"satinGuides", window.satinGuideModeAct_},
            {"satinRails", window.railEditModeAct_},
            {"addGuide", window.addSatinGuideAct_},
            {"removeGuide", window.removeSatinGuideAct_},
            {"merge", window.mergeAct_},
        };
        QString out; struct P { QString* o; ~P() { qInfo().noquote() << "SNAP" << *o; } } pp{&out};
        for (const auto& [name, action] : actions) {
            if (!out.isEmpty()) {
                out += QLatin1Char(' ');
            }
            out += QString::fromLatin1(name) + QLatin1Char('=') +
                   (action == nullptr ? QStringLiteral("?")
                                      : QString::number(action->isEnabled() ? 1 : 0));
        }
        return out;
    }

    QTemporaryDir settingsDir_;
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_ui_characterization.moc"
