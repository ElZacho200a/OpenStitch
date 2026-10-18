// SPDX-License-Identifier: Apache-2.0
// Lot C (workflows de broderie), première moitié : création directe sans dialogue (HP-UX-002),
// type de point identique pour une ou N formes (HP-UX-028), avertissement avant d'invalider des
// retouches (HP-UX-029), valeurs mixtes en multi-sélection (HP-UX-038) et régénération différée
// hors fil d'interface (HP-PERF-009). Headless (QT_QPA_PLATFORM=offscreen), sans pixel ni sleep.
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

#include <cmath>
#include <future>
#include <memory>
#include <numbers>
#include <vector>

#include "main_window.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "properties_panel.hpp"
#include "ui_memory.hpp"

using openstitch::Micrometers;
using openstitch::ObjectId;
using openstitch::Vec2um;
namespace doc = openstitch::document;
namespace sg = openstitch::stitch_generation;

namespace {

ObjectId addRect(doc::Project& project, const char* name, int x0Mm, int y0Mm, int wMm, int hMm) {
    doc::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.name = name;
    openstitch::geometry::Path path;
    path.closed = true;
    for (const auto& [dx, dy] :
         {std::pair{0, 0}, std::pair{wMm, 0}, std::pair{wMm, hMm}, std::pair{0, hMm}}) {
        path.nodes.push_back(openstitch::geometry::PathNode{
            Vec2um{Micrometers{(x0Mm + dx) * 1000}, Micrometers{(y0Mm + dy) * 1000}},
            openstitch::geometry::NodeType::Corner, std::nullopt, std::nullopt});
    }
    vec.paths.push_back(openstitch::geometry::PathSet{path, {}});
    const ObjectId id = vec.id;
    project.vector_objects.push_back(std::move(vec));
    return id;
}

ObjectId addEmbroidery(doc::Project& project, ObjectId source, const char* name,
                       doc::StitchParams params) {
    doc::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.name = name;
    emb.source_vector = source;
    emb.params = std::move(params);
    const ObjectId id = emb.id;
    project.embroidery_objects.push_back(std::move(emb));
    return id;
}

doc::Project emptyProject() {
    doc::Project project;
    project.original.width = 2;
    project.original.height = 2;
    project.original.rgba.assign(2 * 2 * 4, 255);
    return project;
}

} // namespace

namespace openstitch::desktop {

class CreationFlowTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // HP-UX-002
    void createTatamiActLeavesNoDialogAndPushesOneUndoStep();
    void createContourAndSatinActsLeaveNoDialog();
    void createWithSettingsKeepsTheDialogAndCancelChangesNothing();
    void lastEditedSettingIsReusedByNextCreation();

    // HP-UX-028
    void inspectorOffersFourTypeButtonsForANewShape();
    void contextToolbarOffersFourTypeButtonsForOneOrManyShapes();
    void contextMenuOfAnUnsewnShapeOffersTheFourTypes();

    // HP-UX-029
    void editingARetouchedObjectShowsTheCountAndUndoRestoresIt();
    void editingACleanObjectShowsNoBanner();

    // HP-UX-038
    void mixedSpacingIsIndeterminateAndAngleEditKeepsEachSpacing();
    void mixedCheckboxAndComboAreIndeterminate();
    void mixedTypeGroupHasIndeterminateLiveFields();

    // HP-PERF-009
    void fiveEditsWithinTheDebounceRegenerateOnce();
    void deferredResultIsIdenticalToTheSynchronousOne();
    void uiEventsRunWhileTheWorkerComputes();
    void staleResultIsRejectedAndFinalStateIsExact();
    void undoRestoresParametersAndPreview();

private:
    static ObjectId buildRetouched(MainWindow& window, doc::Project project, ObjectId source,
                                   int count);
    static void emitSpacing(MainWindow& window, ObjectId emb, int um);
    static bool sameAsSynchronous(const MainWindow& window);

    QTemporaryDir settingsDir_;
};

