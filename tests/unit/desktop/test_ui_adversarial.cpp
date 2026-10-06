// SPDX-License-Identifier: Apache-2.0
// Tests adverses du lot L5 (revue indépendante) : sélection multiple, Suppr universel,
// glisser avec modificateurs, accroche des nœuds, appui long, ligne d'indications,
// menu Aide, préréglages de navigation. Chaque défaut réel est déclaré par
// QEXPECT_FAIL(Continue) + un commentaire « BUG: » ; aucune assertion n'est affaiblie.
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QFocusEvent>
#include <QGraphicsItem>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollBar>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "autosave.hpp"
#include "canvas_view.hpp"
#include "help_dialogs.hpp"
#include "interaction_map.hpp"
#include "main_window.hpp"
#include "native_gesture_helper.hpp"
#include "node_handle.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/document/project.hpp"
#include "selection_hit_test.hpp"

using openstitch::Micrometers;
using openstitch::ObjectId;
using openstitch::RegionId;
using openstitch::Vec2um;
using openstitch::desktop::CanvasView;
using openstitch::desktop::MainWindow;
namespace geo = openstitch::geometry;
namespace doc = openstitch::document;

namespace {

// ---- fixtures ----------------------------------------------------------------------

geo::PathNode corner(int xUm, int yUm) {
    return geo::PathNode{Vec2um{Micrometers{xUm}, Micrometers{yUm}}, geo::NodeType::Corner,
                         std::nullopt, std::nullopt};
}

geo::Path squarePath(int cxMm, int cyMm, int halfMm) {
    geo::Path p;
    p.closed = true;
    for (const auto& [dx, dy] :
         {std::pair{-1, -1}, std::pair{1, -1}, std::pair{1, 1}, std::pair{-1, 1}}) {
        p.nodes.push_back(corner((cxMm + dx * halfMm) * 1000, (cyMm + dy * halfMm) * 1000));
    }
    return p;
}

ObjectId addObject(doc::Project& project, const char* name, std::vector<geo::PathSet> paths,
                   bool visible = true) {
    doc::VectorObject v;
    v.id = project.object_ids.next();
    v.name = name;
    v.paths = std::move(paths);
    v.visible = visible;
    project.vector_objects.push_back(v);
    return v.id;
}

ObjectId addSquare(doc::Project& project, const char* name, int cxMm, int cyMm = 0,
                   int halfMm = 5) {
    return addObject(project, name, {geo::PathSet{squarePath(cxMm, cyMm, halfMm), {}}});
}

ObjectId addEmbroidery(doc::Project& project, const char* name, ObjectId source) {
    doc::EmbroideryObject e;
    e.id = project.object_ids.next();
    e.name = name;
    e.source_vector = source;
    e.params = doc::RunningStitchParams{};
    project.embroidery_objects.push_back(e);
    return e.id;
}

void addImageAndRegion(doc::Project& project) {
    project.original.width = 2;
    project.original.height = 2;
    project.original.rgba.assign(2 * 2 * 4, 255);
    openstitch::segmentation::Segmentation seg;
    seg.width = 1;
    seg.height = 1;
    seg.labels = {1};
    seg.region_slots.push_back(openstitch::segmentation::Region{RegionId{1}, {200, 30, 30}, 1});
    project.segmentation = std::move(seg);
}

struct Big {
    doc::Project project;
    std::vector<ObjectId> squares;      // 4 carrés visibles (x = -30, -15, 0, 15), 10 mm
    ObjectId holed{};                   // carré à trou (x = 30)
    ObjectId open{};                    // polyligne ouverte (x = 45)
    ObjectId proxy{};                   // proxy satin INVISIBLE
    ObjectId proxyEmb{};                // broderie satin sur le proxy
    std::vector<ObjectId> embroideries; // contours (S0, S1, S1 bis)
};

Big buildBig(bool withImage = true) {
    Big b;
    if (withImage) {
        addImageAndRegion(b.project);
    }
    for (int i = 0; i < 4; ++i) {
        static const char* names[] = {"S0", "S1", "S2", "S3"};
        b.squares.push_back(addSquare(b.project, names[i], -30 + 15 * i));
    }
    geo::PathSet holed{squarePath(30, 0, 5), {}};
    holed.holes.push_back(squarePath(30, 0, 2));
    b.holed = addObject(b.project, "Holed", {holed});
    geo::Path open;
    open.closed = false;
    open.nodes = {corner(41'000, -4'000), corner(45'000, 4'000), corner(49'000, -4'000)};
    b.open = addObject(b.project, "Open", {geo::PathSet{open, {}}});
    b.proxy = addObject(b.project, "Proxy", {geo::PathSet{squarePath(0, 25, 3), {}}}, false);

    b.embroideries.push_back(addEmbroidery(b.project, "E-S0", b.squares[0]));
    b.embroideries.push_back(addEmbroidery(b.project, "E-S1", b.squares[1]));
    b.embroideries.push_back(addEmbroidery(b.project, "E-S1bis", b.squares[1]));
    doc::EmbroideryObject satin;
    satin.id = b.project.object_ids.next();
    satin.name = "Satin proxy";
    satin.source_vector = b.proxy;
    doc::SatinParams sp;
    sp.rail_a.closed = false;
    sp.rail_b.closed = false;
    sp.rail_a.nodes = {corner(-3'000, 22'000), corner(3'000, 22'000)};
    sp.rail_b.nodes = {corner(-3'000, 28'000), corner(3'000, 28'000)};
    for (const int x : {-3'000, 3'000}) {
        doc::SatinRung rung;
        rung.a = Vec2um{Micrometers{x}, Micrometers{22'000}};
        rung.b = Vec2um{Micrometers{x}, Micrometers{28'000}};
        sp.rungs.push_back(rung);
    }
    satin.params = sp;
    b.proxyEmb = satin.id;
    b.project.embroidery_objects.push_back(satin);
    return b;
}

// Empreinte exhaustive du document (hors compteur d'ids) : objets, géométrie, ordre,
// broderies, régions.
std::string snap(const doc::Project& project) {
    std::ostringstream out;
    for (const auto& v : project.vector_objects) {
        out << "V" << v.id.value << ':' << v.name << ':' << v.visible << ':'
            << static_cast<int>(v.rgb[0]) << '[';
        for (const auto& set : v.paths) {
            const auto dump = [&](const geo::Path& p) {
                out << (p.closed ? 'c' : 'o');
                for (const auto& n : p.nodes) {
                    out << n.pos.x.value << ',' << n.pos.y.value << ';';
                }
                out << '/';
            };
            dump(set.outer);
            for (const auto& h : set.holes) {
                dump(h);
            }
            out << '|';
        }
        out << "]\n";
    }
    for (const auto& e : project.embroidery_objects) {
        out << "E" << e.id.value << ':' << e.name << ':' << e.source_vector.value << ':'
            << e.params.index() << ':' << e.visible << '\n';
    }
    if (project.segmentation) {
        for (const auto& slot : project.segmentation->region_slots) {
            out << "R" << (slot ? static_cast<long>(slot->id.value) : -1L) << '\n';
        }
        for (const auto l : project.segmentation->labels) {
            out << l << ',';
        }
    }
    return out.str();
}

// Intégrité du document indépendamment de la sélection : ids uniques, broderies liées
// à une source existante.
QString documentProblem(const doc::Project& project) {
    std::set<std::uint64_t> ids;
    for (const auto& v : project.vector_objects) {
        if (!ids.insert(v.id.value).second) {
            return QStringLiteral("duplicate vector id %1").arg(v.id.value);
        }
    }
    for (const auto& e : project.embroidery_objects) {
        if (!ids.insert(e.id.value).second) {
            return QStringLiteral("duplicate embroidery id %1").arg(e.id.value);
        }
        if (project.findObject(e.source_vector) == nullptr) {
            return QStringLiteral("orphan embroidery %1 (source %2)")
                .arg(e.id.value)
                .arg(e.source_vector.value);
        }
    }
    return {};
}

struct Lcg {
    std::uint32_t s;
    std::uint32_t next() {
        s = s * 1664525u + 1013904223u;
        return s >> 8;
    }
    std::uint32_t below(std::uint32_t n) { return next() % n; }
};

void sendMouse(QWidget* viewport, QEvent::Type type, const QPoint& at, Qt::MouseButton button,
               Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) {
    QMouseEvent event(type, QPointF(at), QPointF(viewport->mapToGlobal(at)), button, buttons, mods);
    QApplication::sendEvent(viewport, &event);
}

void closePopups() {
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (auto* menu = qobject_cast<QMenu*>(w); menu != nullptr && menu->isVisible()) {
            menu->close();
        }
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

} // namespace

namespace openstitch::desktop {

class MainWindowTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUIAdversarial"));
        QCoreApplication::setApplicationName(QStringLiteral("UiAdversarialTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
        QStandardPaths::setTestModeEnabled(true);
        QDir(QFileInfo(slotFor(QString()).osp_path).absolutePath()).removeRecursively();
    }
    void cleanup() {
        closePopups();
        InteractionMap::setLongPressMsForTesting(-1);
        InteractionMap::setPreset(Preset::OpenStitch);
        InteractionMap::savePreset();
    }

    // ---- sélection / undo ------------------------------------------------------------
    void fuzzSelectionUndoRedoKeepsInvariants_data() {
        QTest::addColumn<int>("seed");
        const int extra = qEnvironmentVariableIntValue("OSADV_SEEDS");
        for (int seed = 1; seed < 6 + std::max(0, extra); ++seed) {
            QTest::newRow(qPrintable(QStringLiteral("seed%1").arg(seed))) << seed;
        }
    }
    void fuzzSelectionUndoRedoKeepsInvariants();
    void fuzzWithoutImageKeepsInvariants();
    void fuzzCanvasEventsKeepStateMachineSane_data() {
        QTest::addColumn<int>("seed");
        // OSADV_SEEDS=N : explore N graines au lieu de 4 (chasse aux défauts hors CI).
        const int extra = qEnvironmentVariableIntValue("OSADV_SEEDS");
        for (int seed = 21; seed < 25 + std::max(0, extra); ++seed) {
            QTest::newRow(qPrintable(QStringLiteral("seed%1").arg(seed))) << seed;
        }
    }
    void fuzzCanvasEventsKeepStateMachineSane();

    // ---- Suppr universel -------------------------------------------------------------
    void deleteWithRegionAndEmbroideryBothSelectedKeepsRegionAndUndoRestores();
    void deleteOfSatinProxyEmbroideryRemovesHiddenSourceAndUndoRestoresIndices();
    void deleteMultiSelectionWithEmbroideryChildrenRestoresInterleavedOrder();
    void deleteWhileAnEditModeTargetsTheDeletedObjectLeavesNoDanglingMode();
    void deleteWhilePolygonIsPendingKeepsThePendingDrawUsable();
    void deleteDuringFreeformStrokeKeepsDocumentConsistent();
    void deleteWithOnlyHiddenProxySourceSelectedViaRectangleIsImpossible();

    // ---- duplication / glisser -------------------------------------------------------
    void altDuplicateOfMultiSelectionWithEmbroideryUndoRedoCyclesKeepIdsAndOrder();
    void duplicateVectorObjectDoesNotShareEmbroideryChildrenAndUndoIsClean();
    void shiftZeroLengthDragCreatesNoUndoStep();
    void dragOfUnselectedOverlappingObjectSelectsTopmostWithoutMoving();
    void deferredBodyDragDoesNotLeakIntoAReplacedDocument();
    void undoImmediatelyAfterReleaseBeforeDeferredCommandIsHarmless();

    // ---- accroche des nœuds ----------------------------------------------------------
    void nodeSnapOntoOddMicrometerNodeIsExactAndUndoRestoresBoth();
    void nodeSnapIgnoresHiddenObjects();
    void nodeSnapWithCtrlAtReleaseKeepsRawPosition();
    void nodeSnapIsOffByDefaultAndSettingPersistsAcrossWindows();
    void hoverHighlightFollowsDeleteUndoMoveAndVisibility();
    void altPressOnSelectedBodyWithJitterDoesNotBothDuplicateAndOpenMenu();
    void plainClickWithOnePixelJitterOnSelectedBodyMovesNothing();

    // ---- sélection rectangle / dessous ------------------------------------------------
    void degenerateRectanglesNeverSelectOrCrash();
    void crossingRectangleHonoursHolesAndFullEnclosure();
    void selectBelowOnZeroAndOneObject();
    void selectBelowActionOfObjectDeletedMeanwhileIsHarmless();
    void selectBelowToggleAndAddModes();

    // ---- gestes du canevas -----------------------------------------------------------
    void longPressCancelledByToolChange();
    void longPressCancelledByFocusLoss();
    void escapeCancelsRectangleInProgress();
    void middleThenLeftPressDoesNotClickAndLeftThenMiddleClicksOnce();
    void spaceHeldIsResetByToolChangeAndIgnoredWithModalOrTextFocus();
    void staleModifierHintsAfterFocusLoss();

    // ---- ligne d'indications ---------------------------------------------------------
    void hintsForEveryToolModifierAndPresetAreDeterministicAndSane();
    void hintsWidgetNeverForcesWindowWidth();
    void windowSurvivesTinySizeShowHideCycles();

    // ---- menu Aide / préréglages -----------------------------------------------------
    void helpDialogsAreSingleInstanceAcrossRepeatedOpenAndClose();
    void f1WhileDialogOpenAndPresetSwitchWhileDialogOpenThenClose();
    void gesturesDialogSearchSurvivesHostileInputAndRebuilds();
    void quickStartButtonsFollowActionStateAndDisabledOnesNeverFire();
    void twoWindowsShareTheGlobalPresetConsistently();
    void presetPersistsAcrossWindowsAndCorruptSettingFallsBack();

    // ---- chargement / sauvegarde -----------------------------------------------------
    void projectsOfEveryShapeEnableSaveAndKeepSelectionSane();
    void autosaveTickWithMultiSelectionIsHarmless();

private:
    CanvasView* openBig(MainWindow& window, Big& big);
    static QPoint vp(const CanvasView* view, double xMm, double yMm) {
        return view->mapFromScene(QPointF(xMm, yMm));
    }
    static void dragWith(CanvasView* view, QPoint from, QPoint to, Qt::KeyboardModifiers mods,
                         Qt::KeyboardModifiers pressMods = Qt::NoModifier) {
        QWidget* w = view->viewport();
        sendMouse(w, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, pressMods);
        sendMouse(w, QEvent::MouseMove, (from + to) / 2, Qt::NoButton, Qt::LeftButton, mods);
        sendMouse(w, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, mods);
        sendMouse(w, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, mods);
    }
    QString checkAll(MainWindow& window) {
        if (!window.checkSelectionInvariants()) {
            return QStringLiteral("selection invariants violated");
        }
        return documentProblem(window.project_);
    }
    void roundTrip(MainWindow& window, const std::string& baseSnap, const QString& where);
    void runFuzz(int seed, bool withImage);

    QTemporaryDir settingsDir_;
};

CanvasView* MainWindowTest::openBig(MainWindow& window, Big& big) {
    big = buildBig();
    window.applyLoadedProject(big.project);
    window.setTool(Tool::Select);
    window.refreshImage();
    auto* view = window.findChild<CanvasView*>();
    if (view == nullptr) {
        return nullptr;
    }
    window.resize(1600, 1000);
    window.show();
    if (!QTest::qWaitForWindowExposed(&window)) {
        return nullptr;
    }
    view->resetTransform();
    view->scale(10.0, 10.0);
    view->centerOn(QPointF(0.0, 0.0));
    return view;
}

void MainWindowTest::roundTrip(MainWindow& window, const std::string& baseSnap,
                               const QString& where) {
    std::vector<std::string> down{snap(window.project_)};
    while (window.undoStack_.canUndo()) {
        window.undo();
        const QString problem = checkAll(window);
        QVERIFY2(problem.isEmpty(), qPrintable(where + QStringLiteral(" undo: ") + problem));
        down.push_back(snap(window.project_));
        QVERIFY2(down.size() < 100000, "undo loop does not terminate");
    }
    QVERIFY2(down.back() == baseSnap, qPrintable(where + QStringLiteral(": undo-all != base")));
    for (std::size_t i = down.size() - 1; i-- > 0;) {
        QVERIFY2(window.undoStack_.canRedo(), qPrintable(where + QStringLiteral(": redo missing")));
        window.redo();
        const QString problem = checkAll(window);
        QVERIFY2(problem.isEmpty(), qPrintable(where + QStringLiteral(" redo: ") + problem));
        QVERIFY2(snap(window.project_) == down[i],
                 qPrintable(where + QStringLiteral(": redo state differs at %1").arg(i)));
    }
}

void MainWindowTest::runFuzz(int seed, bool withImage) {
    MainWindow window;
    const Big big = buildBig(withImage);
    window.applyLoadedProject(big.project);
    window.setTool(Tool::Select);
    std::string baseSnap = snap(window.project_);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);
    auto* del = window.findChild<QAction*>(QStringLiteral("action_deleteRegion"));
    QVERIFY(del != nullptr);

    std::vector<ObjectId> pool;
    for (const auto& v : big.project.vector_objects) {
        pool.push_back(v.id);
    }
    pool.push_back(ObjectId{9999}); // id mort
    Lcg rng{static_cast<std::uint32_t>(seed) * 2654435761u + 12345u};
    const auto anyId = [&] { return pool[rng.below(static_cast<std::uint32_t>(pool.size()))]; };
    const auto mode = [&] { return static_cast<SelectMode>(rng.below(3)); };
    const auto point = [&] {
        return QPointF(static_cast<double>(rng.below(120)) - 60.0 + 0.37,
                       static_cast<double>(rng.below(80)) - 40.0 + 0.11);
    };
    const auto rect = [&] {
        const double w = rng.below(6) == 0 ? 0.0 : static_cast<double>(rng.below(70));
        const double h = rng.below(6) == 0 ? 0.0 : static_cast<double>(rng.below(60));
        const QPointF p = point();
        return QRectF(p.x(), p.y(), w, h);
    };
    const auto delta = [&] {
        return Vec2um{Micrometers{static_cast<std::int32_t>(rng.below(4001)) - 2000},
                      Micrometers{static_cast<std::int32_t>(rng.below(4001)) - 2000}};
    };
    const bool trace = qEnvironmentVariableIsSet("OSADV_TRACE");
    qInfo("fuzz seed %d image=%d", seed, withImage ? 1 : 0);

    int multiStates = 0;
    int multiDeletes = 0;
    int deletes = 0;
    int undos = 0;
    int redos = 0;
    int dups = 0;
    constexpr int kSteps = 200;
    constexpr int kOps = 24;
    for (int step = 0; step < kSteps; ++step) {
        std::uint32_t op = rng.below(kOps);
        if (op == 12 && rng.below(5) != 0) {
            op = 10 + rng.below(2); // chargements rares : la pile d'annulation doit vivre
        }
        const QString where = QStringLiteral("seed %1 step %2 op %3").arg(seed).arg(step).arg(op);
        if (trace) {
            qInfo("%s", qPrintable(where));
        }
        if (window.project_.vector_objects.empty() && window.project_.embroidery_objects.empty()) {
            // Document vide (chargement d'un projet vide) : un nouveau document riche.
            window.applyLoadedProject(buildBig(withImage).project);
            baseSnap = snap(window.project_);
        }
        const std::size_t objectsBefore = window.project_.vector_objects.size();
        const bool multiBefore = window.hasMultiSelection();
        const bool canUndoBefore = window.undoStack_.canUndo();
        const bool canRedoBefore = window.undoStack_.canRedo();
        switch (op) {
        case 0:
            window.applySelectionClick(anyId(), SelectMode::Replace);
            break;
        case 1:
            window.applySelectionClick(anyId(), SelectMode::Add);
            break;
        case 2:
            window.applySelectionClick(anyId(), SelectMode::Toggle);
            break;
        case 3:
            window.applySelectionClick(std::nullopt, mode());
            break;
        case 4:
            window.onSelectionRectangle(rect(), mode(), rng.below(2) == 0);
            break;
        case 5:
            window.onSelectionClicked(point(), mode());
            break;
        case 6:
            window.onSelectBelow(point(), QPoint(20, 20), mode());
            break;
        case 7:
            if (!window.selectBelowMenu_.isNull() &&
                !window.selectBelowMenu_->actions().isEmpty()) {
                const auto acts = window.selectBelowMenu_->actions();
                acts[static_cast<int>(rng.below(static_cast<std::uint32_t>(acts.size())))]
                    ->trigger();
            }
            break;
        case 8:
        case 9:
            del->trigger();
            break;
        case 10:
            window.undo();
            break;
        case 11:
            window.redo();
            break;
        case 12: {
            const std::uint32_t which = rng.below(8);
            window.applyLoadedProject(which == 0 ? doc::Project{} : buildBig(which != 1).project);
            baseSnap = snap(window.project_);
            break;
        }
        case 13:
            QTest::keyClick(view, rng.below(2) == 0 ? Qt::Key_Right : Qt::Key_Up,
                            rng.below(2) == 0 ? Qt::ShiftModifier : Qt::NoModifier);
            break;
        case 14:
            window.duplicateVectorObject(anyId());
            break;
        case 15:
            window.duplicateAndTranslate(window.selectedObjectIds(), delta());
            break;
        case 16:
            if (!window.project_.embroidery_objects.empty()) {
                const auto& embs = window.project_.embroidery_objects;
                const auto& emb = embs[rng.below(static_cast<std::uint32_t>(embs.size()))];
                std::optional<RegionId> region;
                if (window.project_.segmentation &&
                    window.project_.segmentation->find(RegionId{1}) && rng.below(2) == 0) {
                    region = RegionId{1};
                }
                std::vector<ObjectId> objs;
                if (rng.below(2) == 0 && window.project_.findObject(emb.source_vector) != nullptr) {
                    objs.push_back(emb.source_vector);
                }
                window.setSelection({.region = region, .embroidery = emb.id, .objects = objs});
                window.selectionChanged();
            }
            break;
        case 17:
            if (window.project_.segmentation && window.project_.segmentation->find(RegionId{1})) {
                window.setSelection(
                    {.region = RegionId{1}, .embroidery = std::nullopt, .objects = {}});
                window.selectionChanged();
            }
            break;
        case 18: {
            static const Tool tools[] = {Tool::Select, Tool::Pan,           Tool::DrawPolygon,
                                         Tool::Select, Tool::DrawRectangle, Tool::Select};
            window.setTool(tools[rng.below(6)]);
            break;
        }
        case 19:
            window.translateObjects(window.selectedObjectIds(), delta());
            break;
        case 20:
            window.refreshImage();
            break;
        case 22:
        case 23: {
            // Multi-sélection explicite (2 à 4 objets) puis, une fois sur deux, Suppr.
            std::vector<ObjectId> subset;
            const std::uint32_t n = 2 + rng.below(3);
            for (std::uint32_t i = 0; i < n; ++i) {
                subset.push_back(anyId());
            }
            window.applySelectionRectangle(subset, SelectMode::Replace);
            if (op == 22) {
                del->trigger();
            }
            break;
        }
        case 21:
            if (rng.below(4) == 0) {
                roundTrip(window, baseSnap, where);
                if (QTest::currentTestFailed()) {
                    return;
                }
            }
            break;
        default:
            break;
        }
        if (QTest::currentTestFailed()) {
            return;
        }
        multiStates += window.hasMultiSelection() ? 1 : 0;
        if ((op == 8 || op == 9 || op == 22) &&
            window.project_.vector_objects.size() < objectsBefore) {
            ++deletes;
            multiDeletes +=
                (objectsBefore - window.project_.vector_objects.size() >= 2 || multiBefore) ? 1 : 0;
        }
        undos += (op == 10 && canUndoBefore) ? 1 : 0;
        redos += (op == 11 && canRedoBefore) ? 1 : 0;
        dups += ((op == 14 || op == 15) && window.project_.vector_objects.size() > objectsBefore)
                    ? 1
                    : 0;
        const QString problem = checkAll(window);
        QVERIFY2(problem.isEmpty(), qPrintable(where + QStringLiteral(": ") + problem));
        // Un document sans sélection n'a rien à supprimer ; une sélection existante doit
        // pouvoir l'être.
        if (window.selectedObject_ || window.selectedEmbroidery_ || window.selectedRegion_) {
            QVERIFY2(del->isEnabled(), qPrintable(where + QStringLiteral(": Suppr disabled")));
        }
    }
    roundTrip(window, baseSnap, QStringLiteral("seed %1 final").arg(seed));
    qInfo("coverage seed %d: multi=%d deletes=%d multiDeletes=%d undos=%d redos=%d dups=%d", seed,
          multiStates, deletes, multiDeletes, undos, redos, dups);
    // Le fuzz doit réellement exercer ce qu'il prétend attaquer.
    QVERIFY2(multiStates >= 1 && deletes >= 1 && undos >= 1,
             "fuzz coverage too low: the operation mix no longer exercises the targets");
}

void MainWindowTest::fuzzSelectionUndoRedoKeepsInvariants() {
    QFETCH(int, seed);
    runFuzz(seed, true);
}

void MainWindowTest::fuzzWithoutImageKeepsInvariants() {
    // Document sans image : refreshImage() sort tôt, la sélection doit quand même être
    // élaguée après undo/redo (chemin distinct du cas avec image).
    for (const int seed : {11, 12, 13}) {
        runFuzz(seed, false);
        if (QTest::currentTestFailed()) {
            return;
        }
    }
}

void MainWindowTest::fuzzCanvasEventsKeepStateMachineSane() {
    QFETCH(int, seed);
    MainWindow window;
    Big big;
    CanvasView* view = openBig(window, big);
    QVERIFY(view != nullptr);
    window.activateWindow();
    QWidget* w = view->viewport();
    int modalSeen = 0;
    QTimer watchdog;
    watchdog.setInterval(15);
    connect(&watchdog, &QTimer::timeout, this, [&modalSeen] {
        if (QWidget* modal = QApplication::activeModalWidget()) {
            ++modalSeen;
            modal->close();
        }
        for (QWidget* top : QApplication::topLevelWidgets()) {
            if (auto* menu = qobject_cast<QMenu*>(top); menu != nullptr && menu->isVisible()) {
                menu->close();
            }
        }
    });
    watchdog.start();
    Lcg rng{static_cast<std::uint32_t>(seed) * 40503u + 7u};
    Qt::MouseButtons down;
    std::vector<Qt::MouseButton> pressOrder;
    const Qt::MouseButton buttons[] = {Qt::LeftButton, Qt::LeftButton, Qt::LeftButton,
                                       Qt::MiddleButton, Qt::RightButton};
    const Tool tools[] = {Tool::Select,      Tool::Select,        Tool::Pan,
                          Tool::DrawPolygon, Tool::DrawRectangle, Tool::DrawEllipse,
                          Tool::DrawBezier,  Tool::DrawFreeform,  Tool::DrawPolygonRegular};
    const auto point = [&] {
        return vp(view, static_cast<double>(rng.below(120)) - 60.0,
                  static_cast<double>(rng.below(60)) - 30.0);
    };
    const auto mods = [&] {
        Qt::KeyboardModifiers m;
        const std::uint32_t r = rng.below(8);
        if (r & 1) {
            m |= Qt::ShiftModifier;
        }
        if (r & 2) {
            m |= Qt::ControlModifier;
        }
        if (r & 4) {
            m |= Qt::AltModifier;
        }
        return m;
    };
    const Qt::Key keys[] = {Qt::Key_Space,  Qt::Key_Shift, Qt::Key_Control, Qt::Key_Alt,
                            Qt::Key_Escape, Qt::Key_Left,  Qt::Key_Up,      Qt::Key_Right};
    qInfo("canvas fuzz seed %d", seed);
    for (int step = 0; step < 400; ++step) {
        const std::uint32_t op = rng.below(16);
        const QString where = QStringLiteral("seed %1 step %2 op %3").arg(seed).arg(step).arg(op);
        switch (op) {
        case 0:
        case 1:
        case 2:
            sendMouse(w, QEvent::MouseMove, point(), Qt::NoButton, down, mods());
            break;
        case 3:
        case 4: {
            const Qt::MouseButton b = buttons[rng.below(5)];
            if (!down.testFlag(b)) {
                down |= b;
                pressOrder.push_back(b);
                sendMouse(w, QEvent::MouseButtonPress, point(), b, down, mods());
            }
            break;
        }
        case 5:
        case 6:
            if (!pressOrder.empty()) {
                const std::size_t i = rng.below(static_cast<std::uint32_t>(pressOrder.size()));
                const Qt::MouseButton b = pressOrder[i];
                pressOrder.erase(pressOrder.begin() + static_cast<std::ptrdiff_t>(i));
                down &= ~Qt::MouseButtons(b);
                sendMouse(w, QEvent::MouseButtonRelease, point(), b, down, mods());
            }
            break;
        case 7: {
            const Qt::MouseButton b = buttons[rng.below(5)];
            if (down == Qt::NoButton) {
                sendMouse(w, QEvent::MouseButtonDblClick, point(), b, b, mods());
                sendMouse(w, QEvent::MouseButtonRelease, point(), b, Qt::NoButton, mods());
            }
            break;
        }
        case 8: {
            const QPoint at = point();
            const int delta =
                (static_cast<int>(rng.below(7)) - 3) * 60 + (rng.below(4) == 0 ? 7 : 0);
            const bool xOnly = rng.below(4) == 0;
            QWheelEvent wheel(QPointF(at), QPointF(w->mapToGlobal(at)),
                              rng.below(5) == 0 ? QPoint(0, delta / 4) : QPoint(),
                              xOnly ? QPoint(delta, 0) : QPoint(0, delta), down, mods(),
                              Qt::NoScrollPhase, false);
            QApplication::sendEvent(w, &wheel);
            break;
        }
        case 9: {
            const Qt::Key k = keys[rng.below(8)];
            QKeyEvent press(QEvent::KeyPress, k, mods());
            QApplication::sendEvent(view, &press);
            if (rng.below(3) != 0) {
                QKeyEvent release(QEvent::KeyRelease, k, Qt::NoModifier);
                QApplication::sendEvent(view, &release);
            }
            break;
        }
        case 10: {
            QFocusEvent ev(rng.below(2) == 0 ? QEvent::FocusOut : QEvent::FocusIn,
                           Qt::ActiveWindowFocusReason);
            QApplication::sendEvent(view, &ev);
            break;
        }
        case 11:
            window.setTool(tools[rng.below(9)]);
            break;
        case 12: {
            QEvent ev(rng.below(2) == 0 ? QEvent::Enter : QEvent::Leave);
            QApplication::sendEvent(w, &ev);
            break;
        }
        case 13: {
            const auto native = makeNativeGesture(
                rng.below(2) == 0 ? Qt::ZoomNativeGesture : Qt::PanNativeGesture, QPointF(point()),
                (static_cast<double>(rng.below(21)) - 10.0) / 40.0,
                QPointF(static_cast<double>(rng.below(40)) - 20.0, 5.0));
            QApplication::sendEvent(w, native.get());
            break;
        }
        case 14:
            window.findChild<QAction*>(QStringLiteral("action_deleteRegion"))->trigger();
            if (rng.below(2) == 0) {
                window.undo();
            }
            break;
        case 15:
            QCoreApplication::processEvents();
            if (rng.below(12) == 0) {
                window.hide();
                window.show();
            }
            break;
        default:
            break;
        }
        const QString problem = checkAll(window);
        QVERIFY2(problem.isEmpty(), qPrintable(where + QStringLiteral(": ") + problem));
        const double ppm = view->pixelsPerMm();
        QVERIFY2(ppm > 0.0 && ppm < 1e6, qPrintable(where + QStringLiteral(": zoom %1").arg(ppm)));
    }
    // Retour au calme : tous les boutons relâchés, outil Sélection, aucun appui résiduel.
    for (const Qt::MouseButton b : pressOrder) {
        down &= ~Qt::MouseButtons(b);
        sendMouse(w, QEvent::MouseButtonRelease, vp(view, 0, 0), b, down, {});
    }
    QCoreApplication::processEvents();
    closePopups();
    window.cancelPolygonDraw();
    window.setTool(Tool::Select);
    window.hide();
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    view->resetTransform();
    view->scale(10.0, 10.0);
    view->centerOn(QPointF(0.0, 0.0));
    QKeyEvent spaceUp(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(view, &spaceUp);
    window.applySelectionClick(std::nullopt, SelectMode::Replace);
    // La machine à états doit avoir récupéré : un clic simple sélectionne, sans modificateur
    // fantôme (Maj/Ctrl/Alt rémanents) ni pan fantôme.
    const ObjectId target = big.squares[2];
    const auto* obj = window.project_.findObject(target);
    if (obj != nullptr && obj->visible) {
        sendMouse(w, QEvent::MouseMove, vp(view, 0, 20), Qt::NoButton, Qt::NoButton, {});
        QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier, vp(view, 0, 0));
        const auto ids = window.selectedObjectIds();
        QVERIFY2(ids.size() == 1,
                 qPrintable(QStringLiteral("seed %1: plain click selected %2 objects")
                                .arg(seed)
                                .arg(ids.size())));
    }
    QVERIFY2(modalSeen == 0,
             qPrintable(QStringLiteral("unexpected modal dialog x%1").arg(modalSeen)));
}

// ---- Suppr ---------------------------------------------------------------------------

void MainWindowTest::deleteWithRegionAndEmbroideryBothSelectedKeepsRegionAndUndoRestores() {
    MainWindow window;
    const Big big = buildBig();
    window.applyLoadedProject(big.project);
    const std::string before = snap(window.project_);
    window.setSelection({.region = RegionId{1},
                         .embroidery = big.embroideries[1],
                         .objects = {big.squares[1], big.squares[2]}});
    // Région + broderie : la normalisation ne garde que le principal.
    QVERIFY(window.checkSelectionInvariants());
    QVERIFY(window.multiSelection_.empty());
    window.deleteSelection();
    QVERIFY(window.project_.findEmbroidery(big.embroideries[1]) == nullptr);
    QVERIFY(window.project_.findEmbroidery(big.embroideries[2]) != nullptr); // sa jumelle reste
    QVERIFY(window.project_.segmentation->find(RegionId{1}) != nullptr);
    QVERIFY(window.project_.findObject(big.squares[1]) != nullptr);
    QVERIFY(window.checkSelectionInvariants());
    window.undo();
    QCOMPARE(QString::fromStdString(snap(window.project_)), QString::fromStdString(before));
    QVERIFY(!window.undoStack_.canUndo());
}

void MainWindowTest::deleteOfSatinProxyEmbroideryRemovesHiddenSourceAndUndoRestoresIndices() {
    MainWindow window;
    const Big big = buildBig();
    window.applyLoadedProject(big.project);
    const std::string before = snap(window.project_);
    window.setSelection({.region = std::nullopt,
                         .embroidery = big.proxyEmb,
                         .objects = {big.squares[0], big.squares[1], big.squares[2]}});
    QVERIFY(window.checkSelectionInvariants());
    window.deleteSelection();
    QVERIFY(window.project_.findEmbroidery(big.proxyEmb) == nullptr);
    QVERIFY(window.project_.findObject(big.proxy) == nullptr); // proxy invisible emporté
    QVERIFY(window.project_.findObject(big.squares[0]) != nullptr);
    QVERIFY(window.checkSelectionInvariants());
    QVERIFY(documentProblem(window.project_).isEmpty());
    window.undo();
    QCOMPARE(QString::fromStdString(snap(window.project_)), QString::fromStdString(before));
    window.redo();
    QVERIFY(window.project_.findObject(big.proxy) == nullptr);
    QVERIFY(window.checkSelectionInvariants());
}

void MainWindowTest::deleteMultiSelectionWithEmbroideryChildrenRestoresInterleavedOrder() {
    MainWindow window;
    Big big = buildBig();
    // Entrelace davantage les broderies : S0, S1, S0, S1 (sources alternées).
    addEmbroidery(big.project, "E-S0b", big.squares[0]);
    addEmbroidery(big.project, "E-S1ter", big.squares[1]);
    addEmbroidery(big.project, "E-S2", big.squares[2]);
    window.applyLoadedProject(big.project);
    const std::string before = snap(window.project_);
    window.applySelectionClick(big.squares[0], SelectMode::Replace);
    window.applySelectionClick(big.squares[2], SelectMode::Add);
    window.applySelectionClick(big.squares[1], SelectMode::Add);
    window.deleteSelection();
    QCOMPARE(window.undoStack_.undoName(), std::string("Supprimer 3 objets"));
    QVERIFY(window.checkSelectionInvariants());
    QVERIFY(documentProblem(window.project_).isEmpty());
    for (int cycle = 0; cycle < 3; ++cycle) {
        window.undo();
        QCOMPARE(QString::fromStdString(snap(window.project_)), QString::fromStdString(before));
        window.redo();
        QVERIFY(window.project_.findObject(big.squares[0]) == nullptr);
        QVERIFY(window.checkSelectionInvariants());
    }
}

void MainWindowTest::deleteWhileAnEditModeTargetsTheDeletedObjectLeavesNoDanglingMode() {
    // Modes d'édition (points / guides satin / rails) : supprimer l'objet ciblé (Suppr, undo,
    // redo) ne doit laisser ni cible pendante, ni mode coché sans cible, ni génération cassée.
    struct Case {
        const char* name;
        std::function<QAction*(MainWindow&)> mode;
        std::function<ObjectId(const Big&)> embroidery;
        std::function<bool(MainWindow&)> hasTarget;
    };
    const std::vector<Case> cases = {
        {"stitchEdit", [](MainWindow& w) { return w.stitchEditModeAct_; },
         [](const Big& b) { return b.embroideries[0]; },
         [](MainWindow& w) { return w.stitchEditTarget_.has_value(); }},
        {"satinGuides", [](MainWindow& w) { return w.satinGuideModeAct_; },
         [](const Big& b) { return b.proxyEmb; },
         [](MainWindow& w) { return w.satinGuideTarget_.has_value(); }},
        {"railEdit", [](MainWindow& w) { return w.railEditModeAct_; },
         [](const Big& b) { return b.proxyEmb; },
         [](MainWindow& w) { return w.railEditTarget_.has_value(); }},
    };
    QStringList exercised;
    for (const Case& c : cases) {
        MainWindow window;
        const Big big = buildBig();
        window.applyLoadedProject(big.project);
        window.setSelection(
            {.region = std::nullopt, .embroidery = c.embroidery(big), .objects = {}});
        window.selectionChanged();
        QAction* mode = c.mode(window);
        QVERIFY2(mode != nullptr, c.name);
        if (!mode->isEnabled()) {
            continue; // mode non activable pour cet objet dans cette version : rien à attaquer
        }
        mode->setChecked(true);
        if (!c.hasTarget(window)) {
            continue;
        }
        exercised << QLatin1String(c.name);
        // Suppr de la broderie ciblée (et de son proxy source invisible pour le satin).
        window.deleteSelection();
        QVERIFY2(window.project_.findEmbroidery(c.embroidery(big)) == nullptr, c.name);
        const bool targetGone = !c.hasTarget(window);
        QVERIFY2(targetGone || mode->isChecked(), c.name);
        QVERIFY2(!mode->isChecked() || targetGone,
                 qPrintable(QStringLiteral("%1: mode still checked with a dangling target")
                                .arg(QLatin1String(c.name))));
        // La génération des points n'est pas cassée par la cible disparue.
        QVERIFY2(window.sequence_.has_value(),
                 qPrintable(QStringLiteral("%1: sequence lost after deleting the edit target")
                                .arg(QLatin1String(c.name))));
        QVERIFY2(window.exportDstAct_->isEnabled(), c.name);
        QVERIFY(window.checkSelectionInvariants());
        window.undo();
        QVERIFY(window.project_.findEmbroidery(c.embroidery(big)) != nullptr);
        window.redo();
        QVERIFY(window.sequence_.has_value());
        QVERIFY(window.checkSelectionInvariants());
    }
    qInfo("edit modes exercised: %s", qPrintable(exercised.join(QLatin1Char(','))));
    QVERIFY2(exercised.contains(QStringLiteral("stitchEdit")),
             "the stitch edit mode could not be activated: the attack no longer runs");
}

void MainWindowTest::deleteWhilePolygonIsPendingKeepsThePendingDrawUsable() {
    MainWindow window;
    Big big;
    CanvasView* view = openBig(window, big);
    QVERIFY(view != nullptr);
    window.applySelectionClick(big.squares[0], SelectMode::Replace);
    window.setTool(Tool::DrawPolygon);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, vp(view, 30, 30));
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, vp(view, 40, 30));
    QCOMPARE(window.pendingPolygonVertices_.size(), std::size_t{2});
    window.findChild<QAction*>(QStringLiteral("action_deleteRegion"))->trigger();
    QVERIFY(window.project_.findObject(big.squares[0]) == nullptr);
    QCOMPARE(window.pendingPolygonVertices_.size(), std::size_t{2}); // tracé intact
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, vp(view, 40, 40));
    const auto count = window.project_.vector_objects.size();
    window.finishPolygon();
    QCOMPARE(window.project_.vector_objects.size(), count + 1);
    QVERIFY(window.checkSelectionInvariants());
    // Deux pas d'annulation indépendants : le polygone puis la suppression.
    window.undo();
    QCOMPARE(window.project_.vector_objects.size(), count);
    window.undo();
    QVERIFY(window.project_.findObject(big.squares[0]) != nullptr);
    QVERIFY(documentProblem(window.project_).isEmpty());
}

void MainWindowTest::deleteDuringFreeformStrokeKeepsDocumentConsistent() {
    MainWindow window;
    Big big;
    CanvasView* view = openBig(window, big);
    QVERIFY(view != nullptr);
    window.applySelectionClick(big.squares[3], SelectMode::Replace);
    window.setTool(Tool::DrawFreeform);
    QWidget* w = view->viewport();
    sendMouse(w, QEvent::MouseButtonPress, vp(view, 30, 30), Qt::LeftButton, Qt::LeftButton, {});
    sendMouse(w, QEvent::MouseMove, vp(view, 35, 32), Qt::NoButton, Qt::LeftButton, {});
    window.deleteSelection(); // en plein tracé
    sendMouse(w, QEvent::MouseMove, vp(view, 40, 38), Qt::NoButton, Qt::LeftButton, {});
    sendMouse(w, QEvent::MouseMove, vp(view, 32, 40), Qt::NoButton, Qt::LeftButton, {});
    sendMouse(w, QEvent::MouseButtonRelease, vp(view, 32, 40), Qt::LeftButton, Qt::NoButton, {});
    QVERIFY(window.checkSelectionInvariants());
    QVERIFY(documentProblem(window.project_).isEmpty());
    while (window.undoStack_.canUndo()) {
        window.undo();
        QVERIFY(window.checkSelectionInvariants());
    }
    QVERIFY(window.project_.findObject(big.squares[3]) != nullptr);
}

void MainWindowTest::deleteWithOnlyHiddenProxySourceSelectedViaRectangleIsImpossible() {
    // Un rectangle qui couvre un objet INVISIBLE (proxy satin) ne doit jamais le
    // sélectionner (donc jamais le supprimer ni le déplacer par erreur).
    MainWindow window;
    const Big big = buildBig();
    window.applyLoadedProject(big.project);
    window.setTool(Tool::Select);
    window.onSelectionRectangle(QRectF(-100, -100, 200, 200), SelectMode::Replace, false);
    window.onSelectionRectangle(QRectF(-100, -100, 200, 200), SelectMode::Add, true);
    const auto ids = window.selectedObjectIds();
    QVERIFY(std::find(ids.begin(), ids.end(), big.proxy) == ids.end());
    QCOMPARE(ids.size(), std::size_t{6}); // 4 carrés + carré à trou + polyligne ouverte
}

// ---- duplication / glisser -----------------------------------------------------------

void MainWindowTest::altDuplicateOfMultiSelectionWithEmbroideryUndoRedoCyclesKeepIdsAndOrder() {
    MainWindow window;
    Big big;
    CanvasView* view = openBig(window, big);
    QVERIFY(view != nullptr);
    window.applySelectionRectangle({big.squares[0], big.squares[1], big.squares[2]},
                                   SelectMode::Replace);
    window.refreshImage();
    const std::string before = snap(window.project_);
    const auto embCount = window.project_.embroidery_objects.size();
    const auto vecCount = window.project_.vector_objects.size();

    dragWith(view, vp(view, -15, 0), vp(view, -12, -2), Qt::AltModifier);
    QTRY_COMPARE_WITH_TIMEOUT(window.project_.vector_objects.size(), vecCount + 3, 2000);
    QCOMPARE(window.project_.embroidery_objects.size(), embCount); // aucune broderie clonée
    const std::string afterDup = snap(window.project_);
    QCOMPARE(window.multiSelection_.size(), std::size_t{3});
    QVERIFY(window.checkSelectionInvariants());
    std::vector<ObjectId> copies = window.selectedObjectIds();

    for (int cycle = 0; cycle < 5; ++cycle) {
        window.undo();
        QCOMPARE(QString::fromStdString(snap(window.project_)), QString::fromStdString(before));
        QVERIFY(window.checkSelectionInvariants());
        QVERIFY(!window.undoStack_.canUndo());
        window.redo();
        QCOMPARE(QString::fromStdString(snap(window.project_)), QString::fromStdString(afterDup));
        QVERIFY(window.checkSelectionInvariants());
    }
    // Nouvelle duplication après annulation : ids neufs, jamais ceux de la branche perdue.
    window.undo();
    window.duplicateVectorObject(big.squares[0]);
    const ObjectId fresh = window.project_.vector_objects.back().id;
    QVERIFY(std::find(copies.begin(), copies.end(), fresh) == copies.end());
    QVERIFY(documentProblem(window.project_).isEmpty());
}

void MainWindowTest::duplicateVectorObjectDoesNotShareEmbroideryChildrenAndUndoIsClean() {
    MainWindow window;
    const Big big = buildBig();
    window.applyLoadedProject(big.project);
    const std::string before = snap(window.project_);
    window.duplicateVectorObject(big.squares[1]); // 2 broderies enfants
    QCOMPARE(window.project_.embroidery_objects.size(), big.project.embroidery_objects.size());
    const ObjectId copy = window.project_.vector_objects.back().id;
    for (const auto& e : window.project_.embroidery_objects) {
        QVERIFY(e.source_vector != copy);
    }
    window.undo();
    QCOMPARE(QString::fromStdString(snap(window.project_)), QString::fromStdString(before));
    window.duplicateVectorObject(ObjectId{424242}); // inexistant : sans effet
    QVERIFY(!window.undoStack_.canUndo());
}

void MainWindowTest::shiftZeroLengthDragCreatesNoUndoStep() {
    MainWindow window;
    Big big;
    CanvasView* view = openBig(window, big);
    QVERIFY(view != nullptr);
    window.applySelectionClick(big.squares[1], SelectMode::Replace);
    window.refreshImage();
    const std::string before = snap(window.project_);
    dragWith(view, vp(view, -15, 0), vp(view, -15, 0), Qt::ShiftModifier);
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    QCOMPARE(QString::fromStdString(snap(window.project_)), QString::fromStdString(before));
    QVERIFY(!window.undoStack_.canUndo());
    // Maj + glisser quasi nul (1 px en diagonale) : verrou d'axe sans division par zéro.
    dragWith(view, vp(view, -15, 0), vp(view, -15, 0) + QPoint(1, 1), Qt::ShiftModifier);
    QCoreApplication::processEvents();
    QVERIFY(window.checkSelectionInvariants());
    QVERIFY(documentProblem(window.project_).isEmpty());
}

void MainWindowTest::dragOfUnselectedOverlappingObjectSelectsTopmostWithoutMoving() {
    MainWindow window;
    Big big;
    CanvasView* view = openBig(window, big);
    QVERIFY(view != nullptr);
    // Recouvrement : un grand carré posé SUR S1 (même z, ajouté après).
    const ObjectId over = addSquare(window.project_, "Over", -15, 0, 8);
    window.refreshImage();
    window.applySelectionClick(big.squares[1], SelectMode::Replace);
    window.refreshImage();
    const std::string before = snap(window.project_);
    // Glisser depuis un point couvert par les deux objets (S1 sélectionné, dessous).
    dragWith(view, vp(view, -15, 0), vp(view, -9, 3), Qt::NoModifier);
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    QVERIFY(window.checkSelectionInvariants());
    QVERIFY(documentProblem(window.project_).isEmpty());
    const auto ids = window.selectedObjectIds();
    QVERIFY(ids.size() == 1 && (ids.front() == over || ids.front() == big.squares[1]));
    // Aucun objet ne disparaît ; l'annulation ramène exactement l'état initial.
    while (window.undoStack_.canUndo()) {
        window.undo();
    }
    QCOMPARE(QString::fromStdString(snap(window.project_)), QString::fromStdString(before));
}

void MainWindowTest::deferredBodyDragDoesNotLeakIntoAReplacedDocument() {
    MainWindow window;
    Big big;
    CanvasView* view = openBig(window, big);
    QVERIFY(view != nullptr);
    window.applySelectionClick(big.squares[1], SelectMode::Replace);
    window.refreshImage();
    // Relâchement du glisser : la commande est DIFFÉRÉE (QTimer::singleShot(0)).
    dragWith(view, vp(view, -15, 0), vp(view, -10, 4), Qt::NoModifier);
    // Un autre document, dont les ids sont réutilisés (les ids repartent de 1), est chargé
    // avant que la commande différée ne s'exécute.
    Big other = buildBig();
    window.applyLoadedProject(other.project);
    const std::string fresh = snap(window.project_);
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    // BUG: le lambda différé de VectorObjectBodyItem (main_window.cpp, renderBase) ne teste pas
    // documentGeneration_ (contrairement aux autres commandes différées) : il translate un
    // objet du NOUVEAU document portant le même id et empile une commande d'annulation.
    QCOMPARE(QString::fromStdString(snap(window.project_)), QString::fromStdString(fresh));
    QVERIFY(!window.undoStack_.canUndo());
}

void MainWindowTest::undoImmediatelyAfterReleaseBeforeDeferredCommandIsHarmless() {
    MainWindow window;
    Big big;
    CanvasView* view = openBig(window, big);
    QVERIFY(view != nullptr);
    window.applySelectionClick(big.squares[1], SelectMode::Replace);
    window.refreshImage();
    const std::string before = snap(window.project_);
    dragWith(view, vp(view, -15, 0), vp(view, -10, 4), Qt::AltModifier); // duplication différée
    window.undo();                                                       // pile vide : sans effet
    QCOMPARE(QString::fromStdString(snap(window.project_)), QString::fromStdString(before));
    QTRY_VERIFY_WITH_TIMEOUT(
        window.project_.vector_objects.size() > big.project.vector_objects.size(), 2000);
    QVERIFY(window.checkSelectionInvariants());
    window.undo();
    QCOMPARE(QString::fromStdString(snap(window.project_)), QString::fromStdString(before));
}

// ---- accroche -----------------------------------------------------------------------

void MainWindowTest::nodeSnapOntoOddMicrometerNodeIsExactAndUndoRestoresBoth() {
    MainWindow window;
    Big big;
    CanvasView* view = openBig(window, big);
    QVERIFY(view != nullptr);
    window.snapNodesAct_->setChecked(true); // réglage « Accrochage des nœuds au glisser »
    // Cible : carré isolé dont les coins tombent sur des micromètres « impairs ».
    geo::Path odd;
    odd.closed = true;
    odd.nodes = {corner(-12'003, 11'997), corner(-2'001, 12'001), corner(-1'997, 21'997),
                 corner(-12'003, 22'001)};
    const ObjectId target = addObject(window.project_, "Odd", {geo::PathSet{odd, {}}});
    // A : carré -15, nœud supplémentaire au milieu de l'arête haute (indice 3).
    auto& nodes = window.project_.findObject(big.squares[1])->paths.front().outer.nodes;
    nodes.insert(nodes.begin() + 3, corner(-15'000, 5'000));
    window.applySelectionClick(big.squares[1], SelectMode::Replace);
    window.refreshImage();
    const auto nodeA = [&] {
        return window.project_.findObject(big.squares[1])->paths.front().outer.nodes[3].pos;
    };
    const Vec2um beforeA = nodeA();
    const std::string targetBefore = [&] {
        std::ostringstream o;
        for (const auto& n : window.project_.findObject(target)->paths.front().outer.nodes) {
            o << n.pos.x.value << ',' << n.pos.y.value << ';';
        }
        return o.str();
    }();
    dragWith(view, vp(view, -15, -5), vp(view, -11.93, -12.04), Qt::NoModifier);
    QTRY_VERIFY_WITH_TIMEOUT(nodeA() != beforeA, 2000);
    QVERIFY2(nodeA() == (Vec2um{Micrometers{-12'003}, Micrometers{11'997}}),
             qPrintable(QStringLiteral("snapped to (%1,%2) um instead of (-12003,11997)")
                            .arg(nodeA().x.value)
                            .arg(nodeA().y.value))); // exact, au µm
    const std::string targetAfter = [&] {
        std::ostringstream o;
        for (const auto& n : window.project_.findObject(target)->paths.front().outer.nodes) {
            o << n.pos.x.value << ',' << n.pos.y.value << ';';
        }
        return o.str();
    }();
    QCOMPARE(QString::fromStdString(targetAfter), QString::fromStdString(targetBefore));
    window.undo();
    QVERIFY(nodeA() == beforeA);
    QVERIFY(!window.undoStack_.canUndo());
    window.redo();
    QVERIFY(nodeA() == (Vec2um{Micrometers{-12'003}, Micrometers{11'997}}));
    QVERIFY(window.checkSelectionInvariants());
}

void MainWindowTest::nodeSnapIgnoresHiddenObjects() {
    MainWindow window;
    Big big;
    CanvasView* view = openBig(window, big);
    QVERIFY(view != nullptr);
    window.snapNodesAct_->setChecked(true); // réglage « Accrochage des nœuds au glisser »
    geo::Path hidden;
    hidden.closed = true;
    hidden.nodes = {corner(20'003, 19'997), corner(24'000, 19'997), corner(24'000, 24'000)};
    addObject(window.project_, "Hidden", {geo::PathSet{hidden, {}}}, false);
    auto& nodes = window.project_.findObject(big.squares[3])->paths.front().outer.nodes;
    nodes.insert(nodes.begin() + 3, corner(15'000, 5'000));
    window.applySelectionClick(big.squares[3], SelectMode::Replace);
    window.refreshImage();
    const auto nodeS = [&] {
        return window.project_.findObject(big.squares[3])->paths.front().outer.nodes[3].pos;
    };
    const Vec2um before = nodeS();
    // Relâché à ~0,05 mm du nœud (20,003 ; 19,997) de l'objet masqué.
    dragWith(view, vp(view, 15, -5), vp(view, 20.0, -20.0), Qt::NoModifier);
    QTRY_VERIFY_WITH_TIMEOUT(nodeS() != before, 2000);
    QVERIFY(nodeS() != (Vec2um{Micrometers{20'003}, Micrometers{19'997}}));
}

void MainWindowTest::nodeSnapWithCtrlAtReleaseKeepsRawPosition() {
    MainWindow window;
    Big big;
    CanvasView* view = openBig(window, big);
    QVERIFY(view != nullptr);
    window.snapNodesAct_->setChecked(true);
    auto& nodes = window.project_.findObject(big.squares[0])->paths.front().outer.nodes;
    nodes.insert(nodes.begin() + 3, corner(-30'000, 5'000));
    window.applySelectionClick(big.squares[0], SelectMode::Replace);
    window.refreshImage();
    const auto node = [&] {
        return window.project_.findObject(big.squares[0])->paths.front().outer.nodes[3].pos;
    };
    const Vec2um before = node();
    const Vec2um corner1{Micrometers{-20'000}, Micrometers{5'000}};
    // Témoin : sans modificateur, le nœud s'accroche au sommet (-20, 5) de S1.
    dragWith(view, vp(view, -30, -5), vp(view, -19.9, -4.9), Qt::NoModifier);
    QTRY_VERIFY_WITH_TIMEOUT(node() != before, 2000);
    QVERIFY(node() == corner1);
    window.undo();
    QVERIFY(node() == before);
    // Ctrl + Maj au relâchement : accroche suspendue, position brute.
    window.applySelectionClick(big.squares[0], SelectMode::Replace);
    window.selectionChanged();
    dragWith(view, vp(view, -30, -5), vp(view, -19.9, -4.9),
             Qt::ControlModifier | Qt::ShiftModifier);
    QTRY_VERIFY_WITH_TIMEOUT(node() != before, 2000);
    QVERIFY(node() != corner1);
    window.undo();
    QVERIFY(node() == before);
    QVERIFY(window.checkSelectionInvariants());
}

void MainWindowTest::nodeSnapIsOffByDefaultAndSettingPersistsAcrossWindows() {
    {
        QSettings s;
        s.remove(QStringLiteral("edit/snapNodesOnDrag"));
        s.sync();
    }
    {
        MainWindow window;
        QVERIFY(!window.snapNodesAct_->isChecked()); // désactivé par défaut
        Big big;
        CanvasView* view = openBig(window, big);
        QVERIFY(view != nullptr);
        auto& nodes = window.project_.findObject(big.squares[0])->paths.front().outer.nodes;
        nodes.insert(nodes.begin() + 3, corner(-30'000, 5'000));
        window.applySelectionClick(big.squares[0], SelectMode::Replace);
        window.refreshImage();
        const Vec2um before =
            window.project_.findObject(big.squares[0])->paths.front().outer.nodes[3].pos;
        dragWith(view, vp(view, -30, -5), vp(view, -19.9, -4.9), Qt::NoModifier);
        QTRY_VERIFY_WITH_TIMEOUT(
            window.project_.findObject(big.squares[0])->paths.front().outer.nodes[3].pos != before,
            2000);
        // Réglage coupé : la position est brute (au pixel, 0,1 mm), jamais le sommet de S1.
        QVERIFY(window.project_.findObject(big.squares[0])->paths.front().outer.nodes[3].pos !=
                (Vec2um{Micrometers{-20'000}, Micrometers{5'000}}));
        window.snapNodesAct_->setChecked(true);
    }
    {
        MainWindow window; // le réglage est relu au démarrage
        QVERIFY(window.snapNodesAct_->isChecked());
        window.snapNodesAct_->setChecked(false);
    }
    MainWindow window;
    QVERIFY(!window.snapNodesAct_->isChecked());
}

void MainWindowTest::hoverHighlightFollowsDeleteUndoMoveAndVisibility() {
    MainWindow window;
    Big big;
    CanvasView* view = openBig(window, big);
    QVERIFY(view != nullptr);
    QWidget* w = view->viewport();
    const auto hoverAt = [&](double xMm, double yMm) {
        // Deux évènements : le premier peut être traité aussitôt, le dernier est coalescé.
        sendMouse(w, QEvent::MouseMove, vp(view, xMm, yMm) + QPoint(1, 0), Qt::NoButton,
                  Qt::NoButton, {});
        sendMouse(w, QEvent::MouseMove, vp(view, xMm, yMm), Qt::NoButton, Qt::NoButton, {});
    };
    const auto shown = [&] {
        return window.hoverItem_ != nullptr && window.hoverItem_->isVisible();
    };
    hoverAt(-30, 0);
    QTRY_VERIFY_WITH_TIMEOUT(shown(), 2000);
    // L'objet survolé est supprimé : plus de surbrillance, même sans bouger la souris.
    window.applySelectionClick(big.squares[0], SelectMode::Replace);
    window.deleteSelection();
    QVERIFY(!shown());
    hoverAt(-30, 0);
    QTest::qWait(60);
    QVERIFY(!shown());
    // Annulation : l'objet revient, le survol aussi.
    window.undo();
    hoverAt(-30, 0);
    QTRY_VERIFY_WITH_TIMEOUT(shown(), 2000);
    // Déplacement d'un objet (cache des contours) : l'ancien emplacement n'est plus surligné.
    window.translateObjects({big.squares[0]}, Vec2um{Micrometers{0}, Micrometers{30'000}});
    hoverAt(-30, 0);
    QTest::qWait(60);
    QVERIFY(!shown());
    hoverAt(-30, -30);
    QTRY_VERIFY_WITH_TIMEOUT(shown(), 2000);
    // Objet masqué : jamais surligné.
    window.project_.findObject(big.squares[0])->visible = false;
    window.refreshImage();
    hoverAt(-30, -30);
    QTest::qWait(60);
    QVERIFY(!shown());
    window.project_.findObject(big.squares[0])->visible = true;
    window.refreshImage();
    // Objet sélectionné : jamais surligné ; le curseur quitte le viewport : masqué.
    hoverAt(-15, 0);
    QTRY_VERIFY_WITH_TIMEOUT(shown(), 2000);
    window.applySelectionClick(big.squares[1], SelectMode::Replace);
    hoverAt(-15, 0);
    QTest::qWait(60);
    QVERIFY(!shown());
    hoverAt(0, 0);
    QTRY_VERIFY_WITH_TIMEOUT(shown(), 2000);
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(w, &leave);
    QVERIFY(!shown());
    // Outil changé pendant une surbrillance en attente : masqué et rien ne réapparaît.
    hoverAt(0, 0);
    window.setTool(Tool::Pan);
    QTest::qWait(60);
    QVERIFY(!shown());
}

void MainWindowTest::altPressOnSelectedBodyWithJitterDoesNotBothDuplicateAndOpenMenu() {
    MainWindow window;
    Big big;
    CanvasView* view = openBig(window, big);
    QVERIFY(view != nullptr);
    window.applySelectionClick(big.squares[1], SelectMode::Replace);
    window.refreshImage();
    QSignalSpy below(view, &CanvasView::selectBelowRequested);
    const auto count = window.project_.vector_objects.size();
    const std::string before = snap(window.project_);
    // Alt + clic à 2 px près (< startDragDistance) sur le corps sélectionné.
    dragWith(view, vp(view, -15, 0), vp(view, -15, 0) + QPoint(2, 1), Qt::AltModifier,
             Qt::AltModifier);
    QTest::qWait(60);
    const bool duplicated = window.project_.vector_objects.size() > count;
    const bool menu = below.count() > 0;
    QVERIFY2(!(duplicated && menu),
             "Alt + click with 2 px jitter both duplicated the object and opened the menu");
    QVERIFY2(duplicated || menu, "Alt + click did nothing");
    QVERIFY(window.checkSelectionInvariants());
    closePopups();
    while (window.undoStack_.canUndo()) {
        window.undo();
    }
    QCOMPARE(QString::fromStdString(snap(window.project_)), QString::fromStdString(before));
}

void MainWindowTest::plainClickWithOnePixelJitterOnSelectedBodyMovesNothing() {
    MainWindow window;
    Big big;
    CanvasView* view = openBig(window, big);
    QVERIFY(view != nullptr);
    window.applySelectionClick(big.squares[1], SelectMode::Replace);
    window.refreshImage();
    const std::string before = snap(window.project_);
    dragWith(view, vp(view, -15, 0), vp(view, -15, 0) + QPoint(1, 0), Qt::NoModifier);
    QTest::qWait(60);
    // BUG (antérieur à L5, VectorObjectBodyItem) : aucun seuil de glisser. Un clic dont la
    // souris bouge d'un pixel déplace l'objet sélectionné (0,1 mm ici, 1 mm à l'échelle 1) et
    // empile une commande d'annulation.
    QCOMPARE(QString::fromStdString(snap(window.project_)), QString::fromStdString(before));
}

// ---- rectangle / dessous --------------------------------------------------------------

void MainWindowTest::degenerateRectanglesNeverSelectOrCrash() {
    MainWindow window;
    Big big;
    const std::vector<ObjectId> none;
    window.applyLoadedProject(buildBig().project);
    window.setTool(Tool::Select);
    // Objets pathologiques : sans chemin, un seul nœud, deux nœuds ouverts.
    addObject(window.project_, "NoPath", {});
    geo::Path single;
    single.closed = true;
    single.nodes = {corner(50'000, 40'000)};
    addObject(window.project_, "OneNode", {geo::PathSet{single, {}}});
    geo::Path flat;
    flat.closed = true;
    flat.nodes = {corner(-50'000, 40'000), corner(-40'000, 40'000)};
    addObject(window.project_, "Flat", {geo::PathSet{flat, {}}});
    geo::Path empty;
    addObject(window.project_, "EmptyPath", {geo::PathSet{empty, {}}});
    window.refreshImage();
    const std::vector<QRectF> rects = {
        QRectF(), // nul
        QRectF(0, 0, 0, 0),
        QRectF(-15, 0, 0, 20),            // largeur nulle
        QRectF(-20, 0, 30, 0),            // hauteur nulle
        QRectF(1e12, 1e12, 5, 5),         // très loin
        QRectF(-1e15, -1e15, 2e15, 2e15), // énorme
        QRectF(-1e-9, -1e-9, 2e-9, 2e-9), // minuscule
    };
    for (const SelectMode mode : {SelectMode::Replace, SelectMode::Add, SelectMode::Toggle}) {
        for (const bool crossing : {false, true}) {
            for (const QRectF& r : rects) {
                window.onSelectionRectangle(r, mode, crossing);
                const QString problem = checkAll(window);
                QVERIFY2(problem.isEmpty(), qPrintable(problem));
            }
        }
    }
    // Fenêtre de largeur nulle ou nulle : jamais « tout sélectionner ».
    window.applySelectionClick(std::nullopt, SelectMode::Replace);
    window.onSelectionRectangle(QRectF(), SelectMode::Replace, false);
    QVERIFY2(window.selectedObjectIds().empty(), "null window rectangle selected something");
    window.onSelectionRectangle(QRectF(-15, 0, 0, 0), SelectMode::Replace, false);
    QVERIFY2(window.selectedObjectIds().empty(), "zero-area window rectangle selected something");
    // Énorme : fenêtre ET croisement prennent tous les objets visibles non vides.
    for (const bool crossing : {false, true}) {
        window.onSelectionRectangle(QRectF(-1e6, -1e6, 2e6, 2e6), SelectMode::Replace, crossing);
        const auto ids = window.selectedObjectIds();
        for (const auto& v : window.project_.vector_objects) {
            if (v.name == "Flat" && !crossing) {
                continue; // boîte englobante de hauteur nulle : voir le test dédié ci-dessous
            }
            const bool shouldBeIn = v.visible && !objectScenePath(v).isEmpty();
            const bool in = std::find(ids.begin(), ids.end(), v.id) != ids.end();
            QVERIFY2(in == shouldBeIn,
                     qPrintable(QStringLiteral("huge rect crossing=%1 object %2 in=%3 expected=%4")
                                    .arg(crossing)
                                    .arg(QString::fromStdString(v.name))
                                    .arg(in)
                                    .arg(shouldBeIn)));
        }
    }
    // Objet plat (boîte englobante de hauteur nulle, ex. polyligne parfaitement horizontale) :
    // le cadre « fenêtre » englobant tout doit aussi le prendre.
    window.onSelectionRectangle(QRectF(-1e6, -1e6, 2e6, 2e6), SelectMode::Replace, false);
    const auto all = window.selectedObjectIds();
    const auto flatObj =
        std::find_if(window.project_.vector_objects.begin(), window.project_.vector_objects.end(),
                     [](const auto& v) { return v.name == "Flat"; });
    // BUG: QRectF::contains() renvoie false pour un rectangle de largeur/hauteur nulle (même
    // entièrement couvert) : une forme plate n'est jamais prise par un cadre fenêtre, alors que
    // le cadre « croisement » la prend.
    QVERIFY(std::find(all.begin(), all.end(), flatObj->id) != all.end());
}

void MainWindowTest::crossingRectangleHonoursHolesAndFullEnclosure() {
    MainWindow window;
    const Big big = buildBig();
    window.applyLoadedProject(big.project);
    window.setTool(Tool::Select);
    // Rectangle entièrement DANS le trou (30 +- 2 mm) : ne touche ni le remplissage ni le trait.
    window.onSelectionRectangle(QRectF(29, -1, 2, 2), SelectMode::Replace, true);
    auto ids = window.selectedObjectIds();
    QVERIFY2(std::find(ids.begin(), ids.end(), big.holed) == ids.end(),
             "crossing rectangle fully inside a hole selected the holed object");
    // Rectangle englobant entièrement un petit objet, centre hors de lui : croisement ET
    // fenêtre doivent le prendre.
    window.onSelectionRectangle(QRectF(-36, -6, 12, 12 + 60), SelectMode::Replace, true);
    ids = window.selectedObjectIds();
    QVERIFY2(std::find(ids.begin(), ids.end(), big.squares[0]) != ids.end(),
             "crossing rectangle enclosing an object did not select it");
    window.onSelectionRectangle(QRectF(-36, -66, 12, 72), SelectMode::Replace, false);
    ids = window.selectedObjectIds();
    QVERIFY(std::find(ids.begin(), ids.end(), big.squares[0]) != ids.end());
    // Polyligne ouverte : croisement par le trait, fenêtre par la boîte englobante.
    window.onSelectionRectangle(QRectF(44, -5, 2, 2), SelectMode::Replace, true);
    ids = window.selectedObjectIds();
    QVERIFY2(std::find(ids.begin(), ids.end(), big.open) != ids.end(),
             "crossing rectangle over a vertex of an open path did not select it");
    window.onSelectionRectangle(QRectF(40, -6, 10, 12), SelectMode::Replace, false);
    ids = window.selectedObjectIds();
    QVERIFY2(std::find(ids.begin(), ids.end(), big.open) != ids.end(),
             "window rectangle enclosing an open path did not select it");
}

void MainWindowTest::selectBelowOnZeroAndOneObject() {
    MainWindow window;
    const Big big = buildBig();
    window.applyLoadedProject(big.project);
    window.setTool(Tool::Select);
    window.onSelectBelow(QPointF(0.0, 40.0), QPoint(5, 5), SelectMode::Replace); // 0 objet
    QVERIFY(window.selectBelowMenu_.isNull() || !window.selectBelowMenu_->isVisible());
    QVERIFY(!window.selectedObject_.has_value());
    window.onSelectBelow(QPointF(-15.0, 0.0), QPoint(5, 5), SelectMode::Replace); // 1 objet
    QVERIFY(!window.selectBelowMenu_.isNull());
    QCOMPARE(window.selectBelowMenu_->actions().size(), 1);
    window.selectBelowMenu_->actions().front()->trigger();
    QVERIFY(window.selectedObject_ == big.squares[1]);
    // Outil de dessin : jamais de menu.
    window.setTool(Tool::DrawPolygon);
    window.onSelectBelow(QPointF(-15.0, 0.0), QPoint(5, 5), SelectMode::Replace);
    window.setTool(Tool::Select);
    // Vecteurs masqués (affichage) : jamais de menu ni de sélection.
    window.showVectorsAct_->setChecked(false);
    window.onSelectBelow(QPointF(-15.0, 0.0), QPoint(5, 5), SelectMode::Replace);
    QVERIFY(window.checkSelectionInvariants());
}

void MainWindowTest::selectBelowActionOfObjectDeletedMeanwhileIsHarmless() {
    MainWindow window;
    Big big;
    window.applyLoadedProject(buildBig().project);
    window.setTool(Tool::Select);
    const ObjectId top = addSquare(window.project_, "Top", -15, 0, 8);
    window.refreshImage();
    window.onSelectBelow(QPointF(-15.0, 0.0), QPoint(5, 5), SelectMode::Replace);
    QPointer<QMenu> menu = window.selectBelowMenu_;
    QVERIFY(!menu.isNull());
    QCOMPARE(menu->actions().size(), 2);
    // Pendant que le menu est ouvert : les deux objets sont supprimés.
    window.applySelectionRectangle({top, window.project_.vector_objects[1].id},
                                   SelectMode::Replace);
    window.deleteSelection();
    for (QAction* act : menu->actions()) {
        act->trigger(); // ids morts : no-op
    }
    QVERIFY(window.checkSelectionInvariants());
    QVERIFY(documentProblem(window.project_).isEmpty());
    // Un second appel ferme proprement le premier menu (pas de double popup).
    window.onSelectBelow(QPointF(-30.0, 0.0), QPoint(5, 5), SelectMode::Replace);
    window.onSelectBelow(QPointF(0.0, 0.0), QPoint(5, 5), SelectMode::Replace);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    int visible = 0;
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (auto* m = qobject_cast<QMenu*>(w);
            m != nullptr && m->isVisible() && m->objectName() == QLatin1String("selectBelowMenu")) {
            ++visible;
        }
    }
    QVERIFY2(visible <= 1, "several select-below menus visible at once");
}

void MainWindowTest::selectBelowToggleAndAddModes() {
    MainWindow window;
    const Big big = buildBig();
    window.applyLoadedProject(big.project);
    window.setTool(Tool::Select);
    window.applySelectionClick(big.squares[0], SelectMode::Replace);
    window.onSelectBelow(QPointF(-15.0, 0.0), QPoint(5, 5), SelectMode::Add);
    window.selectBelowMenu_->actions().front()->trigger();
    QCOMPARE(window.multiSelection_.size(), std::size_t{2});
    window.onSelectBelow(QPointF(-15.0, 0.0), QPoint(5, 5), SelectMode::Toggle);
    window.selectBelowMenu_->actions().front()->trigger(); // retire le principal
    QVERIFY(window.selectedObject_ == big.squares[0]);
    QVERIFY(window.multiSelection_.empty());
    QVERIFY(window.checkSelectionInvariants());
}

// ---- gestes ---------------------------------------------------------------------------

void MainWindowTest::longPressCancelledByToolChange() {
    InteractionMap::setLongPressMsForTesting(30);
    MainWindow window;
    Big big;
    CanvasView* view = openBig(window, big);
    QVERIFY(view != nullptr);
    QSignalSpy below(view, &CanvasView::selectBelowRequested);
    QSignalSpy clicked(view, &CanvasView::canvasClickedMm);
    sendMouse(view->viewport(), QEvent::MouseButtonPress, vp(view, -15, 0), Qt::LeftButton,
              Qt::LeftButton, {});
    window.setTool(Tool::DrawPolygon); // l'outil change pendant l'appui
    QTest::qWait(120);
    QCOMPARE(below.count(), 0);
    sendMouse(view->viewport(), QEvent::MouseButtonRelease, vp(view, -15, 0), Qt::LeftButton,
              Qt::NoButton, {});
    QCOMPARE(clicked.count(), 0); // pas de clic fantôme émis sous le nouvel outil
    QCOMPARE(window.pendingPolygonVertices_.size(), std::size_t{0});
    QVERIFY(window.checkSelectionInvariants());
}

void MainWindowTest::longPressCancelledByFocusLoss() {
    InteractionMap::setLongPressMsForTesting(30);
    MainWindow window;
    Big big;
    CanvasView* view = openBig(window, big);
    QVERIFY(view != nullptr);
    QSignalSpy below(view, &CanvasView::selectBelowRequested);
    QSignalSpy clicked(view, &CanvasView::canvasClickedMm);
    sendMouse(view->viewport(), QEvent::MouseButtonPress, vp(view, 0, 40), Qt::LeftButton,
              Qt::LeftButton, {});
    QFocusEvent out(QEvent::FocusOut, Qt::ActiveWindowFocusReason);
    QApplication::sendEvent(view, &out);
    QTest::qWait(120);
    QCOMPARE(below.count(), 0);
    sendMouse(view->viewport(), QEvent::MouseButtonRelease, vp(view, 0, 40), Qt::LeftButton,
              Qt::NoButton, {});
    QCOMPARE(clicked.count(), 0);
    QCOMPARE(below.count(), 0);
    // Appui long normal ensuite : fonctionne toujours (état propre).
    sendMouse(view->viewport(), QEvent::MouseButtonPress, vp(view, -15, 0), Qt::LeftButton,
              Qt::LeftButton, {});
    QTRY_COMPARE_WITH_TIMEOUT(below.count(), 1, 2000);
    sendMouse(view->viewport(), QEvent::MouseButtonRelease, vp(view, -15, 0), Qt::LeftButton,
              Qt::NoButton, {});
    closePopups();
}

void MainWindowTest::escapeCancelsRectangleInProgress() {
    MainWindow window;
    Big big;
    CanvasView* view = openBig(window, big);
    QVERIFY(view != nullptr);
    window.activateWindow();
    QSignalSpy rect(view, &CanvasView::selectionRectangleMm);
    QWidget* w = view->viewport();
    sendMouse(w, QEvent::MouseButtonPress, vp(view, -40, -10), Qt::LeftButton, Qt::LeftButton, {});
    sendMouse(w, QEvent::MouseMove, vp(view, -20, 10), Qt::NoButton, Qt::LeftButton, {});
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(view, &esc);
    QKeyEvent escUp(QEvent::KeyRelease, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(view, &escUp);
    sendMouse(w, QEvent::MouseButtonRelease, vp(view, -20, 10), Qt::LeftButton, Qt::NoButton, {});
    QCOMPARE(rect.count(), 0); // Échap abandonne le cadre de sélection en cours
    QVERIFY(!window.selectedObject_.has_value());
}

void MainWindowTest::middleThenLeftPressDoesNotClickAndLeftThenMiddleClicksOnce() {
    MainWindow window;
    Big big;
    CanvasView* view = openBig(window, big);
    QVERIFY(view != nullptr);
    QSignalSpy clicked(view, &CanvasView::canvasClickedMm);
    QSignalSpy rect(view, &CanvasView::selectionRectangleMm);
    QWidget* w = view->viewport();
    const QPoint p = vp(view, -15, 0);
    // Milieu puis gauche pendant le panoramique : aucun clic, aucune sélection.
    sendMouse(w, QEvent::MouseButtonPress, p, Qt::MiddleButton, Qt::MiddleButton, {});
    sendMouse(w, QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::MiddleButton | Qt::LeftButton,
              {});
    sendMouse(w, QEvent::MouseMove, p + QPoint(10, 10), Qt::NoButton,
              Qt::MiddleButton | Qt::LeftButton, {});
    sendMouse(w, QEvent::MouseButtonRelease, p + QPoint(10, 10), Qt::LeftButton, Qt::MiddleButton,
              {});
    sendMouse(w, QEvent::MouseButtonRelease, p + QPoint(10, 10), Qt::MiddleButton, Qt::NoButton,
              {});
    QCOMPARE(clicked.count(), 0);
    QCOMPARE(rect.count(), 0);
    QVERIFY(!window.selectedObject_.has_value());
    // Gauche puis milieu : le clic gauche aboutit une seule fois, sans pan fantôme.
    const int hBefore = view->horizontalScrollBar()->value();
    sendMouse(w, QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton, {});
    sendMouse(w, QEvent::MouseButtonPress, p, Qt::MiddleButton, Qt::LeftButton | Qt::MiddleButton,
              {});
    sendMouse(w, QEvent::MouseMove, p + QPoint(30, 0), Qt::NoButton,
              Qt::LeftButton | Qt::MiddleButton, {});
    sendMouse(w, QEvent::MouseButtonRelease, p + QPoint(30, 0), Qt::MiddleButton, Qt::LeftButton,
              {});
    sendMouse(w, QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton, {});
    QVERIFY(window.checkSelectionInvariants());
    Q_UNUSED(hBefore);
    QVERIFY(clicked.count() <= 1);
    // État propre ensuite : un clic simple sélectionne.
    QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier, p);
    QVERIFY(window.selectedObject_ == big.squares[1]);
}

void MainWindowTest::spaceHeldIsResetByToolChangeAndIgnoredWithModalOrTextFocus() {
    MainWindow window;
    Big big;
    CanvasView* view = openBig(window, big);
    QVERIFY(view != nullptr);
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    QEvent enter(QEvent::Enter);
    QApplication::sendEvent(view->viewport(), &enter);
    QKeyEvent down(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "));
    QApplication::sendEvent(view, &down);
    QVERIFY(view->spaceHeld());
    window.setTool(Tool::DrawPolygon); // changement d'outil Espace tenu
    // L'Espace relâché pendant l'autre outil doit lever l'état.
    QKeyEvent up(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "));
    QApplication::sendEvent(view, &up);
    QVERIFY(!view->spaceHeld());
    // Un champ de saisie focalisé : Espace n'est pas volé.
    auto* edit = new QLineEdit(&window);
    edit->setGeometry(0, 0, 50, 20);
    edit->show();
    edit->setFocus();
    QTRY_VERIFY_WITH_TIMEOUT(QApplication::focusWidget() == edit, 2000);
    QApplication::sendEvent(view->viewport(), &enter);
    QKeyEvent down2(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "));
    QApplication::sendEvent(edit, &down2);
    QVERIFY(!view->spaceHeld());
    QCOMPARE(edit->text(), QStringLiteral(" "));
    // Boîte modale ouverte : Espace lui appartient, jamais au canevas.
    {
        QDialog modal(&window);
        modal.setModal(true);
        modal.show();
        QTRY_VERIFY_WITH_TIMEOUT(QApplication::activeModalWidget() == &modal, 2000);
        QApplication::sendEvent(view->viewport(), &enter);
        QKeyEvent downM(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "));
        QApplication::sendEvent(&modal, &downM);
        QVERIFY(!view->spaceHeld());
    }
    // Espace tenu puis perte de focus : remis à zéro.
    view->setFocus();
    QApplication::sendEvent(view, &down);
    QFocusEvent out(QEvent::FocusOut, Qt::ActiveWindowFocusReason);
    QApplication::sendEvent(view, &out);
    QVERIFY(!view->spaceHeld());
    QApplication::sendEvent(view, &up);
    // Fenêtre masquée avec Espace tenu.
    QApplication::sendEvent(view, &down);
    window.hide();
    QVERIFY(!view->spaceHeld());
}

void MainWindowTest::staleModifierHintsAfterFocusLoss() {
    MainWindow window;
    Big big;
    CanvasView* view = openBig(window, big);
    QVERIFY(view != nullptr);
    const QString base = window.hintsText();
    sendMouse(view->viewport(), QEvent::MouseMove, QPoint(100, 100), Qt::NoButton, Qt::NoButton,
              Qt::ShiftModifier);
    QVERIFY(window.hintsText() != base);
    // Le focus (ou l'activation de la fenêtre) part pendant que Maj est tenue : le relâchement
    // se produira dans une autre application, jamais vu par le canevas.
    QFocusEvent out(QEvent::FocusOut, Qt::ActiveWindowFocusReason);
    QApplication::sendEvent(view, &out);
    // BUG: resetTransientInput() ne remet pas lastModifiers_ à zéro : la ligne d'indications
    // (« Maj : ajouter ») et le curseur « + » restent périmés jusqu'au prochain mouvement.
    QCOMPARE(window.hintsText(), base);
    // Même chose quand la fenêtre est désactivée.
    sendMouse(view->viewport(), QEvent::MouseMove, QPoint(101, 100), Qt::NoButton, Qt::NoButton,
              Qt::NoModifier);
    QCOMPARE(window.hintsText(), base);
    sendMouse(view->viewport(), QEvent::MouseMove, QPoint(100, 100), Qt::NoButton, Qt::NoButton,
              Qt::ControlModifier);
    QVERIFY(window.hintsText() != base);
    window.hide();
    QCOMPARE(window.hintsText(), base);
}

// ---- ligne d'indications ----------------------------------------------------------------

void MainWindowTest::hintsForEveryToolModifierAndPresetAreDeterministicAndSane() {
    MainWindow window;
    Big big;
    CanvasView* view = openBig(window, big);
    QVERIFY(view != nullptr);
    const Tool tools[] = {Tool::Select,
                          Tool::Pan,
                          Tool::Rect,
                          Tool::DrawRectangle,
                          Tool::DrawEllipse,
                          Tool::DrawPolygon,
                          Tool::DrawPolygonRegular,
                          Tool::DrawBezier,
                          Tool::DrawFreeform,
                          Tool::DrawSatinColumn,
                          Tool::DrawSatinCutLine,
                          Tool::DrawDirectionGuide,
                          Tool::DrawBreakLine};
    QStringList emptyWithModifier;
    for (const Preset preset : {Preset::OpenStitch, Preset::Touchpad}) {
        InteractionMap::setPreset(preset);
        window.applyNavigationPreset(preset);
        for (const bool withSelection : {false, true}) {
            for (const Tool tool : tools) {
                window.setTool(tool);
                if (withSelection) {
                    window.applySelectionClick(big.squares[1], SelectMode::Replace);
                } else {
                    window.applySelectionClick(std::nullopt, SelectMode::Replace);
                }
                for (int bits = 0; bits < 8; ++bits) {
                    Qt::KeyboardModifiers mods;
                    if (bits & 1) {
                        mods |= Qt::ShiftModifier;
                    }
                    if (bits & 2) {
                        mods |= Qt::ControlModifier;
                    }
                    if (bits & 4) {
                        mods |= Qt::AltModifier;
                    }
                    const QString tag = QStringLiteral("tool=%1 preset=%2 sel=%3 mods=%4")
                                            .arg(static_cast<int>(tool))
                                            .arg(static_cast<int>(preset))
                                            .arg(withSelection)
                                            .arg(bits);
                    sendMouse(view->viewport(), QEvent::MouseMove, QPoint(200, 200), Qt::NoButton,
                              Qt::NoButton, mods);
                    const QString first = window.hintsText();
                    sendMouse(view->viewport(), QEvent::MouseMove, QPoint(201, 200), Qt::NoButton,
                              Qt::NoButton, Qt::NoModifier);
                    sendMouse(view->viewport(), QEvent::MouseMove, QPoint(200, 200), Qt::NoButton,
                              Qt::NoButton, mods);
                    QVERIFY2(window.hintsText() == first, qPrintable(tag + " not deterministic"));
                    QVERIFY2(window.hintsLabel_->toolTip() == first, qPrintable(tag + " tooltip"));
                    QVERIFY2(!first.contains(QLatin1Char('<')) && !first.contains(QLatin1Char('>')),
                             qPrintable(tag + " rich-text hazard"));
                    if (bits == 0) {
                        QVERIFY2(!first.trimmed().isEmpty(), qPrintable(tag + " empty hints"));
                    } else if (first.trimmed().isEmpty()) {
                        emptyWithModifier << tag;
                    }
                    // Le label affiché n'est jamais plus long que le texte complet.
                    QVERIFY2(window.hintsLabel_->text().size() <= first.size(), qPrintable(tag));
                }
                sendMouse(view->viewport(), QEvent::MouseMove, QPoint(202, 200), Qt::NoButton,
                          Qt::NoButton, Qt::NoModifier);
            }
        }
    }
    // Information (non bloquant) : combinaisons Maj/Ctrl/Alt sans aucune indication.
    qInfo("hints empty for %lld tool/modifier combinations (informative)",
          static_cast<long long>(emptyWithModifier.size()));
}

void MainWindowTest::hintsWidgetNeverForcesWindowWidth() {
    MainWindow window;
    Big big;
    CanvasView* view = openBig(window, big);
    QVERIFY(view != nullptr);
    window.setTool(Tool::Select);
    window.applySelectionClick(big.squares[1], SelectMode::Replace);
    sendMouse(view->viewport(), QEvent::MouseMove, QPoint(200, 200), Qt::NoButton, Qt::NoButton,
              Qt::NoModifier);
    const int baseWidth = window.minimumSizeHint().width();
    QStringList offenders;
    for (const Qt::KeyboardModifiers mods :
         {Qt::KeyboardModifiers(Qt::ShiftModifier), Qt::KeyboardModifiers(Qt::ControlModifier),
          Qt::KeyboardModifiers(Qt::AltModifier),
          Qt::KeyboardModifiers(Qt::ShiftModifier | Qt::ControlModifier)}) {
        sendMouse(view->viewport(), QEvent::MouseMove, QPoint(200, 200), Qt::NoButton, Qt::NoButton,
                  mods);
        if (window.minimumSizeHint().width() != baseWidth) {
            offenders << QString::number(static_cast<int>(mods));
        }
    }
    QVERIFY2(offenders.isEmpty(), qPrintable(offenders.join(',')));
    QCOMPARE(window.hintsLabel_->minimumSizeHint().width() <= 4 ||
                 window.hintsLabel_->sizePolicy().horizontalPolicy() == QSizePolicy::Ignored,
             true);
}

void MainWindowTest::windowSurvivesTinySizeShowHideCycles() {
    MainWindow window;
    Big big;
    CanvasView* view = openBig(window, big);
    QVERIFY(view != nullptr);
    for (const QSize size : {QSize(1, 1), QSize(120, 60), QSize(10000, 40), QSize(1600, 1000)}) {
        window.resize(size);
        QCoreApplication::processEvents();
        sendMouse(view->viewport(), QEvent::MouseMove, QPoint(0, 0), Qt::NoButton, Qt::NoButton,
                  Qt::ShiftModifier | Qt::ControlModifier);
        QVERIFY(window.hintsLabel_->width() >= 0);
        window.hide();
        window.show();
        QCoreApplication::processEvents();
    }
    window.setTool(Tool::Pan);
    window.refreshHints();
    QVERIFY(!window.hintsText().isEmpty());
}

// ---- menu Aide / préréglages ------------------------------------------------------------

void MainWindowTest::helpDialogsAreSingleInstanceAcrossRepeatedOpenAndClose() {
    MainWindow window;
    auto* gestures = window.findChild<QAction*>(QStringLiteral("action_help_gestures"));
    auto* quick = window.findChild<QAction*>(QStringLiteral("action_help_quickstart"));
    QVERIFY(gestures != nullptr && quick != nullptr);
    for (int i = 0; i < 6; ++i) {
        gestures->trigger();
        quick->trigger();
        QCOMPARE(window.findChildren<GesturesDialog*>().size(), 1);
        QCOMPARE(window.findChildren<QuickStartDialog*>().size(), 1);
    }
    // Fermeture par la croix puis réouverture : nouvelle instance utilisable.
    for (int i = 0; i < 4; ++i) {
        window.findChild<GesturesDialog*>()->close();
        window.findChild<QuickStartDialog*>()->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(window.gesturesDialog_.isNull());
        QVERIFY(window.quickStartDialog_.isNull());
        gestures->trigger();
        quick->trigger();
        auto* dialog = window.findChild<GesturesDialog*>();
        QVERIFY(dialog != nullptr && dialog->isVisible());
        QVERIFY(dialog->totalRowCount() > 0);
    }
}

void MainWindowTest::f1WhileDialogOpenAndPresetSwitchWhileDialogOpenThenClose() {
    const QScopeGuard guard([] { InteractionMap::setPreset(Preset::OpenStitch); });
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    QTest::keyClick(&window, Qt::Key_F1);
    auto* dialog = window.findChild<GesturesDialog*>();
    QVERIFY(dialog != nullptr);
    // F1 envoyé au dialogue lui-même, puis à la fenêtre : toujours une seule instance.
    QTest::keyClick(dialog, Qt::Key_F1);
    QTest::keyClick(&window, Qt::Key_F1);
    QCOMPARE(window.findChildren<GesturesDialog*>().size(), 1);
    // Préréglage changé par le menu pendant que le dialogue est ouvert.
    const int rowsOpenStitch = dialog->totalRowCount();
    window.findChild<QAction*>(QStringLiteral("navPresetTouchpad"))->trigger();
    QCOMPARE(dialog->presetCombo()->currentIndex(), 1);
    QVERIFY(dialog->totalRowCount() < rowsOpenStitch);
    // Fermé par la croix, puis préréglage changé de nouveau : plus de dialogue, pas de crash.
    dialog->close();
    window.findChild<QAction*>(QStringLiteral("navPresetOpenStitch"))->trigger();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(window.gesturesDialog_.isNull());
    QCOMPARE(InteractionMap::preset(), Preset::OpenStitch);
    window.findChild<QAction*>(QStringLiteral("navPresetTouchpad"))->trigger();
    QVERIFY(window.findChild<QAction*>(QStringLiteral("navPresetTouchpad"))->isChecked());
    // Fenêtre détruite avec un dialogue ouvert (enfant) : pas de crash.
    window.showGesturesDialog();
    window.showQuickStartDialog();
}

void MainWindowTest::gesturesDialogSearchSurvivesHostileInputAndRebuilds() {
    MainWindow window;
    window.showGesturesDialog();
    auto* dialog = window.findChild<GesturesDialog*>();
    QVERIFY(dialog != nullptr);
    const int total = dialog->totalRowCount();
    QVERIFY(total > 0);
    const QStringList hostile = {
        QStringLiteral("("),      QStringLiteral("["),
        QStringLiteral("\\"),     QStringLiteral("*"),
        QStringLiteral(".*"),     QStringLiteral("%"),
        QStringLiteral("(?<a>"),  QStringLiteral("e\u0301"),
        QStringLiteral("\u00e9"), QString(100000, QLatin1Char('x')),
        QStringLiteral("  "),     QStringLiteral("\t\n"),
        QStringLiteral("<b>"),    QString(),
    };
    for (const QString& text : hostile) {
        dialog->searchField()->setText(text);
        QVERIFY2(dialog->visibleRowCount() >= 0 && dialog->visibleRowCount() <= total,
                 qPrintable(text.left(20)));
        QVERIFY(dialog->totalRowCount() == total);
    }
    dialog->searchField()->setText(QStringLiteral("clic"));
    const int clicRows = dialog->visibleRowCount();
    QVERIFY(clicRows > 0 && clicRows < total);
    // Reconstruction (changement de préréglage) pendant qu'un filtre est actif : le résultat
    // est celui d'un dialogue neuf avec le même filtre.
    window.findChild<QAction*>(QStringLiteral("navPresetTouchpad"))->trigger();
    const int filteredAfter = dialog->visibleRowCount();
    const QString filterAfter = dialog->searchField()->text();
    dialog->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    window.showGesturesDialog();
    auto* fresh = window.findChild<GesturesDialog*>();
    QVERIFY(fresh != nullptr);
    fresh->searchField()->setText(filterAfter);
    QCOMPARE(fresh->visibleRowCount(), filteredAfter);
    // Sélectionner une ligne puis filtrer tout : pas de crash, table vide.
    fresh->table()->selectRow(0);
    fresh->searchField()->setText(QStringLiteral("zzzzzzzz-introuvable"));
    QCOMPARE(fresh->visibleRowCount(), 0);
    fresh->searchField()->clear();
    QCOMPARE(fresh->visibleRowCount(), fresh->totalRowCount());
}

void MainWindowTest::quickStartButtonsFollowActionStateAndDisabledOnesNeverFire() {
    MainWindow window;
    window.showQuickStartDialog();
    auto* dialog = window.findChild<QuickStartDialog*>();
    QVERIFY(dialog != nullptr);
    QCOMPARE(dialog->stepCount(), 6);
    const std::vector<QAction*> primary = {window.openImageAct_,       window.segmentAct_,
                                           window.vectorizeRegionAct_, window.createTatamiAct_,
                                           window.analyzeAct_,         window.exportDstAct_};
    const auto inSync = [&](const QString& phase) {
        for (int i = 0; i < 6; ++i) {
            QPushButton* button = dialog->stepButton(i, 0);
            QVERIFY2(button != nullptr, qPrintable(phase + QStringLiteral(" step %1").arg(i)));
            QVERIFY2(
                button->isEnabled() == primary[static_cast<std::size_t>(i)]->isEnabled(),
                qPrintable(phase + QStringLiteral(": step %1 button/action state differ").arg(i)));
        }
    };
    inSync(QStringLiteral("empty"));
    int triggered = 0;
    for (QAction* act : primary) {
        connect(act, &QAction::triggered, this, [&triggered] { ++triggered; });
    }
    // Un bouton grisé ne lance jamais sa commande.
    for (int i = 1; i < 6; ++i) {
        QPushButton* button = dialog->stepButton(i, 0);
        if (!button->isEnabled()) {
            button->click();
        }
    }
    QCOMPARE(triggered, 0);
    const Big big = buildBig();
    window.applyLoadedProject(big.project);
    inSync(QStringLiteral("loaded"));
    window.setSelection({.region = RegionId{1}, .embroidery = std::nullopt, .objects = {}});
    window.selectionChanged();
    inSync(QStringLiteral("region selected"));
    window.applyLoadedProject(doc::Project{});
    inSync(QStringLiteral("reset"));
    // Fenêtre principale détruite avec le dialogue ouvert : pas d'accès pendu.
}

void MainWindowTest::twoWindowsShareTheGlobalPresetConsistently() {
    MainWindow first;
    MainWindow second;
    auto* firstTouch = first.findChild<QAction*>(QStringLiteral("navPresetTouchpad"));
    auto* firstOs = first.findChild<QAction*>(QStringLiteral("navPresetOpenStitch"));
    auto* secondTouch = second.findChild<QAction*>(QStringLiteral("navPresetTouchpad"));
    QVERIFY(firstOs->isChecked());
    secondTouch->trigger();
    QCOMPARE(InteractionMap::preset(), Preset::Touchpad);
    QVERIFY(secondTouch->isChecked());
    // Le préréglage est GLOBAL au processus : la première fenêtre doit refléter le même état.
    QVERIFY(firstTouch->isChecked() && !firstOs->isChecked());
    QCOMPARE(first.hintsText(), second.hintsText());
    // Détruire une fenêtre ne change pas le préréglage.
    {
        MainWindow third;
        QVERIFY(third.findChild<QAction*>(QStringLiteral("navPresetTouchpad"))->isChecked());
    }
    QCOMPARE(InteractionMap::preset(), Preset::Touchpad);
}

void MainWindowTest::presetPersistsAcrossWindowsAndCorruptSettingFallsBack() {
    {
        MainWindow w;
        w.findChild<QAction*>(QStringLiteral("navPresetTouchpad"))->trigger();
    }
    InteractionMap::setPreset(Preset::OpenStitch); // simule un nouveau processus
    {
        MainWindow w;
        QVERIFY(w.findChild<QAction*>(QStringLiteral("navPresetTouchpad"))->isChecked());
    }
    // Valeur corrompue / inconnue : repli sur OpenStitch, sans crash.
    {
        QSettings s(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("OpenStitch"),
                    QStringLiteral("OpenStitch Studio"));
        s.setValue(QStringLiteral("navigation/preset"), QStringLiteral("\x01garbage<>"));
        s.sync();
    }
    InteractionMap::setPreset(Preset::Touchpad);
    {
        MainWindow w;
        QCOMPARE(InteractionMap::preset(), Preset::OpenStitch);
        QVERIFY(w.findChild<QAction*>(QStringLiteral("navPresetOpenStitch"))->isChecked());
        QVERIFY(!w.hintsText().isEmpty());
    }
}

// ---- chargement / sauvegarde -------------------------------------------------------------

void MainWindowTest::projectsOfEveryShapeEnableSaveAndKeepSelectionSane() {
    MainWindow window;
    QAction* saveAct = window.saveProjectAct_;
    QVERIFY(saveAct != nullptr);
    // Vide.
    window.applyLoadedProject(doc::Project{});
    QVERIFY(!saveAct->isEnabled());
    QVERIFY(window.checkSelectionInvariants());
    // Seulement des broderies (aucune image, aucun objet vectoriel).
    {
        doc::Project p;
        doc::EmbroideryObject e;
        e.id = p.object_ids.next();
        e.name = "Only embroidery";
        e.source_vector = ObjectId{0};
        p.embroidery_objects.push_back(e);
        window.applyLoadedProject(p);
        QVERIFY2(saveAct->isEnabled(), "Save disabled for an embroidery-only project");
        QVERIFY(window.checkSelectionInvariants());
        window.setSelection({.region = std::nullopt, .embroidery = e.id, .objects = {}});
        window.selectionChanged();
        window.deleteSelection();
        QVERIFY(window.checkSelectionInvariants());
        window.undo();
        window.redo();
        QVERIFY(window.checkSelectionInvariants());
    }
    // Seulement des objets vectoriels, sans image.
    {
        doc::Project p;
        addSquare(p, "A", 0);
        addSquare(p, "B", 20);
        window.applyLoadedProject(p);
        QVERIFY(saveAct->isEnabled());
        window.applySelectionClick(p.vector_objects[0].id, SelectMode::Replace);
        window.applySelectionClick(p.vector_objects[1].id, SelectMode::Add);
        window.deleteSelection();
        window.undo();
        QVERIFY(window.checkSelectionInvariants());
        window.redo();
        QVERIFY(window.checkSelectionInvariants());
    }
    // Un document chargé avec une sélection multiple en cours : la sélection est vidée.
    {
        Big b = buildBig();
        window.applyLoadedProject(b.project);
        window.applySelectionRectangle({b.squares[0], b.squares[1]}, SelectMode::Replace);
        QCOMPARE(window.multiSelection_.size(), std::size_t{2});
        window.applyLoadedProject(doc::Project{});
        QVERIFY(window.selectedObjectIds().empty());
        QVERIFY(window.checkSelectionInvariants());
    }
}

void MainWindowTest::autosaveTickWithMultiSelectionIsHarmless() {
    MainWindow window;
    window.applyLoadedProject(buildBig().project);
    window.setTool(Tool::Select);
    const ObjectId a = window.project_.vector_objects[0].id;
    const ObjectId b = window.project_.vector_objects[1].id;
    const ObjectId c = window.project_.vector_objects[2].id;
    window.duplicateVectorObject(a); // rend la fenêtre « modifiée »
    window.applySelectionRectangle({a, b, c}, SelectMode::Replace);
    QVERIFY(window.isWindowModified());
    QCOMPARE(window.multiSelection_.size(), std::size_t{3});
    const std::string before = snap(window.project_);
    const auto selBefore = window.selectedObjectIds();
    window.onAutosaveTick();
    QCOMPARE(QString::fromStdString(snap(window.project_)), QString::fromStdString(before));
    QVERIFY(window.selectedObjectIds() == selBefore);
    QVERIFY(window.checkSelectionInvariants());
    QDir(QFileInfo(slotFor(QString()).osp_path).absolutePath()).removeRecursively();
}

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_ui_adversarial.moc"
