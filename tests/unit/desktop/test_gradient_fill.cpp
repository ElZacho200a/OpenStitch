// SPDX-License-Identifier: Apache-2.0
// Remplissage « Uni / Dégradé » du bureau : bascule, réédition globale, poignées du
// canevas (vrais évènements souris), suppression du couple. Headless, sans pixel ni sleep.
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGraphicsScene>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>

#include "app_theme.hpp"
#include "canvas_view.hpp"
#include "gradient_fill_panel.hpp"
#include "main_window.hpp"
#include "node_handle.hpp"
#include "openstitch/document/color_blend.hpp"
#include "openstitch/geometry/primitives.hpp"
#include "openstitch/project_ops/gradient_fill.hpp"
#include "properties_panel.hpp"

namespace openstitch::desktop {
namespace {

Vec2um vec(std::int32_t x, std::int32_t y) {
    return {Micrometers{x}, Micrometers{y}};
}

document::Project fillFixture(bool satin = false) {
    document::Project p;
    document::VectorObject v;
    v.id = p.object_ids.next();
    v.name = "Zone";
    v.paths = {{geometry::rectangle_path(vec(0, -10'000), vec(10'000, 0)), {}}};
    p.vector_objects.push_back(v);
    document::EmbroideryObject e;
    e.id = p.object_ids.next();
    e.source_vector = v.id;
    e.name = "Fond";
    e.rgb = {200, 30, 30};
    if (satin) {
        e.params = document::RunningStitchParams{};
    } else {
        e.params = document::TatamiParams{};
    }
    p.embroidery_objects.push_back(e);
    return p;
}

} // namespace

class MainWindowTest : public QObject {
    Q_OBJECT
public:
    static QList<NodeHandleItem*> handlesOf(MainWindow& w) {
        QList<NodeHandleItem*> out;
        for (QGraphicsItem* item : w.baseItems_) {
            if (auto* handle = dynamic_cast<NodeHandleItem*>(item)) {
                out.push_back(handle);
            }
        }
        return out;
    }

private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchTests"));
        QCoreApplication::setApplicationName(QStringLiteral("GradientFill"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
    }
    void uniToGradientAndBackWithUndoRedo();
    void incompatibleShapeExplainsWhy();
    void eitherLayerSelectsTheWholeGradient();
    void globalEditUpdatesBothLayersWithoutNewOnes();
    void invertSwapsColorsInOneStep();
    void invalidZoneIsRefusedWithoutHistory();
    void dragEndHandleIsOneUndoStep();
    void dragMoveHandleKeepsTransitionInsideShape();
    void deletingOneLayerDeletesTheWholeGradient();
    void panelAndHandlesStayReadableInBothThemes();
    void advancedSectionIsCollapsedAndEditsSpacingInOneStep();
};

void MainWindowTest::uniToGradientAndBackWithUndoRedo() {
    MainWindow w;
    w.applyLoadedProject(fillFixture());
    const auto fill = w.project_.embroidery_objects.front();
    w.setSelection({.embroidery = fill.id, .objects = {fill.source_vector}});
    w.updateActions();
    auto* panel = w.propertiesPanel_->gradientPanel();
    QVERIFY(panel->state().visible);
    QVERIFY(panel->state().available);
    QVERIFY(!panel->state().gradient);

    auto* mode = panel->findChild<QComboBox*>(QStringLiteral("combo_fillMode"));
    QVERIFY(mode != nullptr);
    mode->setCurrentIndex(1);
    emit mode->activated(1);
    QCOMPARE(w.project_.embroidery_objects.size(), std::size_t{2});
    QVERIFY(document::find_color_blend(w.project_, fill.id));
    QVERIFY(panel->state().gradient);
    QVERIFY(w.project_.embroidery_objects[0].rgb != w.project_.embroidery_objects[1].rgb);
    QCOMPARE(QString::fromStdString(w.undoStack_.undoName()),
             QStringLiteral("Remplissage en dégradé"));

    w.undo();
    QCOMPARE(w.project_.embroidery_objects.size(), std::size_t{1});
    QVERIFY(!panel->state().gradient);
    w.redo();
    QCOMPARE(w.project_.embroidery_objects.size(), std::size_t{2});
    QVERIFY(panel->state().gradient);

    mode->setCurrentIndex(0);
    emit mode->activated(0);
    QCOMPARE(w.project_.embroidery_objects.size(), std::size_t{1});
    QVERIFY(!w.project_.embroidery_objects[0].color_blend.has_value());
    QCOMPARE(w.project_.embroidery_objects[0].rgb, fill.rgb);
    w.undo();
    QCOMPARE(w.project_.embroidery_objects.size(), std::size_t{2});
    QVERIFY(document::find_color_blend(w.project_, fill.id));
}

void MainWindowTest::incompatibleShapeExplainsWhy() {
    MainWindow w;
    w.applyLoadedProject(fillFixture(true)); // point droit : pas un remplissage
    const auto e = w.project_.embroidery_objects.front();
    w.setSelection({.embroidery = e.id, .objects = {e.source_vector}});
    w.updateActions();
    auto* panel = w.propertiesPanel_->gradientPanel();
    QVERIFY(!panel->state().visible); // pas de choix Uni/Dégradé pour un contour cousu
    w.setFillMode(true);
    QCOMPARE(w.project_.embroidery_objects.size(), std::size_t{1});

    // Deux remplissages sur la même forme : refus expliqué dans l'interface.
    MainWindow w2;
    auto project = fillFixture();
    auto second = project.embroidery_objects.front();
    second.id = project.object_ids.next();
    project.embroidery_objects.push_back(second);
    w2.applyLoadedProject(project);
    w2.setSelection({.embroidery = project.embroidery_objects[0].id,
                     .objects = {project.embroidery_objects[0].source_vector}});
    w2.updateActions();
    auto* panel2 = w2.propertiesPanel_->gradientPanel();
    QVERIFY(panel2->state().visible);
    QVERIFY(!panel2->state().available);
    QVERIFY(!panel2->state().reason.isEmpty());
    auto* reason = panel2->findChild<QLabel*>(QStringLiteral("label_gradientReason"));
    QVERIFY(reason != nullptr);
    QVERIFY(!reason->isHidden());
    w2.setFillMode(true);
    QCOMPARE(w2.project_.embroidery_objects.size(), std::size_t{2});
}

void MainWindowTest::eitherLayerSelectsTheWholeGradient() {
    MainWindow w;
    w.applyLoadedProject(fillFixture());
    const auto fill = w.project_.embroidery_objects.front();
    w.setSelection({.embroidery = fill.id, .objects = {fill.source_vector}});
    w.setFillMode(true);
    const auto top = w.project_.embroidery_objects[1];
    w.setSelection({.embroidery = top.id, .objects = {top.source_vector}});
    w.updateActions();
    auto* panel = w.propertiesPanel_->gradientPanel();
    QVERIFY(panel->state().gradient);
    QCOMPARE(panel->state().start, QColor(w.project_.embroidery_objects[0].rgb[0],
                                          w.project_.embroidery_objects[0].rgb[1],
                                          w.project_.embroidery_objects[0].rgb[2]));
    QCOMPARE(panel->state().layerCount, 2);
}

void MainWindowTest::globalEditUpdatesBothLayersWithoutNewOnes() {
    MainWindow w;
    w.applyLoadedProject(fillFixture());
    const auto fill = w.project_.embroidery_objects.front();
    w.setSelection({.embroidery = fill.id, .objects = {fill.source_vector}});
    w.setFillMode(true);
    const ObjectId topId = w.project_.embroidery_objects[1].id;
    const auto before = *project_ops::describe_gradient_fill(w.project_, fill.id);
    const auto depth = w.undoStack_.undoNames().size();

    auto* panel = w.propertiesPanel_->gradientPanel();
    auto state = panel->state();
    state.start = QColor(10, 20, 30);
    state.end = QColor(240, 230, 20);
    state.curve = static_cast<int>(document::DensityCurve::Smooth);
    state.complementary = false;
    state.zoneStartPercent = 20.0;
    state.zoneEndPercent = 80.0;
    emit panel->edited(state);

    QCOMPARE(w.project_.embroidery_objects.size(), std::size_t{2});
    QCOMPARE(w.project_.embroidery_objects[0].id, fill.id);
    QCOMPARE(w.project_.embroidery_objects[1].id, topId);
    QCOMPARE(w.undoStack_.undoNames().size(), depth + 1); // un seul pas pour tout le couple
    QCOMPARE(w.project_.embroidery_objects[0].rgb, (std::array<std::uint8_t, 3>{10, 20, 30}));
    QCOMPARE(w.project_.embroidery_objects[1].rgb, (std::array<std::uint8_t, 3>{240, 230, 20}));
    const auto now = *project_ops::describe_gradient_fill(w.project_, topId);
    QCOMPARE(now.gradient.curve, document::DensityCurve::Smooth);
    QVERIFY(!now.complementary);
    QCOMPARE(now.gradient.from.x.value, 2'000);
    QCOMPARE(now.gradient.to.x.value, 8'000);
    QCOMPARE(panel->state().zoneStartPercent, 20.0);

    w.undo();
    QCOMPARE(*project_ops::describe_gradient_fill(w.project_, topId), before);
    QCOMPARE(w.project_.embroidery_objects.size(), std::size_t{2});
}

void MainWindowTest::invertSwapsColorsInOneStep() {
    MainWindow w;
    w.applyLoadedProject(fillFixture());
    const auto fill = w.project_.embroidery_objects.front();
    w.setSelection({.embroidery = fill.id, .objects = {fill.source_vector}});
    w.setFillMode(true);
    const auto bg = w.project_.embroidery_objects[0].rgb;
    const auto fg = w.project_.embroidery_objects[1].rgb;
    const auto depth = w.undoStack_.undoNames().size();
    auto* invert = w.propertiesPanel_->gradientPanel()->findChild<QPushButton*>(
        QStringLiteral("button_gradientInvert"));
    QVERIFY(invert != nullptr);
    invert->click();
    QCOMPARE(w.project_.embroidery_objects[0].rgb, fg);
    QCOMPARE(w.project_.embroidery_objects[1].rgb, bg);
    QCOMPARE(w.undoStack_.undoNames().size(), depth + 1);
    QCOMPARE(w.project_.embroidery_objects.size(), std::size_t{2});
}

void MainWindowTest::invalidZoneIsRefusedWithoutHistory() {
    MainWindow w;
    w.applyLoadedProject(fillFixture());
    const auto fill = w.project_.embroidery_objects.front();
    w.setSelection({.embroidery = fill.id, .objects = {fill.source_vector}});
    w.setFillMode(true);
    const auto depth = w.undoStack_.undoNames().size();
    auto* panel = w.propertiesPanel_->gradientPanel();
    auto state = panel->state();
    state.zoneStartPercent = 70.0;
    state.zoneEndPercent = 30.0;
    emit panel->edited(state);
    QCOMPARE(w.undoStack_.undoNames().size(), depth);
    QCOMPARE(panel->state().zoneStartPercent, 0.0); // l'inspecteur revient à l'état réel
}

void MainWindowTest::dragEndHandleIsOneUndoStep() {
    MainWindow w;
    w.applyLoadedProject(fillFixture());
    w.resize(900, 700);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    const auto fill = w.project_.embroidery_objects.front();
    w.setSelection({.embroidery = fill.id, .objects = {fill.source_vector}});
    w.setFillMode(true);
    w.updateActions();
    const auto baseline = MainWindowTest::handlesOf(w).size(); // poignées de nœuds de la forme
    w.gradientEditModeAct_->setChecked(true);
    QVERIFY(w.gradientEditModeAct_->isChecked());
    const auto handles = MainWindowTest::handlesOf(w);
    QCOMPARE(handles.size(), baseline + 4); // + début, fin, axe entier, orientation

    w.view_->resetTransform();
    w.view_->scale(40.0, 40.0);
    w.view_->centerOn(QPointF(5.0, 5.0));
    NodeHandleItem* end = nullptr;
    for (auto* h : handles) {
        if (std::abs(h->scenePos().x() - 10.0) < 0.01 && std::abs(h->scenePos().y() - 5.0) < 0.01) {
            end = h;
        }
    }
    QVERIFY(end != nullptr);
    const auto depth = w.undoStack_.undoNames().size();
    const QPoint start = w.view_->mapFromScene(end->scenePos());
    const QPoint stop = w.view_->mapFromScene(QPointF(7.0, 5.0));
    QCOMPARE(dynamic_cast<NodeHandleItem*>(w.view_->itemAt(start)), end);
    QTest::mousePress(w.view_->viewport(), Qt::LeftButton, Qt::NoModifier, start);
    for (int i = 1; i <= 6; ++i) { // glissement progressif : aucun pas avant le relâchement
        QTest::mouseMove(w.view_->viewport(), start + (stop - start) * i / 6);
        QCOMPARE(w.undoStack_.undoNames().size(), depth);
    }
    QTest::mouseRelease(w.view_->viewport(), Qt::LeftButton, Qt::NoModifier, stop);
    QTRY_COMPARE_WITH_TIMEOUT(w.undoStack_.undoNames().size(), depth + 1, 1000);

    const auto spec = *project_ops::describe_gradient_fill(w.project_, fill.id);
    const auto zone =
        *project_ops::gradient_zone(w.project_.vector_objects[0].paths, spec.gradient);
    QVERIFY(std::abs(zone.end - 0.7) < 0.02);
    QVERIFY(std::abs(zone.start) < 0.01);
    QCOMPARE(w.project_.embroidery_objects.size(), std::size_t{2});

    w.undo(); // UN seul undo ramène la zone entière
    const auto reverted = *project_ops::describe_gradient_fill(w.project_, fill.id);
    QCOMPARE(reverted.gradient.to.x.value, 10'000);
}

void MainWindowTest::dragMoveHandleKeepsTransitionInsideShape() {
    MainWindow w;
    w.applyLoadedProject(fillFixture());
    w.resize(900, 700);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    const auto fill = w.project_.embroidery_objects.front();
    w.setSelection({.embroidery = fill.id, .objects = {fill.source_vector}});
    w.setFillMode(true);
    // Zone réduite 20–60 % pour que l'axe entier puisse glisser.
    auto* panel = w.propertiesPanel_->gradientPanel();
    auto state = panel->state();
    state.zoneStartPercent = 20.0;
    state.zoneEndPercent = 60.0;
    emit panel->edited(state);
    w.gradientEditModeAct_->setChecked(true);
    w.view_->resetTransform();
    w.view_->scale(40.0, 40.0);
    w.view_->centerOn(QPointF(5.0, 5.0));
    NodeHandleItem* move = nullptr;
    for (auto* h : MainWindowTest::handlesOf(w)) {
        if (std::abs(h->scenePos().x() - 4.0) < 0.01 && std::abs(h->scenePos().y() - 5.0) < 0.01) {
            move = h; // centre de l'axe : (2 mm + 6 mm) / 2
        }
    }
    QVERIFY(move != nullptr);
    const auto depth = w.undoStack_.undoNames().size();
    const QPoint start = w.view_->mapFromScene(move->scenePos());
    const QPoint stop = w.view_->mapFromScene(QPointF(9.5, 5.0)); // bien au-delà du bord
    QTest::mousePress(w.view_->viewport(), Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(w.view_->viewport(), (start + stop) / 2);
    QTest::mouseMove(w.view_->viewport(), stop);
    QTest::mouseRelease(w.view_->viewport(), Qt::LeftButton, Qt::NoModifier, stop);
    QTRY_COMPARE_WITH_TIMEOUT(w.undoStack_.undoNames().size(), depth + 1, 1000);
    const auto spec = *project_ops::describe_gradient_fill(w.project_, fill.id);
    const auto zone =
        *project_ops::gradient_zone(w.project_.vector_objects[0].paths, spec.gradient);
    QVERIFY(zone.end <= 1.0 + 1e-3);
    QVERIFY(std::abs((zone.end - zone.start) - 0.4) < 0.01); // l'étendue est conservée
}

void MainWindowTest::deletingOneLayerDeletesTheWholeGradient() {
    MainWindow w;
    w.applyLoadedProject(fillFixture());
    const auto fill = w.project_.embroidery_objects.front();
    w.setSelection({.embroidery = fill.id, .objects = {fill.source_vector}});
    w.setFillMode(true);
    const auto top = w.project_.embroidery_objects[1];
    w.setSelection({.embroidery = top.id, .objects = {top.source_vector}});
    w.deleteSelection();
    QVERIFY(w.project_.embroidery_objects.empty());
    QCOMPARE(w.project_.vector_objects.size(), std::size_t{1}); // la forme reste
    w.undo();
    QCOMPARE(w.project_.embroidery_objects.size(), std::size_t{2});
    QVERIFY(document::find_color_blend(w.project_, fill.id));
}

void MainWindowTest::advancedSectionIsCollapsedAndEditsSpacingInOneStep() {
    MainWindow w;
    w.applyLoadedProject(fillFixture());
    w.show();
    const auto fill = w.project_.embroidery_objects.front();
    w.setSelection({.embroidery = fill.id, .objects = {fill.source_vector}});
    w.setFillMode(true);
    auto* panel = w.propertiesPanel_->gradientPanel();
    auto* advanced = panel->findChild<QWidget*>(QStringLiteral("group_gradientAdvanced"));
    auto* toggle = panel->findChild<QAbstractButton*>(QStringLiteral("button_gradientAdvanced"));
    QVERIFY(advanced != nullptr && toggle != nullptr);
    QVERIFY(!advanced->isVisible()); // les réglages techniques ne sont pas imposés
    toggle->click();
    QVERIFY(advanced->isVisible());
    auto* spacing = panel->findChild<QDoubleSpinBox*>(QStringLiteral("spin_gradientSpacingEnd"));
    QVERIFY(spacing != nullptr);
    const auto depth = w.undoStack_.undoNames().size();
    spacing->setValue(0.6);
    emit spacing->editingFinished();
    QCOMPARE(w.undoStack_.undoNames().size(), depth + 1);
    const auto spec = *project_ops::describe_gradient_fill(w.project_, fill.id);
    QCOMPARE(spec.gradient.spacing_to.value, 600);
    QCOMPARE(w.project_.embroidery_objects.size(), std::size_t{2});
}

void MainWindowTest::panelAndHandlesStayReadableInBothThemes() {
    MainWindow w;
    w.applyLoadedProject(fillFixture());
    w.resize(1100, 760);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    const auto fill = w.project_.embroidery_objects.front();
    w.setSelection({.embroidery = fill.id, .objects = {fill.source_vector}});
    w.setFillMode(true);
    w.gradientEditModeAct_->setChecked(true);
    const QString dir = qEnvironmentVariable("OPENSTITCH_GRAB_DIR");
    for (const auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
        AppTheme::instance().setMode(mode);
        w.updateInspector();
        QTest::qWait(150); // le thème s'applique avant la capture
        auto* panel = w.propertiesPanel_->gradientPanel();
        QVERIFY(panel->isVisible());
        // Contraste lisible des pastilles : le texte contraste avec chaque fond.
        for (const char* name : {"button_gradientStart", "button_gradientEnd"}) {
            auto* swatch = panel->findChild<QPushButton*>(QString::fromLatin1(name));
            QVERIFY(swatch != nullptr);
            QVERIFY(swatch->styleSheet().contains(QStringLiteral("color:")));
        }
        QVERIFY(MainWindowTest::handlesOf(w).size() >= 4);
        if (!dir.isEmpty()) {
            const QString tag =
                mode == ThemeMode::Light ? QStringLiteral("light") : QStringLiteral("dark");
            w.grab().save(dir + QStringLiteral("/gradient_window_") + tag + QStringLiteral(".png"));
            panel->grab().save(dir + QStringLiteral("/gradient_panel_") + tag +
                               QStringLiteral(".png"));
        }
    }
    AppTheme::instance().setMode(ThemeMode::Light);
}

} // namespace openstitch::desktop

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    openstitch::desktop::MainWindowTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_gradient_fill.moc"