void CreationFlowTest::initTestCase() {
    QVERIFY(settingsDir_.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
    QCoreApplication::setApplicationName(QStringLiteral("CreationFlowTest"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    QStandardPaths::setTestModeEnabled(true);
}

namespace {
// Programme, avant l'appel testé, le rejet de TOUT dialogue modal : si l'appel en ouvre un,
// `modal` passe à vrai (le dialogue est refermé) ; sinon la minuterie ne rencontre aucun dialogue.
void watchModal(QObject* context, bool* modal) {
    QTimer::singleShot(0, context, [modal] {
        if (auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            *modal = true;
            dlg->reject();
        }
    });
}

bool anyVisibleDialog() {
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (qobject_cast<QDialog*>(w) != nullptr && w->isVisible()) {
            return true;
        }
    }
    return false;
}
} // namespace

// --- HP-UX-002 ----------------------------------------------------------------------------

void CreationFlowTest::createTatamiActLeavesNoDialogAndPushesOneUndoStep() {
    MainWindow window;
    doc::Project project = emptyProject();
    const ObjectId shape = addRect(project, "Forme", 0, 0, 20, 10);
    window.applyLoadedProject(project);
    window.selectedObject_ = shape;
    window.updateActions();
    QVERIFY(window.createTatamiAct_->isEnabled());

    const std::size_t before = window.project_.embroidery_objects.size();
    const auto stepsBefore = window.undoStack_.undoNames().size();
    bool modal = false;
    watchModal(&window, &modal);
    window.createTatamiAct_->trigger();
    QCoreApplication::processEvents();

    QVERIFY(!modal);
    QVERIFY(!anyVisibleDialog());
    QCOMPARE(window.project_.embroidery_objects.size(), before + 1);
    QVERIFY(window.project_.embroidery_objects.back().is_tatami());
    QCOMPARE(window.undoStack_.undoNames().size(), stepsBefore + 1);
    window.undo();
    QCOMPARE(window.project_.embroidery_objects.size(), before);
}

void CreationFlowTest::createContourAndSatinActsLeaveNoDialog() {
    MainWindow window;
    doc::Project project = emptyProject();
    const ObjectId shape = addRect(project, "Ruban", 0, 0, 40, 5); // satinable
    window.applyLoadedProject(project);
    window.selectedObject_ = shape;
    window.updateActions();

    bool modal = false;
    watchModal(&window, &modal);
    window.createStitchAct_->trigger();
    QCoreApplication::processEvents();
    QVERIFY(!modal);
    QCOMPARE(window.project_.embroidery_objects.size(), std::size_t(1));
    QVERIFY(std::holds_alternative<doc::RunningStitchParams>(
        window.project_.embroidery_objects.back().params));
    window.undo();
    QCOMPARE(window.project_.embroidery_objects.size(), std::size_t(0));

    watchModal(&window, &modal);
    window.createSatinAct_->trigger();
    QCoreApplication::processEvents();
    QVERIFY(!modal);
    QVERIFY(!anyVisibleDialog());
    QCOMPARE(window.project_.embroidery_objects.size(), std::size_t(1));
    QVERIFY(window.project_.embroidery_objects.back().is_auto_satin());
    QCOMPARE(window.undoStack_.undoNames().size(), std::size_t(1));
}

void CreationFlowTest::createWithSettingsKeepsTheDialogAndCancelChangesNothing() {
    MainWindow window;
    doc::Project project = emptyProject();
    const ObjectId shape = addRect(project, "Forme", 0, 0, 20, 10);
    window.applyLoadedProject(project);
    window.selectedObject_ = shape;
    window.updateActions();
    QVERIFY(window.createTatamiWithSettingsAct_ != nullptr);
    QVERIFY(window.createTatamiWithSettingsAct_->isEnabled());

    // Annulé : rien n'est créé, mais le dialogue a bien été proposé.
    bool modal = false;
    watchModal(&window, &modal);
    window.createTatamiWithSettingsAct_->trigger();
    QVERIFY(modal);
    QCOMPARE(window.project_.embroidery_objects.size(), std::size_t(0));
    QVERIFY(!window.undoStack_.canUndo());

    // Accepté : l'objet est créé avec les valeurs saisies.
    QTimer::singleShot(0, &window, [] {
        if (auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            const auto spins = dlg->findChildren<QDoubleSpinBox*>();
            if (!spins.isEmpty()) {
                spins.first()->setValue(0.7);
            }
            dlg->accept();
        }
    });
    window.createTatamiWithSettingsAct_->trigger();
    QCOMPARE(window.project_.embroidery_objects.size(), std::size_t(1));
    const auto* tatami =
        std::get_if<doc::TatamiParams>(&window.project_.embroidery_objects.back().params);
    QVERIFY(tatami != nullptr);
    QCOMPARE(tatami->row_spacing.value, 700);
}

void CreationFlowTest::lastEditedSettingIsReusedByNextCreation() {
    MainWindow window;
    doc::Project project = emptyProject();
    const ObjectId a = addRect(project, "A", 0, 0, 20, 10);
    const ObjectId b = addRect(project, "B", 30, 0, 20, 10);
    window.applyLoadedProject(project);

    window.selectedObject_ = a;
    window.updateActions();
    window.createTatamiObject();
    QCOMPARE(window.project_.embroidery_objects.size(), std::size_t(1));
    const ObjectId created = window.project_.embroidery_objects.back().id;

    // L'utilisateur règle l'espacement dans l'inspecteur : c'est le « dernier réglage ».
    doc::TatamiParams edited =
        std::get<doc::TatamiParams>(window.project_.embroidery_objects.back().params);
    edited.row_spacing = Micrometers{650};
    window.propertiesPanel_->paramsEdited(created, edited, QStringLiteral("Espacement"));
    window.flushPendingRefresh();

    window.selectedObject_ = b;
    window.selectedEmbroidery_.reset();
    window.updateActions();
    window.createTatamiObject();
    QCOMPARE(window.project_.embroidery_objects.size(), std::size_t(2));
    const auto& second =
        std::get<doc::TatamiParams>(window.project_.embroidery_objects.back().params);
    QCOMPARE(second.row_spacing.value, 650);
}

// --- HP-UX-028 ----------------------------------------------------------------------------

void CreationFlowTest::inspectorOffersFourTypeButtonsForANewShape() {
    for (int type = 0; type < 4; ++type) {
        MainWindow window;
        doc::Project project = emptyProject();
        const ObjectId shape = addRect(project, "Ruban", 0, 0, 40, 5);
        window.applyLoadedProject(project);
        window.selectedObject_ = shape;
        window.updateActions();

        auto* button = window.propertiesPanel_->findChild<QToolButton*>(
            QStringLiteral("button_stitchType%1").arg(type));
        QVERIFY2(button != nullptr, "bouton de type absent pour une forme sans couture");

        bool modal = false;
        watchModal(&window, &modal);
        const auto stepsBefore = window.undoStack_.undoNames().size();
        button->click();
        QCoreApplication::processEvents();
        QVERIFY(!modal);
        QCOMPARE(window.project_.embroidery_objects.size(), std::size_t(1));
        QCOMPARE(window.undoStack_.undoNames().size(), stepsBefore + 1);
        QCOMPARE(MainWindow::stitchTypeIndex(window.project_.embroidery_objects.back()), type);
    }
}

void CreationFlowTest::contextToolbarOffersFourTypeButtonsForOneOrManyShapes() {
    MainWindow window;
    doc::Project project = emptyProject();
    const ObjectId a = addRect(project, "A", 0, 0, 20, 10);
    const ObjectId b = addRect(project, "B", 30, 0, 20, 10);
    window.applyLoadedProject(project);

    const auto countButtons = [&window] {
        int n = 0;
        for (int t = 0; t < 4; ++t) {
            if (window.contextToolbar_->findChild<QToolButton*>(
                    QStringLiteral("button_contextStitchType%1").arg(t)) != nullptr) {
                ++n;
            }
        }
        return n;
    };
    window.selectedObject_ = a;
    window.updateActions();
    QCOMPARE(countButtons(), 4);

    window.setSelection({.region = std::nullopt, .embroidery = std::nullopt, .objects = {a, b}});
    window.updateActions();
    QCOMPARE(countButtons(), 4);
    auto* satinless = window.contextToolbar_->findChild<QToolButton*>(
        QStringLiteral("button_contextStitchType1")); // tatami pour les deux formes
    QVERIFY(satinless != nullptr);
    satinless->click();
    QCOMPARE(window.project_.embroidery_objects.size(), std::size_t(2));
    QCOMPARE(window.undoStack_.undoNames().size(), std::size_t(1)); // UNE commande composite
}

void CreationFlowTest::contextMenuOfAnUnsewnShapeOffersTheFourTypes() {
    MainWindow window;
    doc::Project project = emptyProject();
    addRect(project, "A", -10, -5, 20, 10);
    window.applyLoadedProject(project);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    int found = -1;
    QTimer::singleShot(0, &window, [&found] {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (menu != nullptr) {
            if (auto* sub = menu->findChild<QMenu*>(QStringLiteral("contextStitchTypeMenu"))) {
                found = static_cast<int>(sub->actions().size());
            }
            menu->close();
        }
    });
    window.onCanvasContextMenu(QPointF(0.0, 0.0), QPoint(10, 10));
    QCOMPARE(found, 4);
}

// --- HP-UX-029 ----------------------------------------------------------------------------

// Objet contour sur un rectangle, avec `count` retouches (points déplacés) déjà posées.
ObjectId CreationFlowTest::buildRetouched(MainWindow& window, doc::Project project, ObjectId source,
                                          int count) {
    const ObjectId emb = addEmbroidery(project, source, "Contour", doc::RunningStitchParams{});
    window.applyLoadedProject(project);
    auto view = sg::edit_view(window.project_, emb);
    int done = 0;
    if (view) {
        for (std::size_t i = 0; i < view->raw.size() && done < count; ++i) {
            if (!sg::is_movable_point(view->raw[i])) {
                continue;
            }
            window.undoStack_.execute(std::make_unique<commands::MoveStitchPointCommand>(
                                          emb, i,
                                          Vec2um{Micrometers{view->raw[i].pos.x.value + 100 + done},
                                                 Micrometers{view->raw[i].pos.y.value}},
                                          view->fingerprint, view->point_count),
                                      window.project_);
            ++done;
        }
    }
    window.refreshImage();
    window.selectedEmbroidery_ = emb;
    window.updateActions();
    return emb;
}

void CreationFlowTest::editingARetouchedObjectShowsTheCountAndUndoRestoresIt() {
    MainWindow window;
    doc::Project project = emptyProject();
    const ObjectId source = addRect(project, "R", 0, 0, 60, 30);
    const ObjectId emb = buildRetouched(window, project, source, 14);
    QCOMPARE(window.project_.findEmbroidery(emb)->overrides.size(), std::size_t(14));
    QCOMPARE(window.editStateOf(emb), sg::ObjectEditState::ManuallyEdited);
    QVERIFY(!window.propertiesPanel_->overrideWarningVisible());

    doc::RunningStitchParams edited =
        std::get<doc::RunningStitchParams>(window.project_.findEmbroidery(emb)->params);
    const doc::StitchParams original = window.project_.findEmbroidery(emb)->params;
    edited.stitch_length = Micrometers{2'000};
    window.propertiesPanel_->paramsEdited(emb, edited, QStringLiteral("Longueur de point"));

    auto* label =
        window.propertiesPanel_->findChild<QLabel*>(QStringLiteral("label_overridesWarning"));
    QVERIFY(label != nullptr);
    QVERIFY(window.propertiesPanel_->overrideWarningVisible());
    QVERIFY2(label->text().contains(QStringLiteral("14")), qPrintable(label->text()));
    QVERIFY(window.propertiesPanel_->findChild<QPushButton*>(
                QStringLiteral("button_reapplyOverrides")) != nullptr);

    window.flushPendingRefresh();
    QCOMPARE(window.editStateOf(emb), sg::ObjectEditState::Dirty); // retouches désactivées
    QVERIFY(window.propertiesPanel_->overrideWarningVisible());

    window.undo();
    QVERIFY(window.project_.findEmbroidery(emb)->params == original);
    QCOMPARE(window.editStateOf(emb), sg::ObjectEditState::ManuallyEdited); // « ✎ » restauré
    QVERIFY(!window.propertiesPanel_->overrideWarningVisible());
}

void CreationFlowTest::editingACleanObjectShowsNoBanner() {
    MainWindow window;
    doc::Project project = emptyProject();
    const ObjectId source = addRect(project, "R", 0, 0, 60, 30);
    const ObjectId emb = addEmbroidery(project, source, "Contour", doc::RunningStitchParams{});
    window.applyLoadedProject(project);
    window.selectedEmbroidery_ = emb;
    window.updateActions();

    doc::RunningStitchParams edited =
        std::get<doc::RunningStitchParams>(window.project_.findEmbroidery(emb)->params);
    edited.stitch_length = Micrometers{2'500};
    window.propertiesPanel_->paramsEdited(emb, edited, QStringLiteral("Longueur de point"));
    QVERIFY(!window.propertiesPanel_->overrideWarningVisible());
}

// --- HP-UX-038 ----------------------------------------------------------------------------

namespace {
struct TwoTatamis {
    doc::Project project;
    ObjectId shapeA{};
    ObjectId shapeB{};
    ObjectId embA{};
    ObjectId embB{};
};

TwoTatamis buildTwoTatamis() {
    TwoTatamis fx;
    fx.project = emptyProject();
    fx.shapeA = addRect(fx.project, "A", 0, 0, 20, 10);
    fx.shapeB = addRect(fx.project, "B", 30, 0, 20, 10);
    doc::TatamiParams a;
    a.row_spacing = Micrometers{300};
    doc::TatamiParams b;
    b.row_spacing = Micrometers{500};
    fx.embA = addEmbroidery(fx.project, fx.shapeA, "Tatami A", a);
    fx.embB = addEmbroidery(fx.project, fx.shapeB, "Tatami B", b);
    return fx;
}
} // namespace

void CreationFlowTest::mixedSpacingIsIndeterminateAndAngleEditKeepsEachSpacing() {
    MainWindow window;
    TwoTatamis fx = buildTwoTatamis();
    window.applyLoadedProject(fx.project);
    window.setSelection(
        {.region = std::nullopt, .embroidery = std::nullopt, .objects = {fx.shapeA, fx.shapeB}});
    window.updateActions();

    auto* spacing =
        window.propertiesPanel_->findChild<QDoubleSpinBox*>(QStringLiteral("spin_rowSpacing"));
    auto* angle =
        window.propertiesPanel_->findChild<QDoubleSpinBox*>(QStringLiteral("spin_tatamiAngle"));
    QVERIFY(spacing != nullptr);
    QVERIFY(angle != nullptr);
    QCOMPARE(spacing->text(), QStringLiteral("—")); // 0,3 mm et 0,5 mm : indéterminé
    QVERIFY(angle->text() != QStringLiteral("—"));  // angles identiques : valeur commune

    const auto stepsBefore = window.undoStack_.undoNames().size();
    angle->setValue(45.0);
    window.flushPendingRefresh();
    const auto& ta = std::get<doc::TatamiParams>(window.project_.findEmbroidery(fx.embA)->params);
    const auto& tb = std::get<doc::TatamiParams>(window.project_.findEmbroidery(fx.embB)->params);
    QCOMPARE(ta.row_spacing.value, 300); // la densité n'a pas bougé
    QCOMPARE(tb.row_spacing.value, 500);
    QVERIFY(std::abs(ta.angle.radians - 45.0 * std::numbers::pi / 180.0) < 1e-9);
    QVERIFY(std::abs(tb.angle.radians - 45.0 * std::numbers::pi / 180.0) < 1e-9);
    QCOMPARE(window.undoStack_.undoNames().size(), stepsBefore + 1); // une seule entrée
    QCOMPARE(spacing->text(), QStringLiteral("—"));                  // toujours indéterminé

    // Éditer le champ mixte l'uniformise, lui seul.
    spacing->setValue(0.4);
    window.flushPendingRefresh();
    QCOMPARE(std::get<doc::TatamiParams>(window.project_.findEmbroidery(fx.embA)->params)
                 .row_spacing.value,
             400);
    QCOMPARE(std::get<doc::TatamiParams>(window.project_.findEmbroidery(fx.embB)->params)
                 .row_spacing.value,
             400);
}

void CreationFlowTest::mixedCheckboxAndComboAreIndeterminate() {
    MainWindow window;
    TwoTatamis fx = buildTwoTatamis();
    std::get<doc::TatamiParams>(fx.project.embroidery_objects[0].params).underlay_edge = true;
    std::get<doc::TatamiParams>(fx.project.embroidery_objects[1].params).underlay_edge = false;
    std::get<doc::TatamiParams>(fx.project.embroidery_objects[1].params).underlay_mode =
        doc::UnderlayMode::Auto;
    window.applyLoadedProject(fx.project);
    window.setSelection(
        {.region = std::nullopt, .embroidery = std::nullopt, .objects = {fx.shapeA, fx.shapeB}});
    window.updateActions();

    auto* edge =
        window.propertiesPanel_->findChild<QCheckBox*>(QStringLiteral("check_underlayEdge"));
    auto* mode =
        window.propertiesPanel_->findChild<QComboBox*>(QStringLiteral("combo_underlayMode"));
    QVERIFY(edge != nullptr);
    QVERIFY(mode != nullptr);
    QCOMPARE(edge->checkState(), Qt::PartiallyChecked);
    QCOMPARE(mode->currentIndex(), -1);
    QCOMPARE(mode->placeholderText(), QStringLiteral("—"));

    // Choisir une valeur n'uniformise que ce champ.
    mode->setCurrentIndex(0);
    window.flushPendingRefresh();
    const auto& ta = std::get<doc::TatamiParams>(window.project_.findEmbroidery(fx.embA)->params);
    const auto& tb = std::get<doc::TatamiParams>(window.project_.findEmbroidery(fx.embB)->params);
    QVERIFY(ta.underlay_mode == doc::UnderlayMode::Manual);
    QVERIFY(tb.underlay_mode == doc::UnderlayMode::Manual);
    QVERIFY(ta.underlay_edge);
    QVERIFY(!tb.underlay_edge);

    // Sortir de l'état intermédiaire par un clic : la case redevient normale et tout le groupe
    // reçoit la valeur choisie (ce champ seulement).
    edge->click();
    window.flushPendingRefresh();
    QVERIFY(!edge->isTristate());
    QCOMPARE(edge->checkState(), Qt::Checked);
    QVERIFY(
        std::get<doc::TatamiParams>(window.project_.findEmbroidery(fx.embA)->params).underlay_edge);
    QVERIFY(
        std::get<doc::TatamiParams>(window.project_.findEmbroidery(fx.embB)->params).underlay_edge);
}

void CreationFlowTest::mixedTypeGroupHasIndeterminateLiveFields() {
    MainWindow window;
    doc::Project project = emptyProject();
    const ObjectId a = addRect(project, "A", 0, 0, 20, 10);
    const ObjectId b = addRect(project, "B", 30, 0, 20, 10);
    const ObjectId embA = addEmbroidery(project, a, "Contour A", doc::RunningStitchParams{});
    const ObjectId embB = addEmbroidery(project, b, "Tatami B", doc::TatamiParams{});
    window.applyLoadedProject(project);
    window.setSelection({.region = std::nullopt, .embroidery = std::nullopt, .objects = {a, b}});
    window.updateActions();

    auto* spacing =
        window.propertiesPanel_->findChild<QDoubleSpinBox*>(QStringLiteral("spin_multiSpacing"));
    QVERIFY(spacing != nullptr);
    QCOMPARE(spacing->text(), QStringLiteral("—")); // types différents : indéterminé
    QVERIFY(window.propertiesPanel_->findChild<QPushButton*>(QStringLiteral("button_multiApply")) ==
            nullptr); // un seul modèle : en direct

    const auto stepsBefore = window.undoStack_.undoNames().size();
    spacing->setValue(0.6);
    window.flushPendingRefresh();
    QCOMPARE(window.undoStack_.undoNames().size(), stepsBefore + 1);
    QCOMPARE(
        std::get<doc::TatamiParams>(window.project_.findEmbroidery(embB)->params).row_spacing.value,
        600);
    QVERIFY(std::holds_alternative<doc::RunningStitchParams>(
        window.project_.findEmbroidery(embA)->params)); // le contour n'a pas d'espacement
}

// --- HP-PERF-009 --------------------------------------------------------------------------

namespace {
struct OneTatami {
    doc::Project project;
    ObjectId emb{};
};
OneTatami buildOneTatami() {
    OneTatami fx;
    fx.project = emptyProject();
    const ObjectId shape = addRect(fx.project, "Grand", 0, 0, 60, 40);
    fx.emb = addEmbroidery(fx.project, shape, "Tatami", doc::TatamiParams{});
    return fx;
}

} // namespace

void CreationFlowTest::emitSpacing(MainWindow& window, ObjectId emb, int um) {
    doc::TatamiParams params =
        std::get<doc::TatamiParams>(window.project_.findEmbroidery(emb)->params);
    params.row_spacing = Micrometers{um};
    window.propertiesPanel_->paramsEdited(emb, params, QStringLiteral("Espacement des rangées"));
}

bool CreationFlowTest::sameAsSynchronous(const MainWindow& window) {
    const auto sync = sg::effective_sequence(window.project_);
    return sync && window.sequence_ && sync->commands == window.sequence_->commands;
}

void CreationFlowTest::fiveEditsWithinTheDebounceRegenerateOnce() {
    MainWindow window;
    OneTatami fx = buildOneTatami();
    window.applyLoadedProject(fx.project);
    window.selectedEmbroidery_ = fx.emb;
    window.updateActions();

    const int before = window.refreshCount_;
    const auto stepsBefore = window.undoStack_.undoNames().size();
    for (int um : {420, 440, 460, 480, 500}) {
        emitSpacing(window, fx.emb, um);
    }
    // La commande est appliquée tout de suite, la régénération attend.
    QCOMPARE(std::get<doc::TatamiParams>(window.project_.findEmbroidery(fx.emb)->params)
                 .row_spacing.value,
             500);
    QCOMPARE(window.refreshCount_, before);
    QVERIFY(window.paramsRefreshPending_);
    QCOMPARE(window.undoStack_.undoNames().size(), stepsBefore + 1); // fusion : un pas

    // Sans flush : la minuterie de 150 ms puis le calcul hors fil terminent d'eux-mêmes.
    QTRY_COMPARE_WITH_TIMEOUT(window.refreshCount_, before + 1, 15000);
    QVERIFY(!window.paramsRefreshPending_);
    QCOMPARE(window.refreshCount_, before + 1); // une seule régénération pour cinq éditions
}

void CreationFlowTest::deferredResultIsIdenticalToTheSynchronousOne() {
    MainWindow window;
    OneTatami fx = buildOneTatami();
    window.applyLoadedProject(fx.project);
    window.selectedEmbroidery_ = fx.emb;
    window.updateActions();

    emitSpacing(window, fx.emb, 530);
    const int before = window.refreshCount_;
    QTRY_COMPARE_WITH_TIMEOUT(window.refreshCount_, before + 1, 15000);
    QVERIFY(window.sequence_.has_value());
    QVERIFY(sameAsSynchronous(window)); // octet pour octet identique à la voie synchrone
}

void CreationFlowTest::uiEventsRunWhileTheWorkerComputes() {
    MainWindow window;
    OneTatami fx = buildOneTatami();
    window.applyLoadedProject(fx.project);
    window.selectedEmbroidery_ = fx.emb;
    window.updateActions();

    std::promise<void> release;
    const auto gate = release.get_future().share();
    window.refreshWorkerHook_ = [gate] { gate.wait(); };
    emitSpacing(window, fx.emb, 450);
    window.paramsRefreshTimer_->stop();
    window.startAsyncRefresh(); // le calcul démarre, bloqué dans le fil de travail
    QVERIFY(window.asyncRefresh_ != nullptr);

    bool uiEventRan = false;
    QTimer::singleShot(0, &window, [&uiEventRan] { uiEventRan = true; });
    QTRY_VERIFY(uiEventRan); // le fil d'interface traite des événements pendant le calcul
    QVERIFY(window.asyncRefresh_ != nullptr);

    const int before = window.refreshCount_;
    release.set_value();
    QTRY_VERIFY_WITH_TIMEOUT(window.asyncRefresh_ == nullptr, 15000);
    QCOMPARE(window.refreshCount_, before + 1);
    QVERIFY(sameAsSynchronous(window));
}

void CreationFlowTest::staleResultIsRejectedAndFinalStateIsExact() {
    MainWindow window;
    OneTatami fx = buildOneTatami();
    window.applyLoadedProject(fx.project);
    window.selectedEmbroidery_ = fx.emb;
    window.updateActions();

    std::promise<void> release;
    const auto gate = release.get_future().share();
    window.refreshWorkerHook_ = [gate] { gate.wait(); };
    emitSpacing(window, fx.emb, 450);
    window.paramsRefreshTimer_->stop();
    window.startAsyncRefresh(); // calcul de la valeur 450, bloqué
    QVERIFY(window.asyncRefresh_ != nullptr);

    const int before = window.refreshCount_;
    emitSpacing(window, fx.emb, 600); // nouvelle édition : le calcul en route est périmé
    window.refreshWorkerHook_ = {};
    release.set_value();
    QTRY_VERIFY_WITH_TIMEOUT(!window.paramsRefreshPending_ && window.asyncRefresh_ == nullptr,
                             20000);
    QCOMPARE(window.refreshCount_, before + 1); // seul le résultat à jour a été appliqué
    QCOMPARE(std::get<doc::TatamiParams>(window.project_.findEmbroidery(fx.emb)->params)
                 .row_spacing.value,
             600);
    QVERIFY(sameAsSynchronous(window));
}

void CreationFlowTest::undoRestoresParametersAndPreview() {
    MainWindow window;
    OneTatami fx = buildOneTatami();
    window.applyLoadedProject(fx.project);
    window.selectedEmbroidery_ = fx.emb;
    window.updateActions();
    window.refreshImage();
    const auto original = window.sequence_->commands;
    const doc::StitchParams originalParams = window.project_.findEmbroidery(fx.emb)->params;

    window.undoStack_.breakMergeChain();
    emitSpacing(window, fx.emb, 700);
    window.flushPendingRefresh();
    QVERIFY(window.sequence_->commands != original);

    emitSpacing(window, fx.emb, 710); // édition encore en attente quand on annule
    window.undo();
    QVERIFY(window.project_.findEmbroidery(fx.emb)->params == originalParams);
    QVERIFY(!window.paramsRefreshPending_);
    QCOMPARE(window.sequence_->commands.size(), original.size());
    QVERIFY(window.sequence_->commands == original || sameAsSynchronous(window));
    QVERIFY(sameAsSynchronous(window));
}

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::CreationFlowTest)
#include "test_creation_flow.moc"
