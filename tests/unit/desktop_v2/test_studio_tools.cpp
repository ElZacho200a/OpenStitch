// SPDX-License-Identifier: Apache-2.0
// Studio -- famille F « outils de dessin et d'édition vectorielle » : parcours réels (shell QML,
// vrai canevas, souris QTest, vrai moteur) avec annulation en une étape, Échap et persistance OSP.
// Aucun mock du moteur.
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <cmath>
#include <filesystem>

#include "action_controller.hpp"
#include "edit_controller.hpp"
#include "gpu_canvas.hpp"
#include "node_edit.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "studio_controller.hpp"
#include "tool_controller.hpp"
#include "tool_registry.hpp"
#include "vector_controller.hpp"
#include "vector_space_controller.hpp"
#include "workspace_controller.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;

namespace {

// Deux lignes de repère : elles bornent une zone de 60 x 60 mm que le canevas cadre à l'ouverture.
document::Project anchorProject() {
    document::Project project;
    project.finishing = document::SequenceFinishing::legacy();
    const double ys[] = {-30.0, 30.0};
    for (int i = 0; i < 2; ++i) {
        document::VectorObject vector;
        vector.id = ObjectId{11u + static_cast<unsigned>(i)};
        geometry::Path path;
        path.closed = false;
        const auto y = Micrometers{static_cast<std::int32_t>(ys[i] * 1000)};
        path.nodes = {{{Micrometers{-30'000}, y}}, {{Micrometers{30'000}, y}}};
        vector.paths.push_back({path, {}});
        project.vector_objects.push_back(vector);
        document::EmbroideryObject object;
        object.id = ObjectId{101u + static_cast<unsigned>(i)};
        object.source_vector = vector.id;
        object.name = "Repere";
        object.rgb = {20, 40, 200};
        object.params = document::RunningStitchParams{Micrometers{2'000}};
        project.embroidery_objects.push_back(object);
    }
    project.object_ids.reset(300);
    return project;
}

struct Box {
    double x0{1e9}, y0{1e9}, x1{-1e9}, y1{-1e9};
    [[nodiscard]] double width() const { return x1 - x0; }
    [[nodiscard]] double height() const { return y1 - y0; }
};

const document::VectorObject* shapeFor(const document::Project& p, std::uint64_t embroideryId) {
    const auto* emb = p.findEmbroidery(ObjectId{embroideryId});
    return emb ? p.findObject(emb->source_vector) : nullptr;
}

Box boundsOf(const document::Project& p, std::uint64_t embroideryId) {
    Box box;
    if (const auto* shape = shapeFor(p, embroideryId)) {
        for (const auto& set : shape->paths) {
            for (const auto& n : set.outer.nodes) {
                const double x = n.pos.x.value / 1000.0;
                const double y = n.pos.y.value / 1000.0;
                box.x0 = std::min(box.x0, x);
                box.x1 = std::max(box.x1, x);
                box.y0 = std::min(box.y0, y);
                box.y1 = std::max(box.y1, y);
            }
        }
    }
    return box;
}

std::size_t nodeCount(const document::Project& p, std::uint64_t embroideryId) {
    const auto* shape = shapeFor(p, embroideryId);
    return shape && !shape->paths.empty() ? shape->paths.front().outer.nodes.size() : 0;
}

QQuickItem* deepItem(QQuickItem* root, const QString& name) {
    if (root->objectName() == name) {
        return root;
    }
    for (auto* child : root->childItems()) {
        if (auto* found = deepItem(child, name)) {
            return found;
        }
    }
    return nullptr;
}

struct Rig {
    DocumentSession session;
    StudioController controller{session};
    ActionController actions{controller};
    EditController editor{controller};
    ToolController tools{controller};
    VectorController vectors{controller};
    VectorSpaceController stage{controller};
    QQmlApplicationEngine engine;
    QStringList warnings;
    QQuickWindow* window{nullptr};
    GpuCanvas* canvas{nullptr};
};

} // namespace

class StudioToolsTest : public QObject {
    Q_OBJECT
private:
    void open(Rig& rig);
    void settle(Rig& rig);
    [[nodiscard]] std::size_t steps(Rig& rig) { return rig.session.undoNames().size(); }
    [[nodiscard]] std::size_t objects(Rig& rig) {
        return rig.session.project().embroidery_objects.size();
    }
    [[nodiscard]] QPoint at(Rig& rig, double xMm, double yModelMm) {
        const auto s = rig.canvas->camera().worldToScreen({xMm, -yModelMm});
        return rig.canvas->mapToScene(QPointF(s.x, s.y)).toPoint();
    }
    void drag(Rig& rig, QPoint from, QPoint to, Qt::KeyboardModifiers mods = {}) {
        QTest::mousePress(rig.window, Qt::LeftButton, mods, from);
        for (int step = 1; step <= 6; ++step) {
            QTest::mouseMove(rig.window, from + (to - from) * step / 6);
        }
        QTest::mouseRelease(rig.window, Qt::LeftButton, mods, to);
    }
    // Trace avec un outil de forme, renvoie l'identifiant de l'objet créé (sélectionné).
    std::uint64_t draw(Rig& rig, const char* tool, QPointF a, QPointF b,
                       Qt::KeyboardModifiers mods = {}) {
        rig.tools.setActiveTool(QString::fromLatin1(tool));
        const auto before = steps(rig);
        drag(rig, at(rig, a.x(), a.y()), at(rig, b.x(), b.y()), mods);
        if (steps(rig) != before + 1 || rig.session.selection().targets().size() != 1) {
            return 0;
        }
        return rig.session.selection().targets().front().id;
    }
    [[nodiscard]] static QString osp(const QTemporaryDir& d) { return d.filePath("tools.osp"); }

private slots:
    void initTestCase();
    void toolRailMatchesRegistry();
    void basicShapesByMouse();
    void polygonAndRegularShapes();
    void penBezierAndFreehand();
    void escapeCancelsGestures();
    void nodeEditByMouse();
    void nodeDragFromSelectTool();
    void offscreenNodeHandlesDoNotFillQmlCanvas();
    void knifeAndBooleans();
    void snappingIsConfigurable();
};

void StudioToolsTest::initTestCase() {
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));
    qmlRegisterType<GpuCanvas>("OpenStitch.Studio", 1, 0, "GpuCanvas");
    qmlRegisterUncreatableType<StudioController>("OpenStitch.Studio", 1, 0, "StudioController",
                                                 QStringLiteral("Provided by the application"));
    qmlRegisterUncreatableType<EditController>("OpenStitch.Studio", 1, 0, "EditController",
                                               QStringLiteral("Provided by the application"));
    qmlRegisterUncreatableType<ToolController>("OpenStitch.Studio", 1, 0, "ToolController",
                                               QStringLiteral("Provided by the application"));
}

