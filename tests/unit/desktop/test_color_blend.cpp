// SPDX-License-Identifier: Apache-2.0
#include <QAction>
#include <QApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

#include <set>

#include "color_blend_dialog.hpp"
#include "main_window.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/geometry/primitives.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "properties_panel.hpp"

namespace openstitch::desktop {
namespace {

Vec2um vec(std::int32_t x, std::int32_t y) {
    return {Micrometers{x}, Micrometers{y}};
}

document::Project blendFixture(bool image = false, bool top = false) {
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
    e.params = document::TatamiParams{};
    p.embroidery_objects.push_back(e);
    if (top) {
        e.id = p.object_ids.next();
        e.name = "Dessus";
        e.rgb = {30, 30, 200};
        document::DirectionalFillParams dp;
        dp.density_gradient = document::DensityGradient{vec(0, -5'000), vec(10'000, -5'000),
                                                        Micrometers{3'200}, Micrometers{600}};
        dp.underlay_edge = false;
        dp.underlay_parallel = false;
        e.params = dp;
        p.embroidery_objects.push_back(e);
    }
    if (image) {
        p.mm_per_px = Millimeters{0.125};
        p.original.width = p.original.height = 80;
        p.original.rgba.assign(80 * 80 * 4, 255);
        for (int y = 0; y < 80; ++y) {
            for (int x = 0; x < 80; ++x) {
                auto* px = p.original.rgba.data() + (static_cast<std::size_t>(y) * 80 + x) * 4;
                px[0] = static_cast<std::uint8_t>(220 - x * 2);
                px[1] = 30;
                px[2] = static_cast<std::uint8_t>(30 + x * 2);
            }
        }
    }
    return p;
}

} // namespace

class MainWindowTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchTests"));
        QCoreApplication::setApplicationName(QStringLiteral("ColorBlend"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
    }
    void noRasterBlendActionAvailable();
    void gradientInspectorUsesRealStartSpacing();
    void gradientAngleEditPreservesStartSpacing();
    void singleActionOffersConfirmationBeforeMutation();
    void zonesActionRejectsUnfilledSelection();
    void inspectorOffersExplicitLayerSelection();
    void manualBlendSettingsAreUndoableAndSurviveSaveLoad();
    void imageProposalCreatesTwoDistinctColors();
    void dialogRejectsInvalidColorsAndReversesRamp();
    void topIsInsertedImmediatelyAfterBackground();
    void colorButtonEditsOnlySelectedLayerAndUndoes();
    void inspectorButtonUsesSharedAction();
    void layerTypeConversionKeepsOtherLayer();
    void explicitLayerThreadSelectionDoesNotIncludeBackground();
    void dialogPreviewShowsTheStitchesOfBothLayers();
    void createdLayersFollowTheChosenProfile();
    void addLayerInsertsAfterBaseAndUndoes();
    void addLayerQuickIsOneUndoableStepWithoutDialog();
    void layerNamesStayUniqueAfterADeletionAndDoNotStackSuffixes();
    void duplicateLayerInsertsACopyAboveAndUndoes();
    void duplicatingAShapeCarriesAllItsLayersInOneStep();
    void moveLayerSwapsWithTheNeighbourOfTheSameShapeAndUndoes();
    void aShapeSelectedAtTheCanvasTargetsItsTopLayer();
    void layerToolbarMovesHidesAndFollowsTheChosenLayer();
};

void MainWindowTest::moveLayerSwapsWithTheNeighbourOfTheSameShapeAndUndoes() {
    MainWindow w;
    w.applyLoadedProject(blendFixture(false, true)); // Fond puis Dessus
    const ObjectId base = w.project_.embroidery_objects.front().id;
    const ObjectId top = w.project_.embroidery_objects.back().id;
    QVERIFY(!w.moveLayer(top, +1));  // déjà tout en haut
    QVERIFY(!w.moveLayer(base, -1)); // déjà au fond
    QVERIFY(w.moveLayer(base, +1));
    QCOMPARE(w.project_.embroidery_objects.front().id, top);
    QCOMPARE(w.project_.embroidery_objects.back().id, base);
    w.undoAct_->trigger();
    QCOMPARE(w.project_.embroidery_objects.front().id, base);
    QCOMPARE(w.project_.embroidery_objects.back().id, top);
}

void MainWindowTest::aShapeSelectedAtTheCanvasTargetsItsTopLayer() {
    MainWindow w;
    w.applyLoadedProject(blendFixture(false, true));
    const auto shape = w.project_.embroidery_objects.front().source_vector;
    w.setSelection({.objects = {shape}});
    const auto* target = w.resolveSelectedEmbroidery();
    QVERIFY(target != nullptr);
    QCOMPARE(target->id, w.project_.embroidery_objects.back().id); // « Dessus »
}

void MainWindowTest::layerToolbarMovesHidesAndFollowsTheChosenLayer() {
    MainWindow w;
    w.applyLoadedProject(blendFixture(false, true));
    const ObjectId base = w.project_.embroidery_objects.front().id;
    const ObjectId top = w.project_.embroidery_objects.back().id;
    w.setSelection({.embroidery = base});
    w.updateActions();
    auto* up = w.propertiesPanel_->findChild<QPushButton*>(QStringLiteral("button_layerUp"));
    auto* down = w.propertiesPanel_->findChild<QPushButton*>(QStringLiteral("button_layerDown"));
    auto* vis = w.propertiesPanel_->findChild<QPushButton*>(QStringLiteral("button_layerVisible"));
    QVERIFY(up != nullptr && down != nullptr && vis != nullptr);
    QVERIFY(up->isEnabled());    // la couche du fond peut monter
    QVERIFY(!down->isEnabled()); // mais pas descendre
    QCOMPARE(vis->text(), QStringLiteral("Masquer"));

    vis->click();
    QVERIFY(!w.project_.findEmbroidery(base)->visible);
    w.updateActions();
    QCOMPARE(vis->text(), QStringLiteral("Afficher"));
    w.undoAct_->trigger();
    QVERIFY(w.project_.findEmbroidery(base)->visible);

    up->click();
    QCOMPARE(w.project_.embroidery_objects.front().id, top);
    w.updateActions();
    QVERIFY(!up->isEnabled()); // la couche choisie est maintenant tout en haut
    QVERIFY(down->isEnabled());
}

void MainWindowTest::addLayerQuickIsOneUndoableStepWithoutDialog() {
    MainWindow w;
    w.applyLoadedProject(blendFixture(false, true));
    const auto base = w.project_.embroidery_objects.front();
    const auto before = w.project_.embroidery_objects.size();
    const auto steps = w.undoStack_.undoNames().size();
    QVERIFY(w.addLayerQuick(base.id));
    QCOMPARE(w.project_.embroidery_objects.size(), before + 1);
    QCOMPARE(w.undoStack_.undoNames().size(), steps + 1);
    const auto& layer = w.project_.embroidery_objects[1]; // juste après la base
    QCOMPARE(layer.source_vector, base.source_vector);
    QCOMPARE(MainWindow::stitchTypeIndex(layer), MainWindow::stitchTypeIndex(base));
    QVERIFY(layer.rgb != base.rgb);
    QVERIFY(w.selectedEmbroidery_.has_value());
    QCOMPARE(w.selectedEmbroidery_->value, layer.id.value); // la nouvelle couche est choisie
    w.undoAct_->trigger();
    QCOMPARE(w.project_.embroidery_objects.size(), before);
}

void MainWindowTest::layerNamesStayUniqueAfterADeletionAndDoNotStackSuffixes() {
    MainWindow w;
    w.applyLoadedProject(blendFixture(false, false)); // une seule broderie
    const ObjectId baseId = w.project_.embroidery_objects.front().id;
    QVERIFY(w.addLayerQuick(baseId));
    const ObjectId second = w.project_.embroidery_objects[1].id;
    QVERIFY(w.addLayerQuick(second)); // ajoutée depuis une couche : pas de suffixe empilé
    const auto nameOf = [&w](std::size_t i) {
        return QString::fromStdString(w.project_.embroidery_objects[i].name);
    };
    QCOMPARE(w.project_.embroidery_objects.size(), std::size_t{3});
    QVERIFY(nameOf(1).endsWith(QStringLiteral("couche 3")) ||
            nameOf(1).endsWith(QStringLiteral("couche 2")));
    QCOMPARE(nameOf(1).count(QStringLiteral("couche")), 1);
    QCOMPARE(nameOf(2).count(QStringLiteral("couche")), 1);
    QVERIFY(nameOf(1) != nameOf(2));
    // Suppression de « couche 2 » puis nouvel ajout : le numéro libre est réutilisé, pas de
    // doublon.
    for (std::size_t i = 0; i < w.project_.embroidery_objects.size(); ++i) {
        if (nameOf(i).endsWith(QStringLiteral("couche 2"))) {
            w.undoStack_.execute(std::make_unique<commands::RemoveEmbroideryObjectCommand>(
                                     w.project_.embroidery_objects[i].id),
                                 w.project_);
            break;
        }
    }
    QVERIFY(w.addLayerQuick(baseId));
    std::set<QString> names;
    for (std::size_t i = 0; i < w.project_.embroidery_objects.size(); ++i) {
        names.insert(nameOf(i));
    }
    QCOMPARE(names.size(), w.project_.embroidery_objects.size());
}

void MainWindowTest::duplicateLayerInsertsACopyAboveAndUndoes() {
    MainWindow w;
    w.applyLoadedProject(blendFixture(false, true));
    const auto base = w.project_.embroidery_objects.front();
    const auto top = w.project_.embroidery_objects.back();
    QVERIFY(w.duplicateLayer(base.id));
    QCOMPARE(w.project_.embroidery_objects.size(), std::size_t{3});
    const auto& copy = w.project_.embroidery_objects[1];
    QCOMPARE(w.project_.embroidery_objects[0].id, base.id);
    QCOMPARE(w.project_.embroidery_objects[2].id, top.id);
    QVERIFY(copy.id != base.id);
    QCOMPARE(copy.source_vector, base.source_vector);
    QCOMPARE(copy.rgb, base.rgb);
    QCOMPARE(copy.params.index(), base.params.index());
    QVERIFY(copy.name != base.name);
    w.undoAct_->trigger();
    QCOMPARE(w.project_.embroidery_objects.size(), std::size_t{2});
}

void MainWindowTest::duplicatingAShapeCarriesAllItsLayersInOneStep() {
    MainWindow w;
    w.applyLoadedProject(blendFixture(false, true)); // deux couches sur une forme
    const ObjectId shape = w.project_.embroidery_objects.front().source_vector;
    const auto vectors = w.project_.vector_objects.size();
    const auto embroideries = w.project_.embroidery_objects.size();
    const auto steps = w.undoStack_.undoNames().size();
    w.duplicateVectorObject(shape);
    QCOMPARE(w.project_.vector_objects.size(), vectors + 1);
    QCOMPARE(w.project_.embroidery_objects.size(), embroideries * 2);
    QCOMPARE(w.undoStack_.undoNames().size(), steps + 1);
    const ObjectId copyId = w.project_.vector_objects.back().id;
    std::size_t onCopy = 0;
    for (const auto& e : w.project_.embroidery_objects) {
        onCopy += e.source_vector == copyId ? 1 : 0;
    }
    QCOMPARE(onCopy, embroideries);
    w.undoAct_->trigger();
    QCOMPARE(w.project_.vector_objects.size(), vectors);
    QCOMPARE(w.project_.embroidery_objects.size(), embroideries);
}

void MainWindowTest::addLayerInsertsAfterBaseAndUndoes() {
    MainWindow w;
    w.applyLoadedProject(blendFixture(false, true)); // base puis « Dessus »
    const auto base = w.project_.embroidery_objects.front();
    const auto top = w.project_.embroidery_objects.back();
    w.setSelection({.embroidery = base.id, .objects = {base.source_vector}});
    w.updateActions();
    QVERIFY(w.addLayerAct_ != nullptr);
    QVERIFY(w.addLayerAct_->isEnabled());
    const auto color = MainWindow::defaultLayerColor(base.rgb);
    QVERIFY(color != base.rgb);
    QVERIFY(w.addLayer(base.id, 1, color)); // tatami
    QCOMPARE(w.project_.embroidery_objects.size(), std::size_t{3});
    const auto& layer = w.project_.embroidery_objects[1];
    QCOMPARE(w.project_.embroidery_objects[0].id, base.id);
    QCOMPARE(w.project_.embroidery_objects[2].id, top.id);
    QCOMPARE(layer.source_vector, base.source_vector);
    QCOMPARE(layer.rgb, color);
    QVERIFY(layer.is_tatami());
    QVERIFY(layer.id != base.id && layer.id != top.id);
    QVERIFY(QString::fromStdString(layer.name).contains(QStringLiteral("couche")));
    w.undoAct_->trigger();
    QCOMPARE(w.project_.embroidery_objects.size(), std::size_t{2});
    QCOMPARE(w.project_.embroidery_objects[0].id, base.id);
    QCOMPARE(w.project_.embroidery_objects[1].id, top.id);
}

void MainWindowTest::dialogPreviewShowsTheStitchesOfBothLayers() {
    // Régression : MainWindow ne répondait jamais à previewRequested ; l'aperçu
    // restait sur « Aucun aperçu actuel des points ».
    MainWindow w;
    w.applyLoadedProject(blendFixture());
    w.setSelection({.embroidery = w.project_.embroidery_objects.front().id});
    w.updateActions();
    QString stats;
    QString legend;
    QTimer::singleShot(0, &w, [&] {
        auto* d = qobject_cast<ColorBlendDialog*>(QApplication::activeModalWidget());
        QVERIFY(d != nullptr);
        auto* timer = d->findChild<QTimer*>(QStringLiteral("timer_blendPreview"));
        QVERIFY(timer != nullptr);
        QMetaObject::invokeMethod(timer, "timeout"); // sans attendre les 180 ms
        stats = d->findChild<QLabel*>(QStringLiteral("blend_previewStats"))->text();
        legend = d->findChild<QLabel*>(QStringLiteral("blend_previewLegend"))->text();
        d->reject();
    });
    w.twoColorBlendAct_->trigger();
    QVERIFY2(stats.contains(QStringLiteral("points cousus")), qPrintable(stats));
    QVERIFY(legend.contains(QStringLiteral("Fond")));
    QVERIFY(legend.contains(QStringLiteral("Dessus")));
    QCOMPARE(w.project_.embroidery_objects.size(), std::size_t{1}); // aucune mutation
}

void MainWindowTest::createdLayersFollowTheChosenProfile() {
    // Ce que l'aperçu montre (profil complémentaire par défaut) est ce qui est créé :
    // rampes opposées, couches liées, sous-couches désactivées.
    MainWindow w;
    w.applyLoadedProject(blendFixture());
    const ObjectId baseId = w.project_.embroidery_objects.front().id;
    w.setSelection({.embroidery = baseId});
    w.updateActions();
    QTimer::singleShot(0, &w, [] {
        auto* d = qobject_cast<ColorBlendDialog*>(QApplication::activeModalWidget());
        QVERIFY(d != nullptr);
        QVERIFY(d->complementary());
        d->findChild<QDoubleSpinBox*>(QStringLiteral("spin_blendStart"))->setValue(3.0);
        d->findChild<QDoubleSpinBox*>(QStringLiteral("spin_blendEnd"))->setValue(0.5);
        auto* ok = d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
        QTest::mouseClick(ok, Qt::LeftButton);
    });
    w.twoColorBlendAct_->trigger();
    QCOMPARE(w.project_.embroidery_objects.size(), std::size_t{2});
    const auto* base = w.project_.findEmbroidery(baseId);
    const auto top = w.project_.embroidery_objects.back();
    const auto* baseParams = std::get_if<document::DirectionalFillParams>(&base->params);
    const auto* topParams = std::get_if<document::DirectionalFillParams>(&top.params);
    QVERIFY(baseParams != nullptr && topParams != nullptr);
    QVERIFY(baseParams->density_gradient && topParams->density_gradient);
    QCOMPARE(baseParams->density_gradient->spacing_from, topParams->density_gradient->spacing_to);
    QCOMPARE(baseParams->density_gradient->spacing_to, topParams->density_gradient->spacing_from);
    QVERIFY(base->color_blend && top.color_blend);
    QCOMPARE(base->color_blend->partner, top.id);
    QVERIFY(top.color_blend->foreground && !base->color_blend->foreground);
    QVERIFY(!baseParams->underlay_edge && !topParams->underlay_edge);
    w.undo(); // une seule annulation défait tout
    QCOMPARE(w.project_.embroidery_objects.size(), std::size_t{1});
    QVERIFY(!w.project_.embroidery_objects.front().color_blend);
}

void MainWindowTest::noRasterBlendActionAvailable() {
    MainWindow w;
    w.applyLoadedProject(blendFixture());
    w.setSelection({.embroidery = w.project_.embroidery_objects.front().id});
    w.updateActions();
    auto* act = w.findChild<QAction*>(QStringLiteral("action_twoColorBlend"));
    QVERIFY(act != nullptr);
    QVERIFY2(act->isEnabled(), "Un fondu manuel ne doit pas exiger une image raster.");
}

void MainWindowTest::gradientInspectorUsesRealStartSpacing() {
    MainWindow w;
    w.applyLoadedProject(blendFixture(false, true));
    w.setSelection({.embroidery = w.project_.embroidery_objects.back().id});
    w.updateActions();
    auto* spin = w.propertiesPanel_->findChildren<QDoubleSpinBox*>().value(0);
    QVERIFY(spin != nullptr);
    QCOMPARE(spin->value(), 3.2); // row_spacing=0,4 n'est pas le début réel du dégradé
}

void MainWindowTest::gradientAngleEditPreservesStartSpacing() {
    MainWindow w;
    w.applyLoadedProject(blendFixture(false, true));
    const ObjectId top = w.project_.embroidery_objects.back().id;
    w.setSelection({.embroidery = top});
    w.updateActions();
    auto* angle =
        w.propertiesPanel_->findChild<QDoubleSpinBox*>(QStringLiteral("spin_gradientAngle"));
    QVERIFY(angle != nullptr);
    angle->setValue(45.0);
    const auto& dp =
        std::get<document::DirectionalFillParams>(w.project_.findEmbroidery(top)->params);
    QVERIFY(dp.density_gradient);
    QCOMPARE(dp.density_gradient->spacing_from, Micrometers{3'200});
    w.undo();
    QCOMPARE(std::get<document::DirectionalFillParams>(w.project_.findEmbroidery(top)->params)
                 .density_gradient->spacing_from,
             Micrometers{3'200});
}

void MainWindowTest::singleActionOffersConfirmationBeforeMutation() {
    MainWindow w;
    w.applyLoadedProject(blendFixture(true));
    w.setSelection({.embroidery = w.project_.embroidery_objects.front().id});
    w.updateActions();
    bool shown = false;
    QTimer::singleShot(0, &w, [&] {
        if (auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            shown = d->objectName() == QLatin1String("colorBlendDialog");
            d->reject();
        }
    });
    w.findChild<QAction*>(QStringLiteral("action_twoColorBlend"))->trigger();
    QApplication::processEvents();
    QVERIFY2(shown, "Le fondu doit proposer un apercu et pouvoir etre annule avant mutation.");
    QCOMPARE(w.project_.embroidery_objects.size(), std::size_t{1});
    QVERIFY(!w.undoStack_.canUndo());
}

void MainWindowTest::zonesActionRejectsUnfilledSelection() {
    MainWindow w;
    auto p = blendFixture();
    auto v = p.vector_objects.front();
    v.id = p.object_ids.next();
    p.vector_objects.push_back(v); // seconde forme sans remplissage
    w.applyLoadedProject(p);
    w.setSelection({.objects = {p.vector_objects[0].id, p.vector_objects[1].id}});
    w.updateActions();
    auto* act = w.findChild<QAction*>(QStringLiteral("action_shapeBlendZones"));
    QVERIFY(act != nullptr);
    QVERIFY(!act->isEnabled());
}

void MainWindowTest::inspectorOffersExplicitLayerSelection() {
    MainWindow w;
    w.applyLoadedProject(blendFixture(false, true));
    const auto& objects = w.project_.embroidery_objects;
    const ObjectId base = objects.front().id;
    const ObjectId top = objects.back().id;
    w.setSelection({.objects = {objects.front().source_vector}});
    w.updateActions();
    auto* layers = w.propertiesPanel_->findChild<QComboBox*>(QStringLiteral("combo_fillLayer"));
    QVERIFY2(layers != nullptr, "Les deux couches doivent etre accessibles dans l'inspecteur.");
    QCOMPARE(layers->count(), 2);
    const auto original = w.project_.findEmbroidery(base)->params;
    QCOMPARE(layers->currentIndex(), 1); // la forme choisie au canevas vise la couche du dessus
    layers->setCurrentIndex(0);
    QCOMPARE(w.selectedEmbroidery_.value_or(ObjectId{}), base);
    layers->setCurrentIndex(1);
    QCOMPARE(w.selectedEmbroidery_.value_or(ObjectId{}), top);
    auto* spacing =
        w.propertiesPanel_->findChild<QDoubleSpinBox*>(QStringLiteral("spin_rowSpacing"));
    QVERIFY(spacing != nullptr);
    spacing->setValue(2.5);
    QCOMPARE(w.project_.findEmbroidery(base)->params, original);
    QCOMPARE(std::get<document::DirectionalFillParams>(w.project_.findEmbroidery(top)->params)
                 .density_gradient->spacing_from,
             Micrometers{2'500});
}

void MainWindowTest::manualBlendSettingsAreUndoableAndSurviveSaveLoad() {
    MainWindow w;
    w.applyLoadedProject(blendFixture());
    const auto base = w.project_.embroidery_objects.front();
    w.setSelection({.embroidery = base.id});
    w.updateActions();
    QTimer::singleShot(0, &w, [] {
        auto* d = qobject_cast<ColorBlendDialog*>(QApplication::activeModalWidget());
        QVERIFY(d != nullptr);
        d->setComplementary(false); // profil « fond uniforme » : le fond reste inchangé
        d->findChild<QLineEdit*>(QStringLiteral("edit_blendBackground"))->setText("#cc3322");
        d->findChild<QLineEdit*>(QStringLiteral("edit_blendForeground"))->setText("#2244cc");
        d->findChild<QDoubleSpinBox*>(QStringLiteral("spin_blendStart"))->setValue(2.5);
        d->findChild<QDoubleSpinBox*>(QStringLiteral("spin_blendEnd"))->setValue(0.6);
        d->findChild<QDoubleSpinBox*>(QStringLiteral("spin_blendAngle"))->setValue(90.0);
        auto* ok = d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
        QVERIFY(ok->isEnabled());
        QTest::mouseClick(ok, Qt::LeftButton);
    });
    w.twoColorBlendAct_->trigger();
    QCOMPARE(w.project_.embroidery_objects.size(), std::size_t{2});
    const auto top = w.project_.embroidery_objects.back();
    const auto& dp = std::get<document::DirectionalFillParams>(top.params);
    QVERIFY(dp.density_gradient);
    QCOMPARE(dp.density_gradient->spacing_from, Micrometers{2'500});
    QCOMPARE(dp.density_gradient->spacing_to, Micrometers{600});
    QCOMPARE(dp.row_spacing, Micrometers{2'500});
    QCOMPARE(dp.underlay_mode, document::UnderlayMode::Manual);
    QVERIFY(!dp.underlay_edge && !dp.underlay_parallel);
    QCOMPARE(dp.density_gradient->from.x, dp.density_gradient->to.x);
    QCOMPARE(top.rgb, (std::array<std::uint8_t, 3>{0x22, 0x44, 0xcc}));
    QCOMPARE(w.project_.findEmbroidery(base.id)->params, base.params);
    QCOMPARE(w.selectedEmbroidery_, std::optional<ObjectId>{top.id});
    QVERIFY(!w.twoColorBlendAct_->isEnabled());
    QVERIFY(w.sequence_.has_value());
    QCOMPARE(w.undoStack_.undoNames().size(), std::size_t{1});
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto file = std::filesystem::path(temp.filePath("blend.osp").toStdWString());
    QVERIFY(project_io::save_project(file, w.project_).has_value());
    const auto loaded = project_io::load_project(file);
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->embroidery_objects[1].params, top.params);
    QCOMPARE(loaded->embroidery_objects[1].rgb, top.rgb);
    w.undoAct_->trigger();
    QCOMPARE(w.project_.embroidery_objects.size(), std::size_t{1});
    QCOMPARE(w.project_.findEmbroidery(base.id)->rgb, base.rgb);
    w.redoAct_->trigger();
    QCOMPARE(w.project_.findEmbroidery(top.id)->params, top.params);
}

void MainWindowTest::imageProposalCreatesTwoDistinctColors() {
    MainWindow w;
    w.applyLoadedProject(blendFixture(true));
    w.setSelection({.embroidery = w.project_.embroidery_objects.front().id});
    w.updateActions();
    QTimer::singleShot(0, &w, [] {
        auto* d = qobject_cast<ColorBlendDialog*>(QApplication::activeModalWidget());
        QVERIFY(d != nullptr);
        d->findChild<QPushButton*>(QStringLiteral("button_blendAnalyze"))->click();
        auto* ok = d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
        QVERIFY(ok->isEnabled());
        QTest::mouseClick(ok, Qt::LeftButton);
    });
    w.twoColorBlendAct_->trigger();
    QCOMPARE(w.project_.embroidery_objects.size(), std::size_t{2});
    QVERIFY(w.project_.embroidery_objects[0].rgb != w.project_.embroidery_objects[1].rgb);
    QVERIFY(std::get<document::DirectionalFillParams>(w.project_.embroidery_objects[1].params)
                .density_gradient);
}

void MainWindowTest::dialogRejectsInvalidColorsAndReversesRamp() {
    ColorBlendDialog d({200, 30, 30}, {30, 30, 200}, false, false);
    auto* bg = d.findChild<QLineEdit*>(QStringLiteral("edit_blendBackground"));
    auto* fg = d.findChild<QLineEdit*>(QStringLiteral("edit_blendForeground"));
    auto* ok = d.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
    QVERIFY(ok->isEnabled());
    fg->setText(bg->text());
    QVERIFY(!ok->isEnabled());
    fg->setText("#123");
    QVERIFY(!ok->isEnabled());
    fg->setText("#3355ee");
    QVERIFY(ok->isEnabled());
    QCOMPARE(d.startSpacing(), Micrometers{4'000});
    d.findChild<QPushButton*>(QStringLiteral("button_blendReverse"))->click();
    QCOMPARE(d.startSpacing(), Micrometers{400});
    QCOMPARE(d.endSpacing(), Micrometers{4'000});
    QVERIFY(!d.findChild<QPushButton*>(QStringLiteral("button_blendAnalyze"))->isEnabled());
}

void MainWindowTest::topIsInsertedImmediatelyAfterBackground() {
    MainWindow w;
    auto p = blendFixture();
    auto other = p.embroidery_objects.front();
    other.id = p.object_ids.next();
    auto v = p.vector_objects.front();
    v.id = p.object_ids.next();
    p.vector_objects.push_back(v);
    other.source_vector = v.id;
    p.embroidery_objects.push_back(other);
    w.applyLoadedProject(p);
    const auto base = p.embroidery_objects.front().id;
    w.setSelection({.embroidery = base});
    w.updateActions();
    QTimer::singleShot(0, &w, [] {
        auto* d = qobject_cast<ColorBlendDialog*>(QApplication::activeModalWidget());
        QVERIFY(d != nullptr);
        d->accept();
    });
    w.twoColorBlendAct_->trigger();
    QCOMPARE(w.project_.embroidery_objects.size(), std::size_t{3});
    QCOMPARE(w.project_.embroidery_objects[0].id, base);
    QCOMPARE(w.project_.embroidery_objects[1].source_vector, p.embroidery_objects[0].source_vector);
    QCOMPARE(w.project_.embroidery_objects[2].id, other.id);
    w.undoAct_->trigger();
    QCOMPARE(w.project_.embroidery_objects[1].id, other.id);
    w.redoAct_->trigger();
    QCOMPARE(w.project_.embroidery_objects[2].id, other.id);
}

void MainWindowTest::colorButtonEditsOnlySelectedLayerAndUndoes() {
    MainWindow w;
    w.applyLoadedProject(blendFixture(false, true));
    const auto base = w.project_.embroidery_objects.front();
    const auto top = w.project_.embroidery_objects.back();
    w.setSelection({.embroidery = top.id});
    w.updateActions();
    auto* button = w.propertiesPanel_->findChild<QPushButton*>(QStringLiteral("button_fillColor"));
    QVERIFY(button != nullptr);
    QTimer::singleShot(0, &w, [] {
        auto* d = qobject_cast<QColorDialog*>(QApplication::activeModalWidget());
        QVERIFY(d != nullptr);
        d->setCurrentColor(QColor("#44cc55"));
        d->accept();
    });
    button->click();
    QCOMPARE(w.project_.findEmbroidery(base.id)->rgb, base.rgb);
    QCOMPARE(w.project_.findEmbroidery(top.id)->rgb,
             (std::array<std::uint8_t, 3>{0x44, 0xcc, 0x55}));
    w.undoAct_->trigger();
    QCOMPARE(w.project_.findEmbroidery(top.id)->rgb, top.rgb);
}

void MainWindowTest::inspectorButtonUsesSharedAction() {
    MainWindow w;
    w.applyLoadedProject(blendFixture());
    w.setSelection({.embroidery = w.project_.embroidery_objects.front().id});
    w.updateActions();
    auto* button =
        w.propertiesPanel_->findChild<QToolButton*>(QStringLiteral("button_createColorBlend"));
    QVERIFY(button != nullptr);
    QCOMPARE(button->defaultAction(), w.twoColorBlendAct_);
    bool seen = false;
    QTimer::singleShot(0, &w, [&] {
        auto* d = qobject_cast<ColorBlendDialog*>(QApplication::activeModalWidget());
        seen = d != nullptr;
        if (d) {
            d->reject();
        }
    });
    button->click();
    QVERIFY(seen);
    QVERIFY(!w.undoStack_.canUndo());
}

void MainWindowTest::layerTypeConversionKeepsOtherLayer() {
    MainWindow w;
    w.applyLoadedProject(blendFixture(false, true));
    const auto base = w.project_.embroidery_objects.front();
    const auto top = w.project_.embroidery_objects.back();
    w.setSelection({.embroidery = top.id, .objects = {top.source_vector}});
    w.updateActions();
    auto* button = [&]() -> QToolButton* {
        for (auto* b : w.contextToolbar_->findChildren<QToolButton*>()) {
            if (b->text() == QLatin1String("Tatami")) {
                return b;
            }
        }
        return nullptr;
    }();
    QVERIFY(button != nullptr);
    button->click();
    QCOMPARE(w.project_.embroidery_objects.size(), std::size_t{2});
    QCOMPARE(w.project_.findEmbroidery(base.id)->params, base.params);
    QVERIFY(w.project_.findEmbroidery(top.id)->is_tatami());
    w.undoAct_->trigger();
    QCOMPARE(w.project_.findEmbroidery(top.id)->params, top.params);
    // Le menu contextuel passe par la sélection vectorielle : même cible explicite.
    w.setSelection({.embroidery = top.id, .objects = {top.source_vector}});
    w.setStitchTypeForSelection(0);
    QCOMPARE(w.project_.embroidery_objects.size(), std::size_t{2});
    QCOMPARE(w.project_.findEmbroidery(base.id)->params, base.params);
    QVERIFY(std::holds_alternative<document::RunningStitchParams>(
        w.project_.findEmbroidery(top.id)->params));
    w.updateActions();
    auto* layers = w.propertiesPanel_->findChild<QComboBox*>(QStringLiteral("combo_fillLayer"));
    QVERIFY(layers != nullptr);
    QCOMPARE(layers->count(), 2); // le sélecteur garde aussi une couche convertie en contour
    QCOMPARE(layers->currentData().toULongLong(), top.id.value);
}

void MainWindowTest::explicitLayerThreadSelectionDoesNotIncludeBackground() {
    MainWindow w;
    w.applyLoadedProject(blendFixture(false, true));
    const auto top = w.project_.embroidery_objects.back();
    w.setSelection({.embroidery = top.id, .objects = {top.source_vector}});
    QCOMPARE(w.selectedEmbroideryIds(), (std::vector<ObjectId>{top.id}));
}

} // namespace openstitch::desktop
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    openstitch::desktop::MainWindowTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_color_blend.moc"
