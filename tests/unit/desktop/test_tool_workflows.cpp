// SPDX-License-Identifier: Apache-2.0
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QLineF>
#include <QListWidget>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "canvas_view.hpp"
#include "font_catalog.hpp"
#include "main_window.hpp"
#include "node_handle.hpp"
#include "openstitch/document/color_blend.hpp"
#include "openstitch/geometry/primitives.hpp"
#include "satin_guide_item.hpp"
#include "text_dialog.hpp"

namespace openstitch::desktop {
namespace {

// Inventaire fermé de tools.hpp : chaque entrée a un scénario d'effet ci-dessous.
// Select -> sélection/multi-sélection/déplacement/suppression ; Pan -> vue seulement ;
// Rect -> pipeline raster ; DrawRectangle/DrawEllipse/DrawPolygonRegular -> boxTools ;
// DrawPolygon/DrawBezier/DrawSatinColumn -> clickTools ; DrawFreeform -> freeform ;
// DrawDirectionGuide/DrawBreakLine -> directionTools ; Cut -> knife ; Text -> textTool.
constexpr std::array kTools{Tool::Select,
                            Tool::Pan,
                            Tool::Rect,
                            Tool::DrawRectangle,
                            Tool::DrawEllipse,
                            Tool::DrawPolygon,
                            Tool::DrawPolygonRegular,
                            Tool::DrawBezier,
                            Tool::DrawFreeform,
                            Tool::DrawSatinColumn,
                            Tool::DrawDirectionGuide,
                            Tool::DrawBreakLine,
                            Tool::Cut,
                            Tool::Text};
static_assert(kTools.size() == static_cast<std::size_t>(Tool::Text) + 1,
              "Ajouter un scenario d'effet pour tout nouvel outil");

enum class EditMode {
    Node,
    Stitch,
    SatinGuide,
    SatinRail,
    DirectionGuide,
    BreakLine,
    SatinUnified
};

Vec2um um(int x, int y) {
    return {Micrometers{x}, Micrometers{y}};
}

Vec2um modelPoint(QPointF sceneMm) {
    return {to_micrometers(Millimeters{sceneMm.x()}), to_micrometers(Millimeters{-sceneMm.y()})};
}

struct Fixture {
    document::Project project;
    ObjectId vector{};
    ObjectId embroidery{};
};

ObjectId addRectangle(document::Project& project, int centerX = 0) {
    document::VectorObject vector;
    vector.id = project.object_ids.next();
    vector.name = "Workflow rectangle";
    vector.rgb = {80, 120, 200};
    auto path = geometry::rectangle_path(um(centerX - 8'000, -6'000), um(centerX + 8'000, 6'000));
    vector.paths.push_back({std::move(path), {}});
    const ObjectId id = vector.id;
    project.vector_objects.push_back(std::move(vector));
    return id;
}

Fixture editFixture(EditMode mode) {
    Fixture fx;
    fx.vector = addRectangle(fx.project);
    auto& outline = fx.project.vector_objects.front().paths.front().outer;
    // Nœud intérieur d'un bord : les coins de boîte sont recouverts par les poignées
    // de redimensionnement. Ce nœud permet un vrai hit-test de NodeHandleItem.
    outline.nodes = {{um(-8'000, -6'000)},
                     {um(0, -6'000)},
                     {um(8'000, -6'000)},
                     {um(8'000, 6'000)},
                     {um(-8'000, 6'000)}};
    if (mode == EditMode::Node) {
        return fx;
    }
    document::EmbroideryObject embroidery;
    embroidery.id = fx.project.object_ids.next();
    embroidery.name = "Workflow embroidery";
    embroidery.source_vector = fx.vector;
    embroidery.rgb = {80, 120, 200};
    fx.embroidery = embroidery.id;
    if (mode == EditMode::Stitch) {
        embroidery.params = document::RunningStitchParams{};
    } else if (mode == EditMode::DirectionGuide || mode == EditMode::BreakLine) {
        document::DirectionalFillParams params;
        params.row_spacing = Micrometers{1'000};
        params.underlay_edge = false;
        params.underlay_parallel = false;
        geometry::Path path;
        path.closed = false;
        if (mode == EditMode::BreakLine) {
            path.nodes = {{um(0, -8'000)}, {um(0, 0)}, {um(0, 8'000)}};
            params.break_lines.push_back(path); // traverse réellement la forme
        } else {
            path.nodes = {{um(-5'000, 0)}, {um(0, 0)}, {um(5'000, 0)}};
            params.guides.push_back(path);
        }
        embroidery.params = params;
    } else {
        document::SatinParams params;
        params.rail_a.closed = false;
        params.rail_b.closed = false;
        params.rail_a.nodes = {{um(-8'000, 0)}, {um(8'000, 0)}};
        params.rail_b.nodes = {{um(-8'000, 4'000)}, {um(8'000, 4'000)}};
        params.rungs = {{um(-8'000, 0), um(-8'000, 4'000)},
                        {um(0, 0), um(0, 4'000)},
                        {um(8'000, 0), um(8'000, 4'000)}};
        embroidery.params = params;
        // Proxy valide : source_vector existe, mais les rails sont la géométrie réelle.
        fx.project.vector_objects.front().visible = false;
    }
    fx.project.embroidery_objects.push_back(std::move(embroidery));
    return fx;
}

document::Project imageFixture() {
    document::Project project;
    project.original.width = 64;
    project.original.height = 64;
    project.original.rgba.assign(64 * 64 * 4, 255);
    project.mm_per_px = Millimeters{0.5};
    return project;
}

// Evènements du viewport, et non invocation des signaux finaux. Les boutons/modificateurs
// sont explicites : aucun état souris global ni déplacement du curseur système requis.
void mouse(QWidget* viewport, QEvent::Type type, QPoint point, Qt::MouseButton button,
           Qt::MouseButtons held, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QMouseEvent event(type, QPointF(point), QPointF(viewport->mapToGlobal(point)), button, held,
                      modifiers);
    QApplication::sendEvent(viewport, &event);
}

void drag(CanvasView& view, QPoint start, QPoint end,
          Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    mouse(view.viewport(), QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton,
          modifiers);
    mouse(view.viewport(), QEvent::MouseMove, (start + end) / 2, Qt::NoButton, Qt::LeftButton,
          modifiers);
    mouse(view.viewport(), QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton, modifiers);
    mouse(view.viewport(), QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton,
          modifiers);
}

NodeHandleItem* handleAt(const QList<QGraphicsItem*>& items, QPointF scenePoint) {
    for (auto* item : items) {
        auto* handle = dynamic_cast<NodeHandleItem*>(item);
        if (handle != nullptr && QLineF(handle->scenePos(), scenePoint).length() < 0.001) {
            return handle;
        }
    }
    return nullptr;
}

} // namespace

// Même seam privé que test_main_window/test_lettering_ui, dans un exécutable séparé.
class MainWindowTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir settingsDir_;

    static QAction* toolAction(MainWindow& window, Tool tool) {
        switch (tool) {
        case Tool::Select:
            return window.toolSelectAct_;
        case Tool::Pan:
            return window.toolPanAct_;
        case Tool::Rect:
            return window.toolRectAct_;
        case Tool::DrawRectangle:
            return window.toolDrawRectAct_;
        case Tool::DrawEllipse:
            return window.toolDrawEllipseAct_;
        case Tool::DrawPolygon:
            return window.toolDrawPolygonAct_;
        case Tool::DrawPolygonRegular:
            return window.toolDrawPolygonRegularAct_;
        case Tool::DrawBezier:
            return window.toolDrawBezierAct_;
        case Tool::DrawFreeform:
            return window.toolDrawFreeformAct_;
        case Tool::DrawSatinColumn:
            return window.toolDrawSatinColumnAct_;
        case Tool::DrawDirectionGuide:
            return window.drawDirectionGuideAct_;
        case Tool::DrawBreakLine:
            return window.drawBreakLineAct_;
        case Tool::Cut:
            return window.toolCutAct_;
        case Tool::Text:
            return window.toolTextAct_;
        }
        return nullptr;
    }

    static void expose(MainWindow& window) {
        window.resize(1200, 850);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.activateWindow();
        window.view_->setFocus();
        QCoreApplication::processEvents();
        frame(window);
    }

    static void frame(MainWindow& window, double scale = 16.0) {
        // Le dock contextuel peut changer de taille après applyLoadedProject : stabiliser
        // le layout avant de convertir les coordonnées scène en coordonnées du viewport.
        QCoreApplication::processEvents();
        window.view_->resetTransform();
        window.view_->scale(scale, scale);
        window.view_->centerOn(QPointF(0, 0));
    }

    static void activate(MainWindow& window, Tool tool) {
        auto* action = toolAction(window, tool);
        QVERIFY(action != nullptr);
        QVERIFY2(action->isEnabled(), qPrintable(action->toolTip()));
        if (auto* button =
                qobject_cast<QToolButton*>(window.toolPalette_->widgetForAction(action))) {
            QVERIFY(button->isVisible());
            QVERIFY(button->isEnabled());
            QTest::mouseClick(button, Qt::LeftButton);
        } else {
            action->trigger(); // guides/ruptures : actions du menu Broderie
        }
        QCOMPARE(window.currentTool_, tool);
        for (Tool other : kTools) {
            auto* otherAction = toolAction(window, other);
            if (otherAction->isCheckable()) {
                QCOMPARE(otherAction->isChecked(), other == tool);
            }
        }
        window.view_->setFocus();
    }

    static void click(MainWindow& window, QPointF scenePoint,
                      Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        const QPoint point = window.view_->mapFromScene(scenePoint);
        QVERIFY(window.view_->viewport()->rect().contains(point));
        QTest::mouseClick(window.view_->viewport(), Qt::LeftButton, modifiers, point);
    }

    static void dragScene(MainWindow& window, QPointF start, QPointF end,
                          Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        const QPoint a = window.view_->mapFromScene(start);
        const QPoint b = window.view_->mapFromScene(end);
        QVERIFY(window.view_->viewport()->rect().contains(a));
        QVERIFY(window.view_->viewport()->rect().contains(b));
        QVERIFY((b - a).manhattanLength() > QApplication::startDragDistance());
        drag(*window.view_, a, b, modifiers);
    }

    static void selectEmbroidery(MainWindow& window, ObjectId id) {
        window.orderDock_->show();
        window.orderDock_->raise();
        QCoreApplication::processEvents();
        const auto& objects = window.project_.embroidery_objects;
        auto it = std::find_if(objects.begin(), objects.end(),
                               [id](const auto& e) { return e.id == id; });
        QVERIFY(it != objects.end());
        const int row = static_cast<int>(it - objects.begin());
        auto* item = window.orderList_->item(row);
        QVERIFY(item != nullptr);
        window.orderList_->scrollToItem(item);
        const QPoint point = window.orderList_->visualItemRect(item).center();
        QVERIFY(window.orderList_->viewport()->rect().contains(point));
        QTest::mouseClick(window.orderList_->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QCOMPARE(window.selectedEmbroidery_, std::optional<ObjectId>{id});
        QVERIFY(window.selectedObjectIds().empty());
        window.view_->setFocus();
        frame(window);
    }

    static QAction* editAction(MainWindow& window, EditMode mode) {
        switch (mode) {
        case EditMode::Node:
            return nullptr; // implicite avec une seule forme sélectionnée
        case EditMode::Stitch:
            return window.stitchEditModeAct_;
        case EditMode::SatinGuide:
            return window.satinGuideModeAct_;
        case EditMode::SatinRail:
            return window.railEditModeAct_;
        case EditMode::DirectionGuide:
        case EditMode::BreakLine:
            return window.directionGuideModeAct_;
        case EditMode::SatinUnified:
            return window.satinEditModeAct_;
        }
        return nullptr;
    }

    static void enterEdit(MainWindow& window, const Fixture& fx, EditMode mode) {
        if (mode == EditMode::Node) {
            click(window, QPointF(0, 0));
            QCOMPARE(window.selectedObject_, std::optional<ObjectId>{fx.vector});
        } else {
            auto* action = editAction(window, mode);
            QVERIFY(!action->isEnabled()); // aucune cible au chargement
            selectEmbroidery(window, fx.embroidery);
            QVERIFY(!QTest::currentTestFailed());
            QVERIFY(action->isEnabled());
            action->trigger();
            QVERIFY(action->isChecked());
        }
    }

    static QPointF editablePoint(MainWindow& window, EditMode mode) {
        switch (mode) {
        case EditMode::Node:
            return QPointF(0, 6);
        case EditMode::Stitch:
            // Choisir une vraie poignée via son rôle, jamais un nœud du vecteur source.
            for (auto* item : window.baseItems_) {
                auto* handle = dynamic_cast<NodeHandleItem*>(item);
                if (handle != nullptr &&
                    handle->toolTip().startsWith(QStringLiteral("Point de couture #")) &&
                    window.view_->itemAt(window.view_->mapFromScene(handle->scenePos())) ==
                        handle) {
                    return handle->scenePos();
                }
            }
            return QPointF(1000, 1000); // l'assertion du hit-test nomme l'échec
        case EditMode::SatinGuide:
        case EditMode::SatinUnified:
            return QPointF(0, 0);
        case EditMode::SatinRail:
            return QPointF(-8, 0);
        case EditMode::DirectionGuide:
            return QPointF(-5, 0);
        case EditMode::BreakLine:
            return QPointF(0, 0);
        }
        return {};
    }

    static void addEditRows(bool unified = true) {
        QTest::addColumn<int>("modeValue");
        QTest::newRow("vector-node") << static_cast<int>(EditMode::Node);
        QTest::newRow("stitch-point") << static_cast<int>(EditMode::Stitch);
        QTest::newRow("satin-guide") << static_cast<int>(EditMode::SatinGuide);
        QTest::newRow("satin-rail") << static_cast<int>(EditMode::SatinRail);
        QTest::newRow("direction-guide") << static_cast<int>(EditMode::DirectionGuide);
        QTest::newRow("break-line") << static_cast<int>(EditMode::BreakLine);
        if (unified) {
            QTest::newRow("satin-unified") << static_cast<int>(EditMode::SatinUnified);
        }
    }

    static std::size_t pendingCount(const MainWindow& window) {
        return window.pendingPolygonVertices_.size() + window.pendingBezierNodes_.size() +
               window.pendingSatinPoints_.size() + window.pendingGuidePoints_.size() +
               window.pendingFreeformPoints_.size();
    }

    static void armTextDialog(MainWindow& window, const QString& text, bool accept, bool& seen,
                              bool& controlsValid) {
        QTimer::singleShot(0, &window, [text, accept, &seen, &controlsValid] {
            auto* dialog = qobject_cast<TextDialog*>(QApplication::activeModalWidget());
            seen = dialog != nullptr;
            if (dialog == nullptr) {
                if (auto* unexpected = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
                    unexpected->reject();
                }
                return;
            }
            auto* edit = dialog->findChild<QPlainTextEdit*>(QStringLiteral("text_edit"));
            auto* font = dialog->findChild<QComboBox*>(QStringLiteral("font_combo"));
            auto* fill = dialog->findChild<QComboBox*>(QStringLiteral("fill_combo"));
            auto* buttons = dialog->findChild<QDialogButtonBox*>();
            controlsValid =
                edit != nullptr && font != nullptr && fill != nullptr && buttons != nullptr;
            if (!controlsValid) {
                dialog->reject();
                return;
            }
            if (accept) {
                document::TextFontRef builtin;
                builtin.builtin = "vera-sans-bold";
                const int index = FontCatalog::instance().indexOf(builtin);
                const int comboIndex = font->findData(index);
                controlsValid = index >= 0 && comboIndex >= 0;
                if (!controlsValid) {
                    dialog->reject();
                    return;
                }
                font->setCurrentIndex(comboIndex);
                fill->setCurrentIndex(
                    fill->findData(static_cast<int>(document::TextFill::Contour)));
                edit->setPlainText(text);
                // Le rafraîchissement normal est temporisé à 80 ms. Le provoquer sans sleep
                // ne remplace pas le geste : la validation passe bien par le bouton du dialogue.
                controlsValid = QMetaObject::invokeMethod(dialog, "refresh", Qt::DirectConnection);
                auto* ok = buttons->button(QDialogButtonBox::Ok);
                controlsValid = controlsValid && ok->isEnabled();
                if (!controlsValid) {
                    dialog->reject();
                    return;
                }
                QTest::mouseClick(ok, Qt::LeftButton);
            } else {
                QTest::mouseClick(buttons->button(QDialogButtonBox::Cancel), Qt::LeftButton);
            }
        });
    }

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchToolWorkflowsTest"));
        // AppDataLocation/autosave et réglages uniques : aucun créneau de l'utilisateur
        // ni d'un ancien passage du binaire ne peut déclencher une récupération modale.
        QCoreApplication::setApplicationName(
            QStringLiteral("ToolWorkflows-%1-%2")
                .arg(QCoreApplication::applicationPid())
                .arg(settingsDir_.path().section(QLatin1Char('/'), -1)));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
        FontCatalog::instance().setSystemFontDirsForTesting({});
        document::TextFontRef builtin;
        builtin.builtin = "vera-sans-bold";
        QString fontError;
        QVERIFY2(FontCatalog::instance().load(builtin, &fontError) != nullptr,
                 qPrintable(fontError));
    }

    void init() {
        QSettings().clear();
        InteractionMap::setPreset(Preset::OpenStitch);
    }

    void selectionClickRectangleNudgeDeleteRoundTrip() {
        MainWindow window;
        document::Project project;
        const ObjectId a = addRectangle(project, -12'000);
        const ObjectId b = addRectangle(project, 12'000);
        window.applyLoadedProject(project);
        expose(window);
        QVERIFY(!QTest::currentTestFailed());
        frame(window, 10.0); // deux formes + rectangle englobant dans le viewport étroit
        activate(window, Tool::Select);
        click(window, QPointF(-12, 0));
        QCOMPARE(window.selectedObjectIds(), (std::vector<ObjectId>{a}));
        click(window, QPointF(12, 0), Qt::ShiftModifier);
        QCOMPARE(window.selectedObjectIds(), (std::vector<ObjectId>{a, b}));
        click(window, QPointF(-12, 0), Qt::ControlModifier);
        QCOMPARE(window.selectedObjectIds(), (std::vector<ObjectId>{b}));
        click(window, QPointF(0, 12));
        QVERIFY(window.selectedObjectIds().empty());
        dragScene(window, QPointF(-22, -8), QPointF(22, 8));
        QCOMPARE(window.selectedObjectIds().size(), std::size_t{2});
        QVERIFY(window.checkSelectionInvariants());
        QVERIFY(!window.undoStack_.canUndo()); // sélectionner ne modifie pas le document

        const auto originalA = window.project_.findObject(a)->paths;
        const auto originalB = window.project_.findObject(b)->paths;
        QTest::keyClick(window.view_, Qt::Key_Right);
        QCOMPARE(window.undoStack_.undoNames().size(), std::size_t{1});
        const auto movedA = window.project_.findObject(a)->paths;
        const auto movedB = window.project_.findObject(b)->paths;
        QCOMPARE(movedA.front().outer.nodes.front().pos,
                 originalA.front().outer.nodes.front().pos + um(100, 0));
        QCOMPARE(movedB.front().outer.nodes.front().pos,
                 originalB.front().outer.nodes.front().pos + um(100, 0));
        window.undoAct_->trigger();
        QCOMPARE(window.project_.findObject(a)->paths, originalA);
        QCOMPARE(window.project_.findObject(b)->paths, originalB);
        window.redoAct_->trigger();
        QCOMPARE(window.project_.findObject(a)->paths, movedA);
        QCOMPARE(window.project_.findObject(b)->paths, movedB);

        QVERIFY(window.deleteSelectionAct_->isEnabled());
        QTest::keyClick(window.view_, Qt::Key_Delete);
        QVERIFY(window.project_.vector_objects.empty());
        QVERIFY(window.selectedObjectIds().empty());
        QVERIFY(!window.deleteSelectionAct_->isEnabled());
        window.undoAct_->trigger();
        QCOMPARE(window.project_.findObject(a)->paths, movedA);
        QCOMPARE(window.project_.findObject(b)->paths, movedB);
        window.redoAct_->trigger();
        QVERIFY(window.project_.vector_objects.empty());
        QVERIFY(window.checkSelectionInvariants());
    }

    void panMovesViewWithoutSelectingOrMutating_data() {
        QTest::addColumn<double>("scale");
        QTest::newRow("fit-canvas") << 0.0;
        QTest::newRow("zoomed") << 16.0;
    }

    void panMovesViewWithoutSelectingOrMutating() {
        QFETCH(double, scale);
        MainWindow window;
        document::Project project;
        const ObjectId id = addRectangle(project);
        window.applyLoadedProject(project);
        expose(window);
        QVERIFY(!QTest::currentTestFailed());
        if (scale == 0.0) {
            window.view_->fitCanvas();
        } else {
            frame(window, scale);
        }
        const auto paths = window.project_.findObject(id)->paths;
        QSignalSpy changed(window.view_, &CanvasView::viewChanged);
        activate(window, Tool::Pan);
        QVERIFY(!QTest::currentTestFailed());
        const QPoint start = window.view_->mapFromScene(QPointF(0, 0));
        const QPoint end = start + QPoint(48, 24);
        QVERIFY(window.view_->viewport()->rect().contains(end));
        const QPointF centerBefore =
            window.view_->mapToScene(window.view_->viewport()->rect().center());
        drag(*window.view_, start, end);
        QVERIFY(changed.count() > 0);
        QVERIFY(centerBefore !=
                window.view_->mapToScene(window.view_->viewport()->rect().center()));
        QVERIFY(window.selectedObjectIds().empty());
        QVERIFY(!window.undoStack_.canUndo());
        QCOMPARE(window.project_.findObject(id)->paths, paths);
        QVERIFY(!window.view_->gestureActive());
        activate(window, Tool::Select);
        frame(window);
        click(window, QPointF(0, 0));
        QCOMPARE(window.selectedObject_, std::optional<ObjectId>{id});
    }

    void cropRequiresImageAndRoundTripsPipeline() {
        MainWindow window;
        expose(window);
        QVERIFY(!QTest::currentTestFailed());
        QVERIFY(!window.toolRectAct_->isEnabled());
        QVERIFY(!window.cropAct_->isEnabled());
        QVERIFY(!window.toolRectAct_->statusTip().isEmpty());
        window.applyLoadedProject(imageFixture());
        frame(window);
        activate(window, Tool::Rect);
        QVERIFY(!QTest::currentTestFailed());
        QVERIFY(window.view_->cropMode());
        dragScene(window, QPointF(-8, -8), QPointF(8, 8));
        QCOMPARE(window.project_.ops.size(), std::size_t{1});
        QVERIFY(std::holds_alternative<image::CropOp>(window.project_.ops.front()));
        const auto op = std::get<image::CropOp>(window.project_.ops.front());
        QCOMPARE(op.width, 32);
        QCOMPARE(op.height, 32);
        QCOMPARE(window.processed_.width, 32);
        QCOMPARE(window.processed_.height, 32);
        QCOMPARE(window.project_.original.width, 64);
        QCOMPARE(window.project_.original.height, 64);
        QCOMPARE(window.currentTool_, Tool::Select);
        QVERIFY(!window.view_->cropMode());
        QVERIFY(!window.cropAct_->isChecked());
        const auto croppedOps = window.project_.ops;
        window.undoAct_->trigger();
        QVERIFY(window.project_.ops.empty());
        QCOMPARE(window.processed_.width, 64);
        window.redoAct_->trigger();
        QCOMPARE(window.project_.ops, croppedOps);
        QCOMPARE(window.processed_.width, 32);
        QCOMPARE(window.processed_.height, 32);
        activate(window, Tool::Rect);
        QTest::keyClick(window.view_, Qt::Key_Escape);
        QCOMPARE(window.currentTool_, Tool::Select);
        QVERIFY(!window.view_->cropMode());
        QCOMPARE(window.undoStack_.undoNames().size(), std::size_t{1});
    }

    void boxToolsCreateSelectModifyUndoRedo_data() {
        QTest::addColumn<int>("toolValue");
        QTest::addColumn<int>("sides");
        QTest::addColumn<bool>("circle");
        QTest::newRow("rectangle") << static_cast<int>(Tool::DrawRectangle) << 4 << false;
        QTest::newRow("ellipse") << static_cast<int>(Tool::DrawEllipse) << 0 << false;
        QTest::newRow("shift-circle") << static_cast<int>(Tool::DrawEllipse) << 0 << true;
        QTest::newRow("regular-triangle")
            << static_cast<int>(Tool::DrawPolygonRegular) << 3 << false;
        QTest::newRow("regular-heptagon")
            << static_cast<int>(Tool::DrawPolygonRegular) << 7 << false;
    }

    void boxToolsCreateSelectModifyUndoRedo() {
        QFETCH(int, toolValue);
        QFETCH(int, sides);
        QFETCH(bool, circle);
        const Tool tool = static_cast<Tool>(toolValue);
        MainWindow window;
        expose(window); // dessin sans image : vrai document vectoriel, pas image factice
        QVERIFY(!QTest::currentTestFailed());
        window.polygonSidesSpin_->setValue(sides > 0 ? sides : 6);
        activate(window, tool);
        QVERIFY(!QTest::currentTestFailed());
        QSignalSpy drawn(window.view_, &CanvasView::boxDrawnMm);
        dragScene(window, QPointF(-8, -6), QPointF(8, 6),
                  circle ? Qt::ShiftModifier : Qt::NoModifier);
        QCOMPARE(drawn.count(), 1);
        QCOMPARE(window.project_.vector_objects.size(), std::size_t{1});
        QCOMPARE(window.undoStack_.undoNames().size(), std::size_t{1});
        const auto created = window.project_.vector_objects.front();
        QCOMPARE(window.selectedObject_, std::optional<ObjectId>{created.id});
        const auto& path = created.paths.front().outer;
        QVERIFY(path.closed);
        if (tool == Tool::DrawPolygonRegular || tool == Tool::DrawRectangle) {
            QCOMPARE(path.nodes.size(), static_cast<std::size_t>(sides));
        } else {
            QVERIFY(path.nodes.size() >= 4);
            if (circle) {
                const auto bounds = MainWindow::objectPainterPath(created).boundingRect();
                QVERIFY(std::abs(bounds.width() - bounds.height()) < 0.001);
            }
        }
        QCOMPARE(window.currentTool_, tool); // les formes de base restent chaînables
        QVERIFY(window.toolCutAct_->isEnabled());
        window.undoAct_->trigger();
        QVERIFY(window.project_.vector_objects.empty());
        QVERIFY(window.selectedObjectIds().empty());
        QVERIFY(!window.toolCutAct_->isEnabled());
        window.redoAct_->trigger();
        QCOMPARE(window.project_.findObject(created.id)->paths, created.paths);
        activate(window, Tool::Select);
        click(window, QPointF(0, 0));
        QCOMPARE(window.selectedObject_, std::optional<ObjectId>{created.id});
        QTest::keyClick(window.view_, Qt::Key_Right);
        QCOMPARE(window.project_.findObject(created.id)->paths.front().outer.nodes.front().pos,
                 path.nodes.front().pos + um(100, 0));
        QCOMPARE(window.undoStack_.undoNames().size(), std::size_t{2});
        window.undoAct_->trigger();
        QCOMPARE(window.project_.findObject(created.id)->paths, created.paths);
        window.redoAct_->trigger();
        QCOMPARE(window.project_.findObject(created.id)->paths.front().outer.nodes.front().pos,
                 path.nodes.front().pos + um(100, 0));
    }

    void clickToolsFinishBackspaceCancelAndRoundTrip_data() {
        QTest::addColumn<int>("toolValue");
        QTest::addColumn<bool>("open");
        QTest::newRow("polygon-closed") << static_cast<int>(Tool::DrawPolygon) << false;
        QTest::newRow("polygon-open") << static_cast<int>(Tool::DrawPolygon) << true;
        QTest::newRow("bezier-closed") << static_cast<int>(Tool::DrawBezier) << false;
        QTest::newRow("bezier-open") << static_cast<int>(Tool::DrawBezier) << true;
        QTest::newRow("satin-column") << static_cast<int>(Tool::DrawSatinColumn) << false;
    }

    void clickToolsFinishBackspaceCancelAndRoundTrip() {
        QFETCH(int, toolValue);
        QFETCH(bool, open);
        const Tool tool = static_cast<Tool>(toolValue);
        const bool satin = tool == Tool::DrawSatinColumn;
        MainWindow window;
        expose(window);
        QVERIFY(!QTest::currentTestFailed());
        if (open) {
            window.openPathAct_->trigger();
        }
        activate(window, tool);
        QVERIFY(!QTest::currentTestFailed());
        QVERIFY(!window.finishDrawAct_->isEnabled());
        click(window, QPointF(-8, -2));
        QVERIFY(window.finishDrawAct_->isEnabled());
        QVERIFY(window.cancelDrawAct_->isEnabled());
        QCOMPARE(pendingCount(window), std::size_t{1});
        QTest::keyClick(window.view_, Qt::Key_Backspace);
        QCOMPARE(pendingCount(window), std::size_t{0});
        QVERIFY(!window.finishDrawAct_->isEnabled());
        click(window, QPointF(-8, -2));
        window.cancelDrawAct_->trigger();
        QCOMPARE(pendingCount(window), std::size_t{0});
        QCOMPARE(window.currentTool_, tool); // Annuler le tracé != quitter l'outil
        QVERIFY(!window.undoStack_.canUndo());

        if (satin) {
            for (QPointF p : {QPointF(-8, 0), QPointF(-8, -4), QPointF(8, 0), QPointF(8, -4)}) {
                click(window, p);
            }
        } else {
            click(window, QPointF(-8, 4));
            if (tool == Tool::DrawBezier) {
                dragScene(window, QPointF(0, -4), QPointF(2, -4));
            } else {
                click(window, QPointF(0, -4));
            }
            click(window, QPointF(8, 4));
        }
        QTest::keyClick(window.view_, Qt::Key_Return);
        QCOMPARE(window.project_.vector_objects.size(), std::size_t{1});
        QCOMPARE(pendingCount(window), std::size_t{0});
        QVERIFY(!window.finishDrawAct_->isEnabled());
        QVERIFY(!window.cancelDrawAct_->isEnabled());
        QCOMPARE(window.undoStack_.undoNames().size(), std::size_t{1});
        const auto vector = window.project_.vector_objects.front();
        std::optional<document::StitchParams> params;
        ObjectId embroidery{};
        if (satin) {
            QCOMPARE(window.project_.embroidery_objects.size(), std::size_t{1});
            const auto& emb = window.project_.embroidery_objects.front();
            params = emb.params;
            embroidery = emb.id;
            const auto& column = std::get<document::SatinParams>(*params);
            QCOMPARE(column.rail_a.nodes.size(), std::size_t{2});
            QCOMPARE(column.rail_b.nodes.size(), std::size_t{2});
            QCOMPARE(column.rungs.size(), std::size_t{2});
            QVERIFY(!column.rail_a.closed && !column.rail_b.closed);
            QVERIFY(!vector.visible);
            QVERIFY(window.sequence_.has_value());
            QVERIFY(std::any_of(window.sequence_->commands.begin(),
                                window.sequence_->commands.end(),
                                [embroidery](const auto& command) {
                                    return command.type == stitch::CommandType::Stitch &&
                                           command.source == embroidery;
                                }));
            QCOMPARE(window.currentTool_, Tool::Select);
            QCOMPARE(window.selectedEmbroidery_, std::optional<ObjectId>{embroidery});
        } else {
            const auto& path = vector.paths.front().outer;
            QCOMPARE(path.closed, !open);
            QCOMPARE(path.nodes.size(), std::size_t{3});
            QCOMPARE(window.currentTool_, tool);
            if (tool == Tool::DrawBezier) {
                QCOMPARE(path.nodes[1].type, geometry::NodeType::Smooth);
                QVERIFY(path.nodes[1].tan_in && path.nodes[1].tan_out);
                QCOMPARE(*path.nodes[1].tan_in + *path.nodes[1].tan_out, um(0, 0));
            }
        }
        window.undoAct_->trigger();
        QVERIFY(window.project_.vector_objects.empty());
        QVERIFY(window.project_.embroidery_objects.empty());
        QVERIFY(!window.undoStack_.canUndo());
        window.redoAct_->trigger();
        QCOMPARE(window.project_.findObject(vector.id)->paths, vector.paths);
        if (satin) {
            QCOMPARE(window.project_.findEmbroidery(embroidery)->params, *params);
        }
        activate(window, tool);
        click(window, QPointF(-12, 8));
        QTest::keyClick(window.view_, Qt::Key_Escape);
        QCOMPARE(window.currentTool_, Tool::Select);
        QCOMPARE(pendingCount(window), std::size_t{0});
        QCOMPARE(window.undoStack_.undoNames().size(), std::size_t{1});
    }

    void freeformStrokeCreatesAndRoundTrips() {
        MainWindow window;
        expose(window);
        QVERIFY(!QTest::currentTestFailed());
        activate(window, Tool::DrawFreeform);
        QVERIFY(!QTest::currentTestFailed());
        QSignalSpy finished(window.view_, &CanvasView::freeformStrokeFinished);
        const std::array points{QPointF(-8, 6), QPointF(8, 6), QPointF(8, -6), QPointF(-8, -6),
                                QPointF(-8, 5)};
        const QPoint start = window.view_->mapFromScene(points.front());
        mouse(window.view_->viewport(), QEvent::MouseButtonPress, start, Qt::LeftButton,
              Qt::LeftButton);
        for (std::size_t i = 1; i < points.size(); ++i) {
            const QPoint point = window.view_->mapFromScene(points[i]);
            QVERIFY(window.view_->viewport()->rect().contains(point));
            mouse(window.view_->viewport(), QEvent::MouseMove, point, Qt::NoButton, Qt::LeftButton);
        }
        mouse(window.view_->viewport(), QEvent::MouseButtonRelease,
              window.view_->mapFromScene(points.back()), Qt::LeftButton, Qt::NoButton);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(window.project_.vector_objects.size(), std::size_t{1});
        const auto created = window.project_.vector_objects.front();
        QVERIFY(created.paths.front().outer.closed);
        QVERIFY(created.paths.front().outer.nodes.size() >= 4);
        QCOMPARE(window.selectedObject_, std::optional<ObjectId>{created.id});
        QCOMPARE(pendingCount(window), std::size_t{0});
        window.undoAct_->trigger();
        QVERIFY(window.project_.vector_objects.empty());
        window.redoAct_->trigger();
        QCOMPARE(window.project_.findObject(created.id)->paths, created.paths);
        activate(window, Tool::Select);
        click(window, QPointF(0, 0));
        QCOMPARE(window.selectedObject_, std::optional<ObjectId>{created.id});
    }

    void knifeCutsOnlySelectedShapeAndRoundTrips() {
        MainWindow window;
        document::Project project;
        const ObjectId target = addRectangle(project);
        const ObjectId untouched = addRectangle(project, 24'000);
        const auto originalTarget = project.findObject(target)->paths;
        const auto originalOther = project.findObject(untouched)->paths;
        window.applyLoadedProject(project);
        expose(window);
        QVERIFY(!QTest::currentTestFailed());
        click(window, QPointF(0, 0));
        QCOMPARE(window.selectedObject_, std::optional<ObjectId>{target});
        activate(window, Tool::Cut);
        QVERIFY(!QTest::currentTestFailed());
        dragScene(window, QPointF(0, -9), QPointF(0, 9));
        QCOMPARE(window.project_.vector_objects.size(), std::size_t{3});
        QCOMPARE(window.project_.findObject(untouched)->paths, originalOther);
        QCOMPARE(window.undoStack_.undoNames().size(), std::size_t{1});
        QVERIFY(window.checkSelectionInvariants());
        QCOMPARE(pendingCount(window), std::size_t{0});
        const auto cut = window.project_.vector_objects;
        window.undoAct_->trigger();
        QCOMPARE(window.project_.vector_objects.size(), std::size_t{2});
        QCOMPARE(window.project_.findObject(target)->paths, originalTarget);
        QCOMPARE(window.project_.findObject(untouched)->paths, originalOther);
        window.redoAct_->trigger();
        QCOMPARE(window.project_.vector_objects.size(), cut.size());
        for (const auto& vector : cut) {
            QVERIFY(window.project_.findObject(vector.id) != nullptr);
            QCOMPARE(window.project_.findObject(vector.id)->paths, vector.paths);
        }
        activate(window, Tool::Select);
        click(window, QPointF(-4, 0));
        QVERIFY(window.selectedObject_.has_value());
    }

    void knifeCutsMultiLayerBlendAndStackedShapes_data() {
        QTest::addColumn<bool>("selectFirst");
        QTest::addColumn<bool>("selectLayer");
        QTest::newRow("selected-shape") << true << false;
        QTest::newRow("selected-layer") << true << true;
        QTest::newRow("no-selection-cuts-all-visible") << false << false;
    }

    // Non-regression : un bloc multi-couche (paire de fondu liée + forme empilée) doit être
    // coupé d'un seul geste, chaque morceau gardant ses deux couches liées, et s'annuler d'un coup.
    void knifeCutsMultiLayerBlendAndStackedShapes() {
        QFETCH(bool, selectFirst);
        QFETCH(bool, selectLayer);
        MainWindow window;
        document::Project project;
        const ObjectId base = addRectangle(project);
        const ObjectId stacked = addRectangle(project);
        for (const ObjectId source : {base, stacked}) {
            document::EmbroideryObject bottom;
            bottom.id = project.object_ids.next();
            bottom.name = "bottom";
            bottom.source_vector = source;
            bottom.params = document::TatamiParams{};
            document::EmbroideryObject top = bottom;
            top.id = project.object_ids.next();
            top.name = "top";
            top.params = document::DirectionalFillParams{};
            bottom.color_blend = document::ColorBlendLink{top.id, false};
            top.color_blend = document::ColorBlendLink{bottom.id, true};
            project.embroidery_objects.push_back(bottom);
            project.embroidery_objects.push_back(top);
        }
        window.applyLoadedProject(project);
        expose(window);
        QVERIFY(!QTest::currentTestFailed());
        if (selectFirst) {
            std::optional<ObjectId> layer;
            if (selectLayer) {
                layer = window.project_.embroidery_objects.front().id;
            }
            window.setSelection({.region = std::nullopt, .embroidery = layer, .objects = {base}});
        }
        activate(window, Tool::Cut);
        QVERIFY(!QTest::currentTestFailed());
        dragScene(window, QPointF(0, -9), QPointF(0, 9));
        const std::size_t expectedVectors = selectFirst ? 3 : 4;
        QCOMPARE(window.project_.vector_objects.size(), expectedVectors);
        QCOMPARE(window.project_.embroidery_objects.size(), expectedVectors * 2);
        for (const auto& emb : window.project_.embroidery_objects) {
            QVERIFY(document::find_color_blend(window.project_, emb.id).has_value());
        }
        QCOMPARE(window.undoStack_.undoNames().size(), std::size_t{1});
        window.undoAct_->trigger();
        QCOMPARE(window.project_.vector_objects.size(), std::size_t{2});
        QCOMPARE(window.project_.embroidery_objects.size(), std::size_t{4});
        window.redoAct_->trigger();
        QCOMPARE(window.project_.vector_objects.size(), expectedVectors);
        QVERIFY(window.checkSelectionInvariants());
    }

    void knifeCutsDirectionalFillWithGuidesAndBreakLines_data() {
        QTest::addColumn<bool>("breakLine");
        QTest::addColumn<bool>("selectEmbroideryLayer");
        QTest::addColumn<bool>("editModeOn");
        QTest::newRow("guide-shape-selected") << false << false << false;
        QTest::newRow("guide-layer-selected") << false << true << false;
        QTest::newRow("guide-edit-mode-on") << false << true << true;
        QTest::newRow("break-shape-selected") << true << false << false;
        QTest::newRow("break-layer-selected") << true << true << false;
        QTest::newRow("break-edit-mode-on") << true << true << true;
    }

    // Non-regression : le couteau doit couper une forme remplie en directionnel (guides et
    // lignes de rupture comprises), quelle que soit la sélection, et s'annuler en un pas.
    void knifeCutsDirectionalFillWithGuidesAndBreakLines() {
        QFETCH(bool, breakLine);
        QFETCH(bool, selectEmbroideryLayer);
        QFETCH(bool, editModeOn);
        MainWindow window;
        auto fx = editFixture(breakLine ? EditMode::BreakLine : EditMode::DirectionGuide);
        window.applyLoadedProject(fx.project);
        expose(window);
        QVERIFY(!QTest::currentTestFailed());
        if (selectEmbroideryLayer) {
            selectEmbroidery(window, fx.embroidery);
            if (editModeOn) {
                editAction(window, breakLine ? EditMode::BreakLine : EditMode::DirectionGuide)
                    ->trigger();
            }
        } else {
            window.setSelection(
                {.region = std::nullopt, .embroidery = std::nullopt, .objects = {fx.vector}});
        }
        activate(window, Tool::Cut);
        QVERIFY(!QTest::currentTestFailed());
        dragScene(window, QPointF(2, -9), QPointF(2, 9));
        QCOMPARE(window.project_.vector_objects.size(), std::size_t{2});
        QCOMPARE(window.project_.embroidery_objects.size(), std::size_t{2});
        QCOMPARE(window.undoStack_.undoNames().size(), std::size_t{1});
        window.undoAct_->trigger();
        QCOMPARE(window.project_.vector_objects.size(), std::size_t{1});
        QCOMPARE(window.project_.embroidery_objects.size(), std::size_t{1});
        window.redoAct_->trigger();
        QCOMPARE(window.project_.vector_objects.size(), std::size_t{2});
    }

    // Un pont coupé peut laisser la forme connexe par l'autre côté de son trou.
    // Vérifier le vrai geste souris : une seule forme et un seul remplissage,
    // avec une modification annulable plutôt qu'un refus silencieux.
    void knifeCutsBridgeWithoutCreatingNewObject() {
        MainWindow window;
        auto fx = editFixture(EditMode::DirectionGuide);
        auto& shape = fx.project.vector_objects.front().paths.front();
        shape.outer = geometry::rectangle_path(um(-12'000, -12'000), um(12'000, 12'000));
        shape.holes.push_back(geometry::rectangle_path(um(-4'000, -4'000), um(4'000, 4'000)));
        const auto originalPaths = fx.project.vector_objects.front().paths;
        window.applyLoadedProject(fx.project);
        expose(window);
        QVERIFY(!QTest::currentTestFailed());

        window.setSelection(
            {.region = std::nullopt, .embroidery = std::nullopt, .objects = {fx.vector}});
        activate(window, Tool::Cut);
        QVERIFY(!QTest::currentTestFailed());
        dragScene(window, QPointF(0, -15), QPointF(0, -2));

        QCOMPARE(window.project_.vector_objects.size(), std::size_t{1});
        QCOMPARE(window.project_.embroidery_objects.size(), std::size_t{1});
        QCOMPARE(window.project_.vector_objects[0].id, fx.vector);
        QCOMPARE(window.project_.embroidery_objects[0].id, fx.embroidery);
        QVERIFY(window.project_.vector_objects[0].paths.front().holes.empty());
        QCOMPARE(window.undoStack_.undoNames().size(), std::size_t{1});

        window.undoAct_->trigger();
        QCOMPARE(window.project_.findObject(fx.vector)->paths == originalPaths, true);
        window.redoAct_->trigger();
        QVERIFY(window.project_.findObject(fx.vector)->paths.front().holes.empty());
    }

    void directionToolsDrawEditCancelAndRoundTrip_data() {
        QTest::addColumn<bool>("isBreak");
        QTest::newRow("direction-guide") << false;
        QTest::newRow("break-line") << true;
    }

    void directionToolsDrawEditCancelAndRoundTrip() {
        QFETCH(bool, isBreak);
        MainWindow window;
        auto fx = editFixture(EditMode::DirectionGuide);
        auto& initial = std::get<document::DirectionalFillParams>(
            fx.project.findEmbroidery(fx.embroidery)->params);
        initial.guides.clear();
        window.applyLoadedProject(fx.project);
        expose(window);
        QVERIFY(!QTest::currentTestFailed());
        QVERIFY(!window.drawDirectionGuideAct_->isEnabled());
        QVERIFY(!window.drawBreakLineAct_->isEnabled());
        selectEmbroidery(window, fx.embroidery);
        const Tool tool = isBreak ? Tool::DrawBreakLine : Tool::DrawDirectionGuide;
        activate(window, tool);
        QVERIFY(!QTest::currentTestFailed());
        QVERIFY(window.directionGuideModeAct_->isChecked());
        QCOMPARE(window.directionGuideTarget_, std::optional<ObjectId>{fx.embroidery});
        const std::array points = isBreak
                                      ? std::array{QPointF(0, -8), QPointF(0, 0), QPointF(0, 8)}
                                      : std::array{QPointF(-5, 0), QPointF(0, -2), QPointF(5, 0)};
        click(window, points[0]);
        click(window, points[1]);
        QTest::keyClick(window.view_, Qt::Key_Backspace);
        QCOMPARE(window.pendingGuidePoints_.size(), std::size_t{1});
        click(window, points[1]);
        click(window, points[2]);
        window.finishDrawAct_->trigger();
        QCOMPARE(window.undoStack_.undoNames().size(), std::size_t{1});
        const auto drawn = std::get<document::DirectionalFillParams>(
            window.project_.findEmbroidery(fx.embroidery)->params);
        const auto& paths = isBreak ? drawn.break_lines : drawn.guides;
        QCOMPARE(paths.size(), std::size_t{1});
        QVERIFY(!paths.front().closed);
        QCOMPARE(paths.front().nodes.size(), std::size_t{3});
        if (isBreak) {
            QVERIFY(std::all_of(paths.front().nodes.begin(), paths.front().nodes.end(),
                                [](const auto& node) { return !node.tan_in && !node.tan_out; }));
        } else {
            QVERIFY(paths.front().nodes[1].tan_in.has_value());
            QVERIFY(paths.front().nodes[1].tan_out.has_value());
        }
        QCOMPARE(window.currentTool_, tool); // plusieurs guides peuvent être tracés à la suite
        QVERIFY(window.pendingGuidePoints_.empty());
        QVERIFY(!window.finishDrawAct_->isEnabled());
        click(window, QPointF(-6, 3));
        window.cancelDrawAct_->trigger();
        QVERIFY(window.pendingGuidePoints_.empty());
        QCOMPARE(std::get<document::DirectionalFillParams>(
                     window.project_.findEmbroidery(fx.embroidery)->params),
                 drawn);
        activate(window, Tool::Select);
        auto* handle = handleAt(window.baseItems_, points[1]);
        QVERIFY(handle != nullptr);
        const QPoint start = window.view_->mapFromScene(handle->scenePos());
        QCOMPARE(window.view_->itemAt(start), static_cast<QGraphicsItem*>(handle));
        drag(*window.view_, start, start + QPoint(24, 0));
        QTRY_COMPARE(window.undoStack_.undoNames().size(), std::size_t{2});
        const auto moved = std::get<document::DirectionalFillParams>(
            window.project_.findEmbroidery(fx.embroidery)->params);
        const auto& movedPaths = isBreak ? moved.break_lines : moved.guides;
        QVERIFY(movedPaths.front().nodes[1].pos != paths.front().nodes[1].pos);
        QCOMPARE(movedPaths.front().nodes[0], paths.front().nodes[0]);
        QCOMPARE(movedPaths.front().nodes[2], paths.front().nodes[2]);
        window.undoAct_->trigger();
        QCOMPARE(std::get<document::DirectionalFillParams>(
                     window.project_.findEmbroidery(fx.embroidery)->params),
                 drawn);
        window.undoAct_->trigger();
        QCOMPARE(std::get<document::DirectionalFillParams>(
                     window.project_.findEmbroidery(fx.embroidery)->params),
                 initial);
        window.redoAct_->trigger();
        window.redoAct_->trigger();
        QCOMPARE(std::get<document::DirectionalFillParams>(
                     window.project_.findEmbroidery(fx.embroidery)->params),
                 moved);
        QTest::keyClick(window.view_, Qt::Key_Escape);
        QVERIFY(!window.directionGuideModeAct_->isChecked());
        QVERIFY(!window.directionGuideTarget_);
        QCOMPARE(window.currentTool_, Tool::Select);
    }

    void secondaryModesMoveExactlyOneUndoStep_data() { addEditRows(); }

    void secondaryModesMoveExactlyOneUndoStep() {
        QFETCH(int, modeValue);
        const EditMode mode = static_cast<EditMode>(modeValue);
        const auto fx = editFixture(mode);
        MainWindow window;
        window.applyLoadedProject(fx.project);
        expose(window);
        QVERIFY(!QTest::currentTestFailed());
        enterEdit(window, fx, mode);
        QVERIFY(!QTest::currentTestFailed());
        const QPointF scenePoint = editablePoint(window, mode);
        auto* handle = handleAt(window.baseItems_, scenePoint);
        QVERIFY(handle != nullptr);
        const QPoint start = window.view_->mapFromScene(scenePoint);
        QVERIFY(window.view_->viewport()->rect().contains(start));
        QCOMPARE(window.view_->itemAt(start), static_cast<QGraphicsItem*>(handle));
        QTest::mouseClick(window.view_->viewport(), Qt::LeftButton, Qt::NoModifier, start);
        QCoreApplication::processEvents();
        QVERIFY(!window.undoStack_.canUndo()); // clic sans déplacement = pas d'historique fantôme
        // Le clic n'a pas détruit la poignée (sinon le vrai hit-test du glisser le révèle).
        QCOMPARE(window.view_->itemAt(start), static_cast<QGraphicsItem*>(handle));
        const auto originalPaths = window.project_.findObject(fx.vector)->paths;
        const auto originalParams =
            mode != EditMode::Node
                ? std::optional<document::StitchParams>{window.project_
                                                            .findEmbroidery(fx.embroidery)
                                                            ->params}
                : std::nullopt;
        drag(*window.view_, start, start + QPoint(24, mode == EditMode::Node ? 16 : 0));
        QTRY_COMPARE(window.undoStack_.undoNames().size(), std::size_t{1});
        const auto movedPaths = window.project_.findObject(fx.vector)->paths;
        std::optional<document::StitchParams> movedParams;
        std::vector<document::StitchOverride> movedOverrides;
        if (mode == EditMode::Node) {
            QVERIFY(movedPaths != originalPaths);
            QCOMPARE(movedPaths.front().outer.nodes[1].pos,
                     originalPaths.front().outer.nodes[1].pos + um(1'500, -1'000));
        } else {
            const auto* emb = window.project_.findEmbroidery(fx.embroidery);
            movedParams = emb->params;
            movedOverrides = emb->overrides;
            if (mode == EditMode::Stitch) {
                QCOMPARE(movedOverrides.size(), std::size_t{1});
                QVERIFY(movedOverrides.front().moved_to);
                QCOMPARE(*movedOverrides.front().moved_to, modelPoint(scenePoint) + um(1'500, 0));
                QCOMPARE(*movedParams, *originalParams); // point retouché, paramètres intacts
            } else {
                QVERIFY(*movedParams != *originalParams);
                QVERIFY(movedOverrides.empty()); // rails/guides paramétriques, pas overrides
            }
        }
        window.undoAct_->trigger();
        QCOMPARE(window.project_.findObject(fx.vector)->paths, originalPaths);
        QVERIFY(!window.undoStack_.canUndo());
        if (originalParams) {
            QCOMPARE(window.project_.findEmbroidery(fx.embroidery)->params, *originalParams);
            QVERIFY(window.project_.findEmbroidery(fx.embroidery)->overrides.empty());
        }
        window.redoAct_->trigger();
        QCOMPARE(window.project_.findObject(fx.vector)->paths, movedPaths);
        if (movedParams) {
            QCOMPARE(window.project_.findEmbroidery(fx.embroidery)->params, *movedParams);
            QCOMPARE(window.project_.findEmbroidery(fx.embroidery)->overrides, movedOverrides);
        }
        if (mode != EditMode::Node) {
            QTest::keyClick(window.view_, Qt::Key_Escape);
            QVERIFY(!editAction(window, mode)->isChecked());
            QVERIFY(!window.stitchEditTarget_ && !window.satinGuideTarget_ &&
                    !window.railEditTarget_ && !window.directionGuideTarget_);
        }
        QCOMPARE(window.currentTool_, Tool::Select);
    }

    void secondaryModesExitWhenSelectionChanges_data() { addEditRows(false); }

    void secondaryModesExitWhenSelectionChanges() {
        QFETCH(int, modeValue);
        const EditMode mode = static_cast<EditMode>(modeValue);
        auto fx = editFixture(mode);
        std::optional<ObjectId> other;
        if (mode == EditMode::SatinGuide || mode == EditMode::SatinRail) {
            // En remodelage satin, le vide conserve intentionnellement la cible des rails.
            // Un clic sur une autre forme visible est le vrai changement de sélection.
            other = addRectangle(fx.project);
            for (auto& node : fx.project.findObject(*other)->paths.front().outer.nodes) {
                node.pos.y -= Micrometers{16'000};
            }
        }
        MainWindow window;
        window.applyLoadedProject(fx.project);
        expose(window);
        QVERIFY(!QTest::currentTestFailed());
        enterEdit(window, fx, mode);
        QVERIFY(!QTest::currentTestFailed());
        // Un clic dans le vide est un vrai changement de sélection, pas une écriture du seam.
        click(window, QPointF(0, other ? 16 : 12));
        if (other) {
            QCOMPARE(window.selectedObjectIds(), (std::vector<ObjectId>{*other}));
        } else {
            QVERIFY(window.selectedObjectIds().empty());
        }
        QVERIFY(!window.selectedEmbroidery_);
        QVERIFY(!window.stitchEditTarget_ && !window.satinGuideTarget_ && !window.railEditTarget_ &&
                !window.directionGuideTarget_);
        if (auto* action = editAction(window, mode)) {
            QVERIFY(!action->isChecked());
            QVERIFY(!action->isEnabled());
        }
        QVERIFY(!window.undoStack_.canUndo());
        // La prochaine sélection rectangulaire ne doit pas devenir un panoramique
        // simplement parce que la sortie de mode a bloqué le signal QAction::toggled.
        if (mode != EditMode::SatinGuide && mode != EditMode::SatinRail) {
            dragScene(window, QPointF(-10, -8), QPointF(10, 8));
            QCOMPARE(window.selectedObjectIds(), (std::vector<ObjectId>{fx.vector}));
        }
        QCOMPARE(window.view_->baseContext(), Context::Select);
        QVERIFY(!window.undoStack_.canUndo());
    }

    void documentReplacementRestoresSelectionGestures_data() { addEditRows(); }

    void documentReplacementRestoresSelectionGestures() {
        QFETCH(int, modeValue);
        const EditMode mode = static_cast<EditMode>(modeValue);
        const auto fx = editFixture(mode);
        MainWindow window;
        window.applyLoadedProject(fx.project);
        expose(window);
        QVERIFY(!QTest::currentTestFailed());
        enterEdit(window, fx, mode);
        QVERIFY(!QTest::currentTestFailed());
        document::Project replacement;
        const ObjectId a = addRectangle(replacement, -12'000);
        const ObjectId b = addRectangle(replacement, 12'000);
        window.applyLoadedProject(replacement);
        frame(window, 10.0);
        // Le dock Ordre ouvert par enterEdit rétrécit le viewport : adapter le cadrage,
        // pas les gestes ni leurs effets attendus, à la place réellement disponible.
        window.view_->fitInView(QRectF(-25, -11, 50, 22), Qt::KeepAspectRatio);
        QVERIFY(window.selectedObjectIds().empty());
        QVERIFY(!window.stitchEditTarget_ && !window.satinGuideTarget_ && !window.railEditTarget_ &&
                !window.directionGuideTarget_);
        dragScene(window, QPointF(-22, -8), QPointF(22, 8));
        QCOMPARE(window.selectedObjectIds().size(), std::size_t{2});
        QVERIFY(window.isObjectSelected(a));
        QVERIFY(window.isObjectSelected(b));
        QCOMPARE(window.view_->baseContext(), Context::Select);
        QVERIFY(!window.undoStack_.canUndo());
        QVERIFY(window.checkSelectionInvariants());
    }

    void satinGuidesSelectAddRemoveAndRoundTrip() {
        const auto fx = editFixture(EditMode::SatinGuide);
        MainWindow window;
        window.applyLoadedProject(fx.project);
        expose(window);
        QVERIFY(!QTest::currentTestFailed());
        enterEdit(window, fx, EditMode::SatinGuide);
        QVERIFY(!QTest::currentTestFailed());
        QVERIFY(!window.removeSatinGuideAct_->isEnabled());
        auto* item = window.view_->itemAt(window.view_->mapFromScene(QPointF(0, -2)));
        QVERIFY(dynamic_cast<SatinGuideItem*>(item) != nullptr);
        click(window, QPointF(0, -2));
        QTRY_COMPARE(window.selectedSatinGuide_, std::optional<std::size_t>{1});
        QVERIFY(window.removeSatinGuideAct_->isEnabled());
        window.removeSatinGuideAct_->trigger();
        QCOMPARE(
            std::get<document::SatinParams>(window.project_.findEmbroidery(fx.embroidery)->params)
                .rungs.size(),
            std::size_t{2});
        QVERIFY(!window.removeSatinGuideAct_->isEnabled());
        window.undoAct_->trigger();
        QCOMPARE(window.project_.findEmbroidery(fx.embroidery)->params,
                 fx.project.findEmbroidery(fx.embroidery)->params);
        window.addSatinGuideAct_->trigger();
        const auto added = window.project_.findEmbroidery(fx.embroidery)->params;
        QCOMPARE(std::get<document::SatinParams>(added).rungs.size(), std::size_t{4});
        QVERIFY(window.selectedSatinGuide_.has_value());
        QVERIFY(window.removeSatinGuideAct_->isEnabled());
        window.undoAct_->trigger();
        QCOMPARE(window.project_.findEmbroidery(fx.embroidery)->params,
                 fx.project.findEmbroidery(fx.embroidery)->params);
        window.redoAct_->trigger();
        QCOMPARE(window.project_.findEmbroidery(fx.embroidery)->params, added);
    }

    void secondaryModeSwitchingIsExclusiveAndNonMutating_data() {
        QTest::addColumn<bool>("satin");
        QTest::newRow("satin-rails-guides-points") << true;
        QTest::newRow("direction-guides-points") << false;
    }

    void secondaryModeSwitchingIsExclusiveAndNonMutating() {
        QFETCH(bool, satin);
        const EditMode mode = satin ? EditMode::SatinUnified : EditMode::DirectionGuide;
        const auto fx = editFixture(mode);
        MainWindow window;
        window.applyLoadedProject(fx.project);
        expose(window);
        QVERIFY(!QTest::currentTestFailed());
        enterEdit(window, fx, mode);
        QVERIFY(!QTest::currentTestFailed());
        if (satin) {
            QVERIFY(window.satinGuideModeAct_->isChecked());
            QVERIFY(window.railEditModeAct_->isChecked());
        } else {
            QVERIFY(window.directionGuideModeAct_->isChecked());
        }
        window.stitchEditModeAct_->trigger();
        QVERIFY(window.stitchEditModeAct_->isChecked());
        QCOMPARE(window.stitchEditTarget_, std::optional<ObjectId>{fx.embroidery});
        QVERIFY(!window.satinEditModeAct_->isChecked());
        QVERIFY(!window.satinGuideModeAct_->isChecked());
        QVERIFY(!window.railEditModeAct_->isChecked());
        QVERIFY(!window.directionGuideModeAct_->isChecked());
        QVERIFY(!window.satinGuideTarget_ && !window.railEditTarget_ &&
                !window.directionGuideTarget_);
        QCOMPARE(window.view_->baseContext(), Context::StitchEdit);
        // Vérifier un vrai point interactif dans le nouveau mode, pas seulement ses booléens.
        const QPointF point = editablePoint(window, EditMode::Stitch);
        auto* handle = handleAt(window.baseItems_, point);
        QVERIFY(handle != nullptr);
        QCOMPARE(window.view_->itemAt(window.view_->mapFromScene(point)),
                 static_cast<QGraphicsItem*>(handle));
        dragScene(window, point, point + QPointF(1.5, 0));
        QTRY_COMPARE(window.project_.findEmbroidery(fx.embroidery)->overrides.size(),
                     std::size_t{1});
        const auto overrides = window.project_.findEmbroidery(fx.embroidery)->overrides;
        editAction(window, mode)->trigger();
        QVERIFY(editAction(window, mode)->isChecked());
        QVERIFY(!window.stitchEditModeAct_->isChecked());
        QVERIFY(!window.stitchEditTarget_ && !window.stitchEditView_);
        QCOMPARE(window.view_->baseContext(), Context::NodeEdit);
        if (satin) {
            QVERIFY(window.satinGuideModeAct_->isChecked());
            QVERIFY(window.railEditModeAct_->isChecked());
        }
        QCOMPARE(window.project_.findEmbroidery(fx.embroidery)->overrides, overrides);
        QCOMPARE(window.project_.findEmbroidery(fx.embroidery)->params,
                 fx.project.findEmbroidery(fx.embroidery)->params);
        QCOMPARE(window.undoStack_.undoNames().size(), std::size_t{1});
        QTest::keyClick(window.view_, Qt::Key_Escape);
        QVERIFY(!editAction(window, mode)->isChecked());
        QVERIFY(!window.stitchEditTarget_ && !window.satinGuideTarget_ && !window.railEditTarget_ &&
                !window.directionGuideTarget_);
    }

    void incompleteGesturesDoNotCreateCommands_data() {
        QTest::addColumn<int>("toolValue");
        for (const auto& entry :
             {std::pair{"rectangle-click", Tool::DrawRectangle},
              std::pair{"ellipse-click", Tool::DrawEllipse},
              std::pair{"regular-click", Tool::DrawPolygonRegular},
              std::pair{"polygon-one-point", Tool::DrawPolygon},
              std::pair{"bezier-one-point", Tool::DrawBezier},
              std::pair{"satin-one-pair", Tool::DrawSatinColumn},
              std::pair{"freeform-click", Tool::DrawFreeform}, std::pair{"knife-click", Tool::Cut},
              std::pair{"guide-one-point", Tool::DrawDirectionGuide},
              std::pair{"break-one-point", Tool::DrawBreakLine},
              std::pair{"crop-click", Tool::Rect}}) {
            QTest::newRow(entry.first) << static_cast<int>(entry.second);
        }
    }

    void incompleteGesturesDoNotCreateCommands() {
        QFETCH(int, toolValue);
        const Tool tool = static_cast<Tool>(toolValue);
        const auto fx = editFixture(EditMode::DirectionGuide);
        MainWindow window;
        auto project = tool == Tool::Rect ? imageFixture() : fx.project;
        window.applyLoadedProject(project);
        expose(window);
        QVERIFY(!QTest::currentTestFailed());
        if (tool == Tool::DrawDirectionGuide || tool == Tool::DrawBreakLine) {
            selectEmbroidery(window, fx.embroidery);
        }
        activate(window, tool);
        QVERIFY(!QTest::currentTestFailed());
        click(window, QPointF(-5, -4));
        if (tool == Tool::DrawSatinColumn) {
            click(window, QPointF(-5, -2));
        }
        if (window.finishDrawAct_->isEnabled()) {
            window.finishDrawAct_->trigger();
        }
        QCOMPARE(window.project_.vector_objects.size(), project.vector_objects.size());
        for (const auto& vector : project.vector_objects) {
            QCOMPARE(window.project_.findObject(vector.id)->paths, vector.paths);
        }
        QCOMPARE(window.project_.embroidery_objects.size(), project.embroidery_objects.size());
        for (const auto& embroidery : project.embroidery_objects) {
            QCOMPARE(window.project_.findEmbroidery(embroidery.id)->params, embroidery.params);
        }
        QVERIFY(window.project_.ops.empty());
        QVERIFY(!window.undoStack_.canUndo());
        QTest::keyClick(window.view_, Qt::Key_Escape);
        QCOMPARE(pendingCount(window), std::size_t{0});
        QCOMPARE(window.currentTool_, Tool::Select);
        QVERIFY(!window.finishDrawAct_->isEnabled());
        QVERIFY(!window.cancelDrawAct_->isEnabled());
    }

    void toolChangesDiscardPendingDrawing_data() {
        QTest::addColumn<int>("toolValue");
        for (const auto& entry :
             {std::pair{"polygon", Tool::DrawPolygon}, std::pair{"bezier", Tool::DrawBezier},
              std::pair{"satin", Tool::DrawSatinColumn}, std::pair{"freeform", Tool::DrawFreeform},
              std::pair{"knife", Tool::Cut}, std::pair{"guide", Tool::DrawDirectionGuide},
              std::pair{"break", Tool::DrawBreakLine}, std::pair{"rectangle", Tool::DrawRectangle},
              std::pair{"ellipse", Tool::DrawEllipse},
              std::pair{"regular", Tool::DrawPolygonRegular}, std::pair{"crop", Tool::Rect}}) {
            QTest::newRow(entry.first) << static_cast<int>(entry.second);
        }
    }

    void toolChangesDiscardPendingDrawing() {
        QFETCH(int, toolValue);
        const Tool tool = static_cast<Tool>(toolValue);
        MainWindow window;
        auto fx = editFixture(EditMode::DirectionGuide);
        auto project = tool == Tool::Rect ? imageFixture() : fx.project;
        const auto vectorCount = project.vector_objects.size();
        const auto embroideryCount = project.embroidery_objects.size();
        window.applyLoadedProject(project);
        expose(window);
        QVERIFY(!QTest::currentTestFailed());
        if (tool == Tool::DrawDirectionGuide || tool == Tool::DrawBreakLine) {
            selectEmbroidery(window, fx.embroidery);
        }
        activate(window, tool);
        QVERIFY(!QTest::currentTestFailed());
        const bool dragTool = tool == Tool::DrawFreeform || tool == Tool::Cut ||
                              tool == Tool::Rect || tool == Tool::DrawRectangle ||
                              tool == Tool::DrawEllipse || tool == Tool::DrawPolygonRegular;
        const QPoint start = window.view_->mapFromScene(QPointF(-5, -4));
        const QPoint end = window.view_->mapFromScene(QPointF(5, 4));
        if (dragTool) {
            mouse(window.view_->viewport(), QEvent::MouseButtonPress, start, Qt::LeftButton,
                  Qt::LeftButton);
            mouse(window.view_->viewport(), QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
        } else {
            click(window, QPointF(-5, -4));
            QVERIFY(pendingCount(window) > 0);
            QVERIFY(window.finishDrawAct_->isEnabled());
        }
        // Changement par l'action (clavier/menu), possible même pendant un glisser.
        window.toolSelectAct_->trigger();
        QCOMPARE(window.currentTool_, Tool::Select);
        if (dragTool) {
            mouse(window.view_->viewport(), QEvent::MouseButtonRelease, end, Qt::LeftButton,
                  Qt::NoButton);
        }
        QCoreApplication::processEvents();
        QCOMPARE(pendingCount(window), std::size_t{0});
        QVERIFY(window.polygonPreviewItem_ == nullptr && window.bezierPreviewItem_ == nullptr &&
                window.satinPreviewItem_ == nullptr && window.guidePreviewItem_ == nullptr &&
                window.freeformPreviewItem_ == nullptr);
        QVERIFY(!window.finishDrawAct_->isEnabled());
        QVERIFY(!window.cancelDrawAct_->isEnabled());
        QVERIFY(!window.view_->cropMode() && !window.view_->boxDrawMode() &&
                !window.view_->polygonDrawMode() && !window.view_->bezierDrawMode() &&
                !window.view_->freeformDrawMode() && !window.view_->satinPairDrawMode());
        QCOMPARE(window.project_.vector_objects.size(), vectorCount);
        QCOMPARE(window.project_.embroidery_objects.size(), embroideryCount);
        QVERIFY(window.project_.ops.empty());
        QVERIFY(!window.undoStack_.canUndo());
        // Le prochain outil doit fonctionner, sans héritage d'un press ou d'un aperçu.
        activate(window, Tool::DrawRectangle);
        dragScene(window, QPointF(-6, -4), QPointF(6, 4), Qt::ControlModifier);
        QCOMPARE(window.project_.vector_objects.size(), vectorCount + 1);
        QCOMPARE(window.undoStack_.undoNames().size(), std::size_t{1});
    }

    void deferredHandleReleaseCannotMutateReplacement_data() { addEditRows(false); }

    void deferredHandleReleaseCannotMutateReplacement() {
        QFETCH(int, modeValue);
        const EditMode mode = static_cast<EditMode>(modeValue);
        const auto fx = editFixture(mode);
        MainWindow window;
        window.applyLoadedProject(fx.project);
        expose(window);
        QVERIFY(!QTest::currentTestFailed());
        enterEdit(window, fx, mode);
        QVERIFY(!QTest::currentTestFailed());
        const QPointF scenePoint = editablePoint(window, mode);
        auto* handle = handleAt(window.baseItems_, scenePoint);
        QVERIFY(handle != nullptr);
        const QPoint start = window.view_->mapFromScene(scenePoint);
        QCOMPARE(window.view_->itemAt(start), static_cast<QGraphicsItem*>(handle));
        const auto generation = window.documentGeneration_;
        // sendEvent ne traite pas les QTimer(0) : la fenêtre de remplacement est réelle,
        // contrôlée sans sleep. Les ids sont volontairement réutilisés dans le projet neuf.
        drag(*window.view_, start, start + QPoint(24, mode == EditMode::Node ? 16 : 0));
        QVERIFY(!window.undoStack_.canUndo());
        window.applyLoadedProject(fx.project);
        QVERIFY(window.documentGeneration_ != generation);
        QCoreApplication::processEvents();
        QCOMPARE(window.project_.findObject(fx.vector)->paths,
                 fx.project.findObject(fx.vector)->paths);
        if (mode != EditMode::Node) {
            QCOMPARE(window.project_.findEmbroidery(fx.embroidery)->params,
                     fx.project.findEmbroidery(fx.embroidery)->params);
            QVERIFY(window.project_.findEmbroidery(fx.embroidery)->overrides.empty());
        }
        QVERIFY(!window.undoStack_.canUndo());
        QVERIFY(!window.undoStack_.canRedo());
        QVERIFY(!window.stitchEditTarget_ && !window.satinGuideTarget_ && !window.railEditTarget_ &&
                !window.directionGuideTarget_);
    }

    void deferredTransformReleaseCannotMutateReplacement_data() {
        QTest::addColumn<int>("kind");
        QTest::newRow("resize") << 0;
        QTest::newRow("rotation") << 1;
        QTest::newRow("fill-angle") << 2;
    }

    void deferredTransformReleaseCannotMutateReplacement() {
        QFETCH(int, kind);
        auto fx = editFixture(EditMode::Node);
        if (kind == 2) {
            document::EmbroideryObject fill;
            fill.id = fx.project.object_ids.next();
            fill.source_vector = fx.vector;
            fill.params = document::TatamiParams{};
            fx.embroidery = fill.id;
            fx.project.embroidery_objects.push_back(fill);
        }
        MainWindow window;
        window.applyLoadedProject(fx.project);
        expose(window);
        QVERIFY(!QTest::currentTestFailed());
        window.setSelection(
            {.embroidery = kind == 2 ? std::optional<ObjectId>{fx.embroidery} : std::nullopt,
             .objects = {fx.vector}});
        window.displayImage(window.processed_);
        window.updateActions();
        QCoreApplication::processEvents();
        frame(window);
        QGraphicsItem* handle = nullptr;
        for (auto* item : window.baseItems_) {
            if ((kind == 2 && dynamic_cast<NodeHandleItem*>(item) == nullptr) ||
                (kind != 2 && dynamic_cast<ResizeHandleItem*>(item) == nullptr)) {
                continue;
            }
            const auto text = item->toolTip();
            if ((kind == 0 && text.contains(QStringLiteral("redimensionner"))) ||
                (kind == 1 && text.contains(QStringLiteral("pivoter"))) ||
                (kind == 2 && text.contains(QStringLiteral("orientation des fils")))) {
                handle = item;
                break;
            }
        }
        QVERIFY(handle != nullptr);
        const QPoint start = window.view_->mapFromScene(handle->scenePos());
        QCOMPARE(window.view_->itemAt(start), static_cast<QGraphicsItem*>(handle));
        drag(*window.view_, start, start + QPoint(30, 24));
        QVERIFY(!window.undoStack_.canUndo());
        window.applyLoadedProject(fx.project);
        QCoreApplication::processEvents();
        QCOMPARE(window.project_.findObject(fx.vector)->paths,
                 fx.project.findObject(fx.vector)->paths);
        if (kind == 2) {
            QCOMPARE(window.project_.findEmbroidery(fx.embroidery)->params,
                     fx.project.findEmbroidery(fx.embroidery)->params);
        }
        QVERIFY(!window.undoStack_.canUndo());
    }

    void textToolDialogCreateEditCancelDeleteRoundTrip() {
        MainWindow window;
        expose(window);
        QVERIFY(!QTest::currentTestFailed());
        QVERIFY(!window.editTextAct_->isEnabled());
        QVERIFY(!window.removeTextAct_->isEnabled());
        activate(window, Tool::Text);
        QVERIFY(!QTest::currentTestFailed());
        bool seen = false;
        bool controlsValid = false;
        armTextDialog(window, QString(), false, seen, controlsValid);
        click(window, QPointF(-8, 4));
        QVERIFY(seen && controlsValid);
        QVERIFY(window.project_.text_objects.empty());
        QVERIFY(window.project_.vector_objects.empty());
        QVERIFY(!window.undoStack_.canUndo());
        QCOMPARE(window.currentTool_, Tool::Text); // annuler permet de choisir une autre origine

        seen = false;
        controlsValid = false;
        armTextDialog(window, QStringLiteral("HI"), true, seen, controlsValid);
        click(window, QPointF(-8, 4));
        QVERIFY(seen && controlsValid);
        QCOMPARE(window.project_.text_objects.size(), std::size_t{1});
        QCOMPARE(window.project_.embroidery_objects.size(), std::size_t{2});
        QCOMPARE(window.project_.vector_objects.size(), std::size_t{2});
        const auto text = window.project_.text_objects.front();
        QCOMPARE(text.origin, um(-8'000, -4'000));
        QCOMPARE(text.font.builtin, std::string("vera-sans-bold"));
        QCOMPARE(text.fill, document::TextFill::Contour);
        QCOMPARE(window.currentTool_, Tool::Select);
        QCOMPARE(window.selectedTextId(), std::optional<ObjectId>{text.id});
        QVERIFY(window.editTextAct_->isEnabled());
        QVERIFY(window.removeTextAct_->isEnabled());
        QCOMPARE(window.undoStack_.undoNames().size(), std::size_t{1});
        const auto firstLetter = window.project_.vector_objects.front();
        // Désélection puis clic réel sur une lettre, hors poignées : pas applyText en seam.
        click(window, QPointF(0, 14));
        QVERIFY(!window.selectedTextId());
        const auto letterPath = MainWindow::objectPainterPath(firstLetter);
        std::optional<QPointF> letterPoint;
        const auto box = letterPath.boundingRect();
        for (int y = 1; y < 20 && !letterPoint; ++y) {
            for (int x = 1; x < 20; ++x) {
                const QPointF p(box.left() + box.width() * x / 20.0,
                                box.top() + box.height() * y / 20.0);
                const QPointF actual = window.view_->mapToScene(window.view_->mapFromScene(p));
                if (letterPath.contains(actual)) {
                    letterPoint = actual;
                    break;
                }
            }
        }
        QVERIFY(letterPoint.has_value());
        click(window, *letterPoint);
        QCOMPARE(window.selectedTextId(), std::optional<ObjectId>{text.id});

        seen = false;
        controlsValid = false;
        armTextDialog(window, QStringLiteral("HIT"), true, seen, controlsValid);
        window.editTextAct_->trigger();
        QVERIFY(seen && controlsValid);
        QCOMPARE(window.project_.text_objects.front().id, text.id);
        QCOMPARE(window.project_.text_objects.front().text, std::string("HIT"));
        QCOMPARE(window.project_.embroidery_objects.size(), std::size_t{3});
        QCOMPARE(window.undoStack_.undoNames().size(), std::size_t{2});
        const auto edited = window.project_.text_objects.front();
        const auto editedVectors = window.project_.vector_objects;
        seen = false;
        controlsValid = false;
        armTextDialog(window, QString(), false, seen, controlsValid);
        window.editTextAct_->trigger();
        QVERIFY(seen && controlsValid);
        QCOMPARE(window.project_.text_objects.front(), edited);
        QCOMPARE(window.undoStack_.undoNames().size(), std::size_t{2});
        window.undoAct_->trigger();
        QCOMPARE(window.project_.text_objects.front(), text);
        QCOMPARE(window.project_.embroidery_objects.size(), std::size_t{2});
        window.redoAct_->trigger();
        QCOMPARE(window.project_.text_objects.front(), edited);
        for (const auto& letter : editedVectors) {
            QCOMPARE(window.project_.findObject(letter.id)->paths, letter.paths);
        }
        // Undo/redo ne promet pas une sélection restaurée : reprendre une lettre par le canevas.
        click(window, *letterPoint);
        QVERIFY(window.removeTextAct_->isEnabled());
        window.removeTextAct_->trigger();
        QVERIFY(window.project_.text_objects.empty());
        QVERIFY(window.project_.vector_objects.empty());
        QVERIFY(window.project_.embroidery_objects.empty());
        QVERIFY(!window.editTextAct_->isEnabled());
        QVERIFY(!window.removeTextAct_->isEnabled());
        window.undoAct_->trigger();
        QCOMPARE(window.project_.text_objects.front(), edited);
        QCOMPARE(window.project_.embroidery_objects.size(), std::size_t{3});
        window.redoAct_->trigger();
        QVERIFY(window.project_.text_objects.empty());
    }
};

} // namespace openstitch::desktop

// Offscreen même en lancement direct ; le chef câble Qt::Test et les widgets dans CMake.
int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);
    openstitch::desktop::MainWindowTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_tool_workflows.moc"