void StudioToolsTest::open(Rig& rig) {
    connect(&rig.engine, &QQmlApplicationEngine::warnings, this,
            [&rig](const QList<QQmlError>& errors) {
                for (const auto& error : errors) {
                    rig.warnings.push_back(error.toString());
                }
            });
    auto* ctx = rig.engine.rootContext();
    ctx->setContextProperty(QStringLiteral("studio"), &rig.controller);
    ctx->setContextProperty(QStringLiteral("actions"), &rig.actions);
    ctx->setContextProperty(QStringLiteral("editor"), &rig.editor);
    ctx->setContextProperty(QStringLiteral("tools"), &rig.tools);
    ctx->setContextProperty(QStringLiteral("vectors"), &rig.vectors);
    ctx->setContextProperty(QStringLiteral("vectorStage"), &rig.stage);
    rig.engine.load(QUrl(QStringLiteral("qrc:/studio/qml/Main.qml")));
    QVERIFY2(!rig.engine.rootObjects().isEmpty(), qPrintable(rig.warnings.join('\n')));
    rig.window = qobject_cast<QQuickWindow*>(rig.engine.rootObjects().front());
    QVERIFY(rig.window);
    // Les outils de dessin vivent dans l'espace Vecteurs (docs/desktop-v2/23-espaces-studio.md).
    auto* workspace = rig.window->findChild<WorkspaceController*>();
    QVERIFY(workspace);
    workspace->setCurrent(QStringLiteral("vector"));
    QTRY_VERIFY_WITH_TIMEOUT(
        rig.window->findChild<GpuCanvas*>(QStringLiteral("vectorCanvas")) != nullptr, 5'000);
    rig.canvas = rig.window->findChild<GpuCanvas*>(QStringLiteral("vectorCanvas"));
    QVERIFY(rig.canvas);
    rig.session.replaceProject(anchorProject(), true);
    settle(rig);
    // Le canevas de l'espace Vecteurs ne construit pas de points : prêt = formes publiées.
    QTRY_VERIFY_WITH_TIMEOUT(rig.canvas->vectorImage() != nullptr && rig.canvas->width() > 0,
                             10'000);
    rig.tools.setSnapVertices(false);
}

void StudioToolsTest::settle(Rig& rig) {
    QTRY_VERIFY_WITH_TIMEOUT(!rig.controller.busy() && rig.controller.scene() &&
                                 rig.controller.scene()->revision == rig.session.revision(),
                             15'000);
}

void StudioToolsTest::toolRailMatchesRegistry() {
    Rig rig;
    open(rig);
    const auto registry = ToolRegistry::builtin();
    const auto list = rig.tools.tools();
    QVERIFY(list.size() >= 8);
    for (const auto& entry : list) {
        const auto map = entry.toMap();
        const auto id = map.value(QStringLiteral("id")).toString();
        const auto* descriptor = registry.find(id.toStdString());
        QVERIFY2(descriptor, qPrintable(id));
        QCOMPARE(map.value(QStringLiteral("shortcut")).toString().toStdString(),
                 descriptor->shortcut);
        QVERIFY2(deepItem(rig.window->contentItem(), QStringLiteral("tool_") + id), qPrintable(id));
    }
    // Un clic sur le rail active l'outil ; un second clic revient à la sélection.
    auto* rectButton = deepItem(rig.window->contentItem(), QStringLiteral("tool_draw.rectangle"));
    QVERIFY(rectButton);
    const auto centre =
        rectButton->mapToScene(QPointF(rectButton->width() / 2, rectButton->height() / 2))
            .toPoint();
    QTest::mouseClick(rig.window, Qt::LeftButton, {}, centre);
    QCOMPARE(rig.tools.activeTool(), QStringLiteral("draw.rectangle"));
    QVERIFY(rig.tools.capturesMouse());
    QTest::mouseClick(rig.window, Qt::LeftButton, {}, centre);
    QCOMPARE(rig.tools.activeTool(), QStringLiteral("select"));
    // Raccourcis lus dans le registre.
    QTest::keyClick(rig.window, Qt::Key_E);
    QCOMPARE(rig.tools.activeTool(), QStringLiteral("draw.ellipse"));
    QTest::keyClick(rig.window, Qt::Key_Y, Qt::ShiftModifier);
    QCOMPARE(rig.tools.activeTool(), QStringLiteral("draw.polygon-regular"));
    QTest::keyClick(rig.window, Qt::Key_P);
    QCOMPARE(rig.tools.activeTool(), QStringLiteral("draw.bezier"));
    QTest::keyClick(rig.window, Qt::Key_B);
    QCOMPARE(rig.tools.activeTool(), QStringLiteral("node.edit"));
    QTest::keyClick(rig.window, Qt::Key_K);
    QCOMPARE(rig.tools.activeTool(), QStringLiteral("knife"));
    rig.tools.setActiveTool(
        QStringLiteral("bucket")); // pas encore disponible : refus, pas de changement
    QCOMPARE(rig.tools.activeTool(), QStringLiteral("knife"));
    QVERIFY(rig.tools.message().contains(QStringLiteral("pas encore")));
    QVERIFY(!rig.tools.hint().isEmpty());
    QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
}

void StudioToolsTest::basicShapesByMouse() {
    Rig rig;
    open(rig);
    const auto base = objects(rig);
    const auto baseSteps = steps(rig);

    const auto rect = draw(rig, "draw.rectangle", {-10, -6}, {10, 6});
    QVERIFY(rect != 0);
    QCOMPARE(objects(rig), base + 1);
    const auto rb = boundsOf(rig.session.project(), rect);
    QVERIFY(std::abs(rb.width() - 20.0) < 0.4);
    QVERIFY(std::abs(rb.height() - 12.0) < 0.4);
    QCOMPARE(nodeCount(rig.session.project(), rect), std::size_t{4});

    const auto ellipse = draw(rig, "draw.ellipse", {-12, 14}, {12, 24});
    QVERIFY(ellipse != 0);
    const auto eb = boundsOf(rig.session.project(), ellipse);
    QVERIFY(std::abs(eb.width() - 24.0) < 0.6);
    QVERIFY(std::abs(eb.height() - 10.0) < 0.6);
    QCOMPARE(steps(rig), baseSteps + 2);

    // Maj : carré ; une seule étape d'annulation par geste.
    const auto square = draw(rig, "draw.rectangle", {-25, -25}, {-15, -20}, Qt::ShiftModifier);
    QVERIFY(square != 0);
    const auto sb = boundsOf(rig.session.project(), square);
    QVERIFY(std::abs(sb.width() - sb.height()) < 0.4);

    // Un glisser minuscule ne crée rien.
    const auto before = steps(rig);
    rig.tools.setActiveTool(QStringLiteral("draw.rectangle"));
    drag(rig, at(rig, 20, 20), at(rig, 20, 20) + QPoint(1, 1));
    QCOMPARE(steps(rig), before);

    // Annulation : un Ctrl+Z par forme.
    QTest::keyClick(rig.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(objects(rig), base + 2);
    QTest::keyClick(rig.window, Qt::Key_Z, Qt::ControlModifier);
    QTest::keyClick(rig.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(objects(rig), base);
    QCOMPARE(steps(rig), baseSteps);
    QTest::keyClick(rig.window, Qt::Key_Y, Qt::ControlModifier);
    QTest::keyClick(rig.window, Qt::Key_Y, Qt::ControlModifier);
    QTest::keyClick(rig.window, Qt::Key_Y, Qt::ControlModifier);
    QCOMPARE(objects(rig), base + 3);

    // Persistance OSP.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = std::filesystem::path(directory.path().toStdWString()) / "tools.osp";
    QVERIFY(project_io::save_project(path, rig.session.project()).has_value());
    const auto reopened = project_io::load_project(path);
    QVERIFY(reopened.has_value());
    QCOMPARE(reopened->embroidery_objects.size(), base + 3);
    QCOMPARE(nodeCount(*reopened, ellipse), nodeCount(rig.session.project(), ellipse));
    QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
}

void StudioToolsTest::polygonAndRegularShapes() {
    Rig rig;
    open(rig);
    const auto baseSteps = steps(rig);

    // Polygone : clics successifs terminés par Entrée.
    rig.tools.setActiveTool(QStringLiteral("draw.polygon"));
    const QPointF pts[] = {{-20, -10}, {-5, -12}, {-2, 5}, {-18, 8}};
    for (const auto& p : pts) {
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, p.x(), p.y()));
    }
    QVERIFY(rig.tools.busy());
    QCOMPARE(steps(rig), baseSteps); // rien n'est créé avant la validation
    QVERIFY(!rig.canvas->toolOverlay().isEmpty());
    QTest::keyClick(rig.window, Qt::Key_Return);
    QCOMPARE(steps(rig), baseSteps + 1);
    QVERIFY(!rig.tools.busy());
    auto id = rig.session.selection().targets().front().id;
    QCOMPARE(nodeCount(rig.session.project(), id), std::size_t{4});

    // Double-clic : ferme le polygone sur le dernier sommet.
    const QPointF tri[] = {{5, -10}, {20, -10}};
    for (const auto& p : tri) {
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, p.x(), p.y()));
    }
    QTest::mouseDClick(rig.window, Qt::LeftButton, {}, at(rig, 12, 6));
    QCOMPARE(steps(rig), baseSteps + 2);
    id = rig.session.selection().targets().front().id;
    QCOMPARE(nodeCount(rig.session.project(), id), std::size_t{3});

    // Polygone régulier : 6 côtés, puis étoile à 5 branches.
    rig.tools.setSides(6);
    id = draw(rig, "draw.polygon-regular", {12, 18}, {20, 18});
    QVERIFY(id != 0);
    QCOMPARE(nodeCount(rig.session.project(), id), std::size_t{6});
    const auto hex = boundsOf(rig.session.project(), id);
    QVERIFY(hex.width() > 14.0 && hex.width() < 16.5);
    rig.tools.setSides(5);
    rig.tools.setStar(true);
    id = draw(rig, "draw.polygon-regular", {-12, 22}, {-12, 28});
    QVERIFY(id != 0);
    QCOMPARE(nodeCount(rig.session.project(), id), std::size_t{10});
    QCOMPARE(steps(rig), baseSteps + 4);
    rig.tools.setStar(false);

    // Un polygone à deux sommets est refusé sans rien muter.
    rig.tools.setActiveTool(QStringLiteral("draw.polygon"));
    QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, 0, 0));
    QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, 10, 0));
    QTest::keyClick(rig.window, Qt::Key_Return);
    QCOMPARE(steps(rig), baseSteps + 4);
    QVERIFY(rig.tools.message().contains(QStringLiteral("3 sommets")));
    QVERIFY(rig.tools.cancel());

    QTemporaryDir directory;
    const auto path = std::filesystem::path(directory.path().toStdWString()) / "poly.osp";
    QVERIFY(project_io::save_project(path, rig.session.project()).has_value());
    const auto reopened = project_io::load_project(path);
    QVERIFY(reopened.has_value());
    QCOMPARE(nodeCount(*reopened, id), std::size_t{10});
    QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
}

