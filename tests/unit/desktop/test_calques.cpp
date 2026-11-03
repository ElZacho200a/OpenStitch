// SPDX-License-Identifier: Apache-2.0
//
// HP-OBJ-024 : calques du document dans l'interface -- panneau Calques, calque actif, déplacement
// de la sélection, ordre de couture dans le panneau Ordre, calque masqué / verrouillé au canevas.
#include <QApplication>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>
#include <QTreeWidget>

#include "calque_panel.hpp"
#include "main_window.hpp"
#include "openstitch/commands/calque_commands.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/geometry/primitives.hpp"

namespace openstitch::desktop {
namespace {

Vec2um vec(std::int32_t x, std::int32_t y) {
    return {Micrometers{x}, Micrometers{y}};
}

struct Fixture {
    document::Project project;
    ObjectId shapeA;
    ObjectId shapeB;
    ObjectId embA;
    ObjectId embB;
};

// Deux formes côte à côte, chacune avec un objet de broderie (A puis B dans le document).
Fixture twoShapes() {
    Fixture f;
    auto& p = f.project;
    for (int i = 0; i < 2; ++i) {
        document::VectorObject v;
        v.id = p.object_ids.next();
        v.name = i == 0 ? "A" : "B";
        v.paths = {
            {geometry::rectangle_path(vec(i * 20'000, -10'000), vec(i * 20'000 + 10'000, 0)), {}}};
        (i == 0 ? f.shapeA : f.shapeB) = v.id;
        p.vector_objects.push_back(v);
        document::EmbroideryObject e;
        e.id = p.object_ids.next();
        e.name = i == 0 ? "Emb A" : "Emb B";
        e.source_vector = v.id;
        e.rgb = {static_cast<std::uint8_t>(60 + i * 100), 30, 30};
        e.params = document::TatamiParams{};
        (i == 0 ? f.embA : f.embB) = e.id;
        p.embroidery_objects.push_back(e);
    }
    return f;
}

QTreeWidget* calqueTree(CalquePanel& panel) {
    return panel.findChild<QTreeWidget*>(QStringLiteral("tree_calques"));
}

} // namespace

class MainWindowTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchTests"));
        QCoreApplication::setApplicationName(QStringLiteral("Calques"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
    }
    void panelShowsTheImplicitCalqueThenTheStackTopFirst();
    void panelEmitsToggleAddAndActivateRequests();
    void addingTheFirstCalqueMaterialisesTheImplicitOneAndUndoes();
    void newShapesEnterTheActiveCalque();
    void moveSelectionToCalqueMovesShapesInOneUndoableStep();
    void orderPanelFollowsCalquesAndStopsMovesAtTheirBoundary();
    void hiddenOrLockedCalqueShapesAreNotPickedAtTheCanvas();
};

void MainWindowTest::panelShowsTheImplicitCalqueThenTheStackTopFirst() {
    CalquePanel panel;
    document::Project project;
    panel.refresh(project);
    auto* tree = calqueTree(panel);
    QVERIFY(tree != nullptr);
    QCOMPARE(tree->topLevelItemCount(), 1); // calque implicite
    QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Calque 1"));
    QCOMPARE(tree->topLevelItem(0)->data(0, Qt::UserRole).toULongLong(), qulonglong{0});
    QVERIFY(panel.findChild<QPushButton*>(QStringLiteral("button_calqueAdd"))->isEnabled());
    QVERIFY(!panel.findChild<QPushButton*>(QStringLiteral("button_calqueRemove"))->isEnabled());

    project.calques = {{project.object_ids.next(), "Fond", true, false},
                       {project.object_ids.next(), "Effets", false, true}};
    project.active_calque = project.calques[1].id;
    panel.refresh(project);
    QCOMPARE(tree->topLevelItemCount(), 2);
    QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Effets")); // le dessus d'abord
    QCOMPARE(tree->topLevelItem(1)->text(0), QStringLiteral("Fond"));
    QCOMPARE(tree->topLevelItem(0)->checkState(1), Qt::Unchecked); // masqué
    QCOMPARE(tree->topLevelItem(0)->checkState(2), Qt::Checked);   // verrouillé
    QVERIFY(tree->topLevelItem(0)->font(0).bold());                // calque actif
    QVERIFY(!tree->topLevelItem(1)->font(0).bold());
    QVERIFY(panel.findChild<QPushButton*>(QStringLiteral("button_calqueRemove"))->isEnabled());
    // Le calque du dessus ne peut plus monter ; celui du fond ne peut plus descendre.
    QVERIFY(!panel.findChild<QPushButton*>(QStringLiteral("button_calqueUp"))->isEnabled());
    QVERIFY(panel.findChild<QPushButton*>(QStringLiteral("button_calqueDown"))->isEnabled());
}

void MainWindowTest::panelEmitsToggleAddAndActivateRequests() {
    CalquePanel panel;
    document::Project project;
    const ObjectId fond = project.object_ids.next();
    const ObjectId effets = project.object_ids.next();
    project.calques = {{fond, "Fond", true, false}, {effets, "Effets", true, false}};
    project.active_calque = effets;
    panel.refresh(project);
    auto* tree = calqueTree(panel);

    std::optional<std::pair<std::uint64_t, bool>> visibility;
    std::optional<std::uint64_t> activated;
    int adds = 0;
    QObject::connect(&panel, &CalquePanel::calqueVisibilityToggled, &panel,
                     [&](ObjectId id, bool on) { visibility = {id.value, on}; });
    QObject::connect(&panel, &CalquePanel::calqueActivated, &panel,
                     [&](ObjectId id) { activated = id.value; });
    QObject::connect(&panel, &CalquePanel::addCalqueRequested, &panel, [&] { ++adds; });

    tree->topLevelItem(0)->setCheckState(1, Qt::Unchecked); // « Effets » masqué
    QTRY_VERIFY(visibility.has_value());
    QCOMPARE(*visibility, (std::pair<std::uint64_t, bool>{effets.value, false}));

    tree->clearSelection();
    tree->topLevelItem(1)->setSelected(true); // « Fond » devient actif
    QTRY_VERIFY(activated.has_value());
    QCOMPARE(*activated, fond.value);

    panel.findChild<QPushButton*>(QStringLiteral("button_calqueAdd"))->click();
    QCOMPARE(adds, 1);
}

void MainWindowTest::addingTheFirstCalqueMaterialisesTheImplicitOneAndUndoes() {
    MainWindow w;
    auto f = twoShapes();
    w.applyLoadedProject(std::move(f.project));
    QVERIFY(w.project_.calques.empty());
    w.addCalque();
    QCOMPARE(w.project_.calques.size(), std::size_t{2});
    QCOMPARE(QString::fromStdString(w.project_.calques[0].name), QStringLiteral("Calque 1"));
    QCOMPARE(w.project_.active_calque, w.project_.calques[1].id); // le nouveau est actif
    // Les formes existantes restent dans le calque du fond.
    QCOMPARE(w.project_.calqueOfShape(w.project_.vector_objects[0].id)->id,
             w.project_.calques[0].id);
    w.undoAct_->trigger();
    QVERIFY(w.project_.calques.empty());
}

void MainWindowTest::newShapesEnterTheActiveCalque() {
    MainWindow w;
    auto f = twoShapes();
    w.applyLoadedProject(std::move(f.project));
    w.addCalque();
    const ObjectId active = w.project_.active_calque;
    document::VectorObject fresh;
    fresh.id = w.project_.object_ids.next();
    w.undoStack_.execute(std::make_unique<commands::AddVectorObjectCommand>(fresh), w.project_);
    QCOMPARE(w.project_.findObject(fresh.id)->calque, active);
}

void MainWindowTest::moveSelectionToCalqueMovesShapesInOneUndoableStep() {
    MainWindow w;
    auto f = twoShapes();
    const ObjectId shapeB = f.shapeB;
    w.applyLoadedProject(std::move(f.project));
    w.addCalque();
    const ObjectId top = w.project_.calques[1].id;
    w.setSelection({.objects = {shapeB}});
    const auto steps = w.undoStack_.undoNames().size();
    w.moveSelectionToCalque(top);
    QCOMPARE(w.project_.findObject(shapeB)->calque, top);
    QCOMPARE(w.undoStack_.undoNames().size(), steps + 1);
    w.undoAct_->trigger();
    QVERIFY(w.project_.findObject(shapeB)->calque != top);
}

void MainWindowTest::orderPanelFollowsCalquesAndStopsMovesAtTheirBoundary() {
    MainWindow w;
    auto f = twoShapes();
    const ObjectId shapeA = f.shapeA;
    const ObjectId embA = f.embA;
    const ObjectId embB = f.embB;
    w.applyLoadedProject(std::move(f.project));
    w.addCalque();
    // A (premier du document) monte dans le calque du dessus : B est cousu avant.
    w.setSelection({.objects = {shapeA}});
    w.moveSelectionToCalque(w.project_.calques[1].id);
    w.refreshOrderPanel();
    QCOMPARE(w.orderRows_.size(), std::size_t{2});
    QCOMPARE(w.project_.embroidery_objects[w.orderRows_[0]].id, embB);
    QCOMPARE(w.project_.embroidery_objects[w.orderRows_[1]].id, embA);
    // Chaque objet est seul dans son calque : Monter/Descendre sont grisés à la frontière.
    w.orderList_->setCurrentRow(0);
    QVERIFY(!w.orderUpBtn_->isEnabled());
    QVERIFY(!w.orderDownBtn_->isEnabled());
    w.orderList_->setCurrentRow(1);
    QVERIFY(!w.orderUpBtn_->isEnabled());
}

void MainWindowTest::hiddenOrLockedCalqueShapesAreNotPickedAtTheCanvas() {
    MainWindow w;
    auto f = twoShapes();
    const ObjectId shapeB = f.shapeB;
    w.applyLoadedProject(std::move(f.project));
    w.addCalque();
    w.setSelection({.objects = {shapeB}});
    w.moveSelectionToCalque(w.project_.calques[1].id);
    const QPointF center =
        w.objectPainterPath(*w.project_.findObject(shapeB)).boundingRect().center();
    QCOMPARE(w.objectAt(center).value_or(ObjectId{}), shapeB);

    const ObjectId calque = w.project_.calques[1].id;
    w.undoStack_.execute(std::make_unique<commands::SetCalqueLockedCommand>(calque, true),
                         w.project_);
    QVERIFY(!w.objectAt(center).has_value()); // verrouillé : non saisissable
    w.undoStack_.execute(std::make_unique<commands::SetCalqueLockedCommand>(calque, false),
                         w.project_);
    QVERIFY(w.objectAt(center).has_value());
    w.undoStack_.execute(std::make_unique<commands::SetCalqueVisibleCommand>(calque, false),
                         w.project_);
    QVERIFY(!w.objectAt(center).has_value()); // masqué : ni dessiné ni saisissable
    QVERIFY(!w.calqueShowsShape(*w.project_.findObject(shapeB)));
}

} // namespace openstitch::desktop
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    openstitch::desktop::MainWindowTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_calques.moc"