void StudioToolsTest::penBezierAndFreehand() {
    Rig rig;
    open(rig);
    const auto baseSteps = steps(rig);

    rig.tools.setActiveTool(QStringLiteral("draw.bezier"));
    QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, -12, -8));
    drag(rig, at(rig, 12, -8), at(rig, 20, -8)); // clic-glisser : nœud lisse
    QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, 12, 8));
    QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, -12, 8));
    QVERIFY(rig.tools.busy());
    QCOMPARE(steps(rig), baseSteps);
    QTest::keyClick(rig.window, Qt::Key_Return);
    QCOMPARE(steps(rig), baseSteps + 1);
    auto id = rig.session.selection().targets().front().id;
    const auto* shape = shapeFor(rig.session.project(), id);
    QVERIFY(shape);
    const auto& nodes = shape->paths.front().outer.nodes;
    QCOMPARE(nodes.size(), std::size_t{4});
    QVERIFY(nodes[1].tan_out.has_value());  // le glisser a posé une tangente
    QVERIFY(!nodes[0].tan_out.has_value()); // les clics simples restent des coins

    // Fermeture par clic sur le premier nœud (3 nœuds).
    QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, -10, 16));
    QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, 10, 16));
    QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, 0, 28));
    QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, -10, 16));
    QCOMPARE(steps(rig), baseSteps + 2);
    QCOMPARE(nodeCount(rig.session.project(), rig.session.selection().targets().front().id),
             std::size_t{3});

    // Main levée : cercle de 10 mm de rayon.
    rig.tools.setActiveTool(QStringLiteral("draw.freeform"));
    const auto start = at(rig, 40, 0);
    QTest::mousePress(rig.window, Qt::LeftButton, {}, start);
    constexpr int kSteps = 36;
    QPoint last = start;
    for (int i = 1; i <= kSteps; ++i) {
        const double a = 6.283185307179586 * i / kSteps;
        last = at(rig, 30 + 10 * std::cos(a), 10 * std::sin(a));
        QTest::mouseMove(rig.window, last);
    }
    QTest::mouseRelease(rig.window, Qt::LeftButton, {}, last);
    QCOMPARE(steps(rig), baseSteps + 3);
    id = rig.session.selection().targets().front().id;
    const auto fb = boundsOf(rig.session.project(), id);
    QVERIFY(std::abs(fb.width() - 20.0) < 2.0);

    // Annulation en une étape chacune, puis persistance.
    QTemporaryDir directory;
    const auto path = std::filesystem::path(directory.path().toStdWString()) / "pen.osp";
    QVERIFY(project_io::save_project(path, rig.session.project()).has_value());
    const auto reopened = project_io::load_project(path);
    QVERIFY(reopened.has_value());
    QCOMPARE(nodeCount(*reopened, id), nodeCount(rig.session.project(), id));
    for (int i = 0; i < 3; ++i) {
        QTest::keyClick(rig.window, Qt::Key_Z, Qt::ControlModifier);
    }
    QCOMPARE(steps(rig), baseSteps);
    QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
}

void StudioToolsTest::escapeCancelsGestures() {
    Rig rig;
    open(rig);
    const auto baseSteps = steps(rig);
    rig.tools.setActiveTool(QStringLiteral("draw.rectangle"));
    QTest::mousePress(rig.window, Qt::LeftButton, {}, at(rig, -10, -10));
    QTest::mouseMove(rig.window, at(rig, 0, 0));
    QTest::mouseMove(rig.window, at(rig, 10, 10));
    QVERIFY(rig.tools.busy());
    QVERIFY(!rig.canvas->toolOverlay().isEmpty()); // aperçu pendant le geste, sans mutation
    QVERIFY(!rig.canvas->gestureInfo().isEmpty());
    QCOMPARE(steps(rig), baseSteps);
    QTest::keyClick(rig.window, Qt::Key_Escape);
    QVERIFY(!rig.tools.busy());
    QTest::mouseRelease(rig.window, Qt::LeftButton, {}, at(rig, 10, 10));
    QCOMPARE(steps(rig), baseSteps);
    QVERIFY(rig.canvas->toolOverlay().isEmpty());

    // Plume en cours : Échap abandonne la construction.
    rig.tools.setActiveTool(QStringLiteral("draw.bezier"));
    QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, -5, -5));
    QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, 5, -5));
    QVERIFY(rig.tools.busy());
    QTest::keyClick(rig.window, Qt::Key_Escape);
    QVERIFY(!rig.tools.busy());
    QCOMPARE(steps(rig), baseSteps);

    // Retour arrière : retire le dernier sommet du polygone.
    rig.tools.setActiveTool(QStringLiteral("draw.polygon"));
    for (const QPointF p : {QPointF{-5, -5}, QPointF{5, -5}, QPointF{5, 5}, QPointF{-5, 5}}) {
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, p.x(), p.y()));
    }
    QTest::keyClick(rig.window, Qt::Key_Backspace);
    QTest::keyClick(rig.window, Qt::Key_Return);
    QCOMPARE(steps(rig), baseSteps + 1);
    QCOMPARE(nodeCount(rig.session.project(), rig.session.selection().targets().front().id),
             std::size_t{3});
    QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
}

// Régression : en outil Sélection, glisser un nœud de la forme sélectionnée déplace ce nœud
// (et non une forme voisine, ni ne désélectionne).
void StudioToolsTest::nodeDragFromSelectTool() {
    Rig rig;
    open(rig);
    const auto id = draw(rig, "draw.rectangle", {-12, -8}, {12, 8});
    QVERIFY(id != 0);
    rig.tools.setActiveTool(QStringLiteral("select"));
    // The interactive top layer must remain, even after removal of the old
    // white/pink vector-selection texture below it.
    const auto upperOverlay = rig.canvas->toolOverlay();
    const bool hasNodes = std::ranges::any_of(upperOverlay, [](const QVariant& value) {
        const auto kind = value.toMap().value(QStringLiteral("kind")).toString();
        return kind == QStringLiteral("point") || kind == QStringLiteral("selected");
    });
    QVERIFY(hasNodes);
    const auto before = nodeView(rig.session.project(), id);
    QVERIFY(before.has_value());
    const auto p0 = before->nodes[0].pos;
    const auto baseSteps = steps(rig);
    drag(rig, at(rig, p0.x, p0.y), at(rig, p0.x, p0.y) + QPoint(40, -30));
    QCOMPARE(steps(rig), baseSteps + 1);
    const auto after = nodeView(rig.session.project(), id);
    QVERIFY(after.has_value());
    QVERIFY(std::abs(after->nodes[0].pos.x - p0.x) > 1.0);
    QCOMPARE(static_cast<int>(rig.session.selection().targets().size()), 1);
}

void StudioToolsTest::offscreenNodeHandlesDoNotFillQmlCanvas() {
    Rig rig;
    open(rig);
    const auto id = draw(rig, "draw.rectangle", {-12, -8}, {12, 8});
    QVERIFY(id != 0);
    rig.tools.setActiveTool(QStringLiteral("select"));
    const auto countMarkers = [](const QVariantList& items) {
        int markers = 0;
        for (const auto& item : items) {
            const auto kind = item.toMap().value(QStringLiteral("kind")).toString();
            if (kind == QStringLiteral("point") || kind == QStringLiteral("selected")) {
                ++markers;
            }
        }
        return markers;
    };
    const auto visible = rig.canvas->toolOverlay();
    QVERIFY(countMarkers(visible) >= 4);
    rig.canvas->panBy(20'000.0, 20'000.0);
    const auto outside = rig.canvas->toolOverlay();
    QCOMPARE(countMarkers(outside), 0);
    // Keep the outline even when both endpoints are outside the screen: it
    // may cross the viewport in other pan/zoom configurations.
    QVERIFY(!outside.isEmpty());
    QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
}

void StudioToolsTest::nodeEditByMouse() {
    Rig rig;
    open(rig);
    const auto id = draw(rig, "draw.rectangle", {-12, -8}, {12, 8});
    QVERIFY(id != 0);
    const auto baseSteps = steps(rig);
    rig.tools.setActiveTool(QStringLiteral("node.edit"));
    auto view = nodeView(rig.session.project(), id);
    QVERIFY(view.has_value());
    QCOMPARE(view->nodes.size(), std::size_t{4});
    const auto before = view->nodes[0].pos;

    // Sélection puis déplacement d'un nœud : UNE étape.
    const auto from = at(rig, before.x, before.y);
    drag(rig, from, from + QPoint(40, -30));
    QCOMPARE(steps(rig), baseSteps + 1);
    auto after = nodeView(rig.session.project(), id);
    QVERIFY(after.has_value());
    QVERIFY(std::abs(after->nodes[0].pos.x - before.x) > 1.0);
    QCOMPARE(rig.tools.selectedNodeCount(), 1);

    // Échap pendant un glissement : aucune mutation.
    const auto moved = at(rig, after->nodes[0].pos.x, after->nodes[0].pos.y);
    QTest::mousePress(rig.window, Qt::LeftButton, {}, moved);
    QTest::mouseMove(rig.window, moved + QPoint(30, 30));
    QVERIFY(rig.tools.busy());
    QTest::keyClick(rig.window, Qt::Key_Escape);
    QTest::mouseRelease(rig.window, Qt::LeftButton, {}, moved + QPoint(30, 30));
    QCOMPARE(steps(rig), baseSteps + 1);
    after = nodeView(rig.session.project(), id);
    QVERIFY(std::abs(after->nodes[0].pos.x - before.x) > 1.0);

    // Annuler ramène le nœud d'origine.
    QTest::keyClick(rig.window, Qt::Key_Z, Qt::ControlModifier);
    after = nodeView(rig.session.project(), id);
    QVERIFY(std::abs(after->nodes[0].pos.x - before.x) < 0.05);
    QCOMPARE(steps(rig), baseSteps);
    QTest::keyClick(rig.window, Qt::Key_Y, Qt::ControlModifier);

    // Insertion d'un nœud par double-clic sur un segment, puis suppression au clavier.
    const auto v2 = nodeView(rig.session.project(), id);
    const auto a = v2->nodes[1].pos;
    const auto b = v2->nodes[2].pos;
    const auto mid = at(rig, (a.x + b.x) / 2, (a.y + b.y) / 2);
    QTest::mouseDClick(rig.window, Qt::LeftButton, {}, mid);
    QCOMPARE(nodeCount(rig.session.project(), id), std::size_t{5});
    QCOMPARE(steps(rig), baseSteps + 2);
    const auto v3 = nodeView(rig.session.project(), id);
    QTest::mouseClick(rig.window, Qt::LeftButton, {},
                      at(rig, v3->nodes[2].pos.x, v3->nodes[2].pos.y));
    QCOMPARE(rig.tools.selectedNodeCount(), 1);
    QTest::keyClick(rig.window, Qt::Key_Delete);
    QCOMPARE(nodeCount(rig.session.project(), id), std::size_t{4});
    QCOMPARE(rig.session.project().embroidery_objects.size(),
             objects(rig)); // l'objet n'est pas supprimé
    QCOMPARE(steps(rig), baseSteps + 3);

    // Lisser / anguleux sur un nœud choisi.
    const auto v4 = nodeView(rig.session.project(), id);
    QTest::mouseClick(rig.window, Qt::LeftButton, {},
                      at(rig, v4->nodes[1].pos.x, v4->nodes[1].pos.y));
    QVERIFY(rig.tools.setSelectedNodeKind(true));
    const auto v5 = nodeView(rig.session.project(), id);
    QVERIFY(v5->nodes[1].smooth);
    QCOMPARE(steps(rig), baseSteps + 4);

    // Courber un segment : glisser son milieu.
    const auto s0 = v5->nodes[2].pos;
    const auto s1 = v5->nodes[3].pos;
    const auto segMid = at(rig, (s0.x + s1.x) / 2, (s0.y + s1.y) / 2);
    drag(rig, segMid, segMid + QPoint(0, 25));
    QCOMPARE(steps(rig), baseSteps + 5);
    QVERIFY(nodeView(rig.session.project(), id)->tangents.size() > v5->tangents.size());

    // Persistance OSP du contour modifié.
    QTemporaryDir directory;
    const auto path = std::filesystem::path(directory.path().toStdWString()) / "nodes.osp";
    QVERIFY(project_io::save_project(path, rig.session.project()).has_value());
    const auto reopened = project_io::load_project(path);
    QVERIFY(reopened.has_value());
    const auto* live = shapeFor(rig.session.project(), id);
    const auto* disk = shapeFor(*reopened, id);
    QVERIFY(live && disk);
    QVERIFY(live->paths.front().outer == disk->paths.front().outer);
    QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
}

void StudioToolsTest::knifeAndBooleans() {
    Rig rig;
    open(rig);
    const auto a = draw(rig, "draw.rectangle", {-15, -10}, {5, 10});
    const auto b = draw(rig, "draw.rectangle", {-5, -10}, {15, 10});
    QVERIFY(a != 0 && b != 0);
    const auto baseSteps = steps(rig);
    const auto baseObjects = objects(rig);
    auto selectBoth = [&] {
        rig.session.selection().replace(
            {Target{TargetKind::EmbroideryObject, a}, Target{TargetKind::EmbroideryObject, b}});
    };

    struct Case {
        const char* mode;
        double width;
    };
    for (const auto& c : {Case{"union", 30.0}, Case{"intersect", 10.0}}) {
        selectBoth();
        QVERIFY2(rig.vectors.booleanOp(QString::fromLatin1(c.mode)), c.mode);
        QCOMPARE(steps(rig), baseSteps + 1);
        const auto selection = rig.session.selection().targets();
        QVERIFY(!selection.empty());
        double minX = 1e9, maxX = -1e9;
        for (const auto& t : selection) {
            const auto box = boundsOf(rig.session.project(), t.id);
            minX = std::min(minX, box.x0);
            maxX = std::max(maxX, box.x1);
        }
        QVERIFY2(std::abs((maxX - minX) - c.width) < 0.3, c.mode);
        QTest::keyClick(rig.window, Qt::Key_Z, Qt::ControlModifier); // une seule étape à défaire
        QCOMPARE(steps(rig), baseSteps);
        QCOMPARE(objects(rig), baseObjects);
    }
    for (const char* mode : {"subtract", "exclude"}) {
        selectBoth();
        QVERIFY2(rig.vectors.booleanOp(QString::fromLatin1(mode)), mode);
        QCOMPARE(steps(rig), baseSteps + 1);
        QTest::keyClick(rig.window, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(steps(rig), baseSteps);
    }
    // Refus : une seule forme sélectionnée, ou mode inconnu ; rien n'est muté.
    rig.session.selection().replace({Target{TargetKind::EmbroideryObject, a}});
    QVERIFY(!rig.vectors.booleanOp(QStringLiteral("union")));
    QVERIFY(!rig.vectors.booleanOp(QStringLiteral("inconnu")));
    QCOMPARE(steps(rig), baseSteps);

    // Fusion, simplification, lissage, séparation : une étape chacune.
    selectBoth();
    QVERIFY(rig.vectors.mergeObjects());
    QCOMPARE(steps(rig), baseSteps + 1);
    QVERIFY(rig.vectors.simplify(0.1));
    QCOMPARE(steps(rig), baseSteps + 2);
    QTest::keyClick(rig.window, Qt::Key_Z, Qt::ControlModifier);
    QTest::keyClick(rig.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(steps(rig), baseSteps);
    QCOMPARE(objects(rig), baseObjects);

    // Couteau : sans sélection, refus ; avec, découpe en plusieurs objets en une étape.
    rig.session.selection().clear();
    rig.tools.setActiveTool(QStringLiteral("knife"));
    drag(rig, at(rig, 0, -18), at(rig, 0, 18));
    QCOMPARE(steps(rig), baseSteps);
    QVERIFY(rig.tools.message().contains(QStringLiteral("sélectionnez")));
    rig.session.selection().replace({Target{TargetKind::EmbroideryObject, a}});
    drag(rig, at(rig, -5, -18), at(rig, -5, 18));
    QCOMPARE(steps(rig), baseSteps + 1);
    QVERIFY(objects(rig) > baseObjects);
    QVERIFY(!rig.session.selection().targets().empty());
    QTemporaryDir directory;
    const auto path = std::filesystem::path(directory.path().toStdWString()) / "knife.osp";
    QVERIFY(project_io::save_project(path, rig.session.project()).has_value());
    const auto reopened = project_io::load_project(path);
    QVERIFY(reopened.has_value());
    QCOMPARE(reopened->embroidery_objects.size(), objects(rig));
    QTest::keyClick(rig.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(steps(rig), baseSteps);
    QCOMPARE(objects(rig), baseObjects);
    QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
}

void StudioToolsTest::snappingIsConfigurable() {
    Rig rig;
    open(rig);
    // Grille : les coins tombent sur des multiples du pas.
    rig.tools.setSnapGrid(true);
    rig.tools.setGridMm(5.0);
    const auto id = draw(rig, "draw.rectangle", {-12.4, -7.3}, {11.6, 8.8});
    QVERIFY(id != 0);
    const auto box = boundsOf(rig.session.project(), id);
    for (const double v : {box.x0, box.x1, box.y0, box.y1}) {
        QVERIFY2(std::abs(v / 5.0 - std::round(v / 5.0)) < 0.01, qPrintable(QString::number(v)));
    }
    rig.tools.setSnapGrid(false);

    // Sommets : un coin tracé près d'un sommet existant s'y accroche exactement ; rayon
    // configurable.
    rig.tools.setSnapVertices(true);
    rig.tools.setSnapRadiusPx(12);
    const auto target = boundsOf(rig.session.project(), id);
    const auto near = at(rig, target.x1, target.y1) + QPoint(4, 3);
    rig.tools.setActiveTool(QStringLiteral("draw.rectangle"));
    drag(rig, at(rig, 20, 15), near);
    const auto created = rig.session.selection().targets().front().id;
    const auto cb = boundsOf(rig.session.project(), created);
    QVERIFY(std::abs(cb.x0 - target.x1) < 0.01 || std::abs(cb.x1 - target.x1) < 0.01);
    QVERIFY(std::abs(cb.y0 - target.y1) < 0.01 || std::abs(cb.y1 - target.y1) < 0.01);
    // Désactivé : le même geste ne s'accroche plus.
    rig.tools.setSnapVertices(false);
    drag(rig, at(rig, 21, 16), near);
    const auto free = boundsOf(rig.session.project(), rig.session.selection().targets().front().id);
    QVERIFY(std::abs(free.x0 - target.x1) > 0.01 && std::abs(free.x1 - target.x1) > 0.01);
    QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
}

QTEST_MAIN(StudioToolsTest)
#include "test_studio_tools.moc"
