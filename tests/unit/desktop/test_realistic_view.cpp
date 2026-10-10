// SPDX-License-Identifier: Apache-2.0
// Rendu réaliste des points : préférences (QSettings), dialogue de réglages et
// câblage dans MainWindow (bascule, cache du pixmap, niveau de détail). Le
// calcul du rendu lui-même est testé dans tests/unit/stitch_render. Headless,
// aucune comparaison de pixels, aucun sleep.
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QImage>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSlider>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <cmath>
#include <vector>

#include "canvas_view.hpp"
#include "main_window.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/stitch_render/raster.hpp"
#include "realistic_preferences.hpp"
#include "realistic_render_dialog.hpp"

using openstitch::Micrometers;
using openstitch::ObjectId;
using openstitch::Vec2um;
using openstitch::desktop::MainWindow;
using openstitch::desktop::RealisticPreferences;
using openstitch::desktop::RealisticRenderDialog;
namespace sr = openstitch::stitch_render;

namespace {

namespace doc = openstitch::document;
namespace geo = openstitch::geometry;

// Projet minimal avec un contour de 10 mm en point droit (de vrais points).
doc::Project squareProject() {
    doc::Project project;
    project.original.width = 2;
    project.original.height = 2;
    project.original.rgba.assign(2 * 2 * 4, 255);

    doc::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.name = "Square";
    geo::Path square;
    square.closed = true;
    constexpr std::int32_t s = 10'000;
    for (const auto& [x, y] :
         std::vector<std::pair<std::int32_t, std::int32_t>>{{0, 0}, {s, 0}, {s, s}, {0, s}}) {
        square.nodes.push_back(geo::PathNode{Vec2um{Micrometers{x}, Micrometers{y}},
                                             geo::NodeType::Corner, std::nullopt, std::nullopt});
    }
    vec.paths.push_back(geo::PathSet{square, {}});
    const ObjectId vecId = vec.id;
    project.vector_objects.push_back(std::move(vec));

    doc::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.name = "Square - contour";
    emb.source_vector = vecId;
    emb.params = doc::RunningStitchParams{};
    project.embroidery_objects.push_back(std::move(emb));
    return project;
}

int pixmapItemCount(const QGraphicsScene* scene) {
    int n = 0;
    for (const QGraphicsItem* it : scene->items()) {
        if (qgraphicsitem_cast<const QGraphicsPixmapItem*>(it) != nullptr && it->zValue() == 20.0) {
            ++n;
        }
    }
    return n;
}

} // namespace

namespace openstitch::desktop {

// Nom imposé par `friend class MainWindowTest` (main_window.hpp).
class MainWindowTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("RealisticViewTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }

    void init() { QSettings().clear(); }

    // ---- Préférences ---------------------------------------------------------

    void preferencesDefaultToOffAndRoundTrip() {
        const RealisticPreferences defaults = loadRealisticPreferences();
        QVERIFY(!defaults.enabled);
        QCOMPARE(defaults.params, sr::RenderParams{});

        RealisticPreferences p;
        p.enabled = true;
        p.params.thread_width_mm = 0.45;
        p.params.relief = 0.2;
        p.params.sheen = 0.9;
        p.params.twist = 0.1;
        p.params.shadow = 0.8;
        p.params.fabric_rgb = {10, 20, 30};
        p.params.fabric_opaque = false;
        p.params.fabric_texture = sr::FabricTexture::Felt;
        p.params.fabric_relief = 0.3;
        p.params.quality = sr::Quality::Fast;
        saveRealisticPreferences(p);
        QCOMPARE(loadRealisticPreferences(), p);
    }

    void outOfRangeStoredValuesAreClamped() {
        QSettings s;
        s.setValue(QStringLiteral("view/realistic/threadWidthMm"), 50.0);
        s.setValue(QStringLiteral("view/realistic/relief"), -4.0);
        s.setValue(QStringLiteral("view/realistic/fabricTexture"), 99);
        const RealisticPreferences p = loadRealisticPreferences();
        QCOMPARE(p.params.thread_width_mm, sr::kMaxThreadWidthMm);
        QCOMPARE(p.params.relief, 0.0);
        QCOMPARE(p.params.fabric_texture, sr::RenderParams{}.fabric_texture);
    }

    // ---- Dialogue ------------------------------------------------------------

    void dialogShowsInitialValuesAndEmitsEveryEdit() {
        RealisticPreferences initial;
        initial.params.thread_width_mm = 0.4;
        initial.params.shadow = 0.25;
        RealisticRenderDialog dialog(initial);
        QCOMPARE(dialog.preferences(), initial);

        QSignalSpy spy(&dialog, &RealisticRenderDialog::preferencesChanged);
        auto* width = dialog.findChild<QDoubleSpinBox*>("realisticThreadWidth");
        auto* relief = dialog.findChild<QSlider*>("realisticRelief");
        auto* sheen = dialog.findChild<QSlider*>("realisticSheen");
        auto* twist = dialog.findChild<QSlider*>("realisticTwist");
        auto* shadow = dialog.findChild<QSlider*>("realisticShadow");
        auto* quality = dialog.findChild<QComboBox*>("realisticQuality");
        auto* texture = dialog.findChild<QComboBox*>("realisticFabricTexture");
        auto* opaque = dialog.findChild<QCheckBox*>("realisticFabricOpaque");
        auto* enabled = dialog.findChild<QCheckBox*>("realisticEnabled");
        QVERIFY(width && relief && sheen && twist && shadow && quality && texture && opaque &&
                enabled);
        QCOMPARE(width->value(), 0.4);
        QCOMPARE(shadow->value(), 25);

        width->setValue(0.5);
        relief->setValue(10);
        sheen->setValue(20);
        twist->setValue(30);
        shadow->setValue(40);
        quality->setCurrentIndex(quality->findData(static_cast<int>(sr::Quality::Fast)));
        texture->setCurrentIndex(texture->findData(static_cast<int>(sr::FabricTexture::Felt)));
        enabled->setChecked(true);
        dialog.setFabricColor(QColor(1, 2, 3));
        QCOMPARE(spy.count(), 9);

        const RealisticPreferences out = dialog.preferences();
        QVERIFY(out.enabled);
        QCOMPARE(out.params.thread_width_mm, 0.5);
        QCOMPARE(out.params.relief, 0.10);
        QCOMPARE(out.params.sheen, 0.20);
        QCOMPARE(out.params.twist, 0.30);
        QCOMPARE(out.params.shadow, 0.40);
        QVERIFY(out.params.quality == sr::Quality::Fast);
        QVERIFY(out.params.fabric_texture == sr::FabricTexture::Felt);
        QCOMPARE(out.params.fabric_rgb, (std::array<std::uint8_t, 3>{1, 2, 3}));
        const auto last = spy.last().first().value<RealisticPreferences>();
        QCOMPARE(last, out);
    }

    void dialogSetPreferencesIsSilentAndResetRestoresDefaults() {
        RealisticRenderDialog dialog(RealisticPreferences{});
        QSignalSpy spy(&dialog, &RealisticRenderDialog::preferencesChanged);
        RealisticPreferences p;
        p.enabled = true;
        p.params.relief = 0.33;
        p.params.fabric_opaque = false;
        dialog.setPreferences(p);
        QCOMPARE(spy.count(), 0);
        QCOMPARE(dialog.preferences().params.relief, 0.33);
        QVERIFY(!dialog.preferences().params.fabric_opaque);
        // Tissu transparent : couleur et texture grisées.
        QVERIFY(!dialog.findChild<QPushButton*>("realisticFabricColor")->isEnabled());
        QVERIFY(!dialog.findChild<QComboBox*>("realisticFabricTexture")->isEnabled());

        dialog.findChild<QPushButton*>("realisticReset")->click();
        QCOMPARE(spy.count(), 1);
        const RealisticPreferences after = dialog.preferences();
        QCOMPARE(after.params, sr::RenderParams{});
        QVERIFY(after.enabled); // la case d'activation n'est pas réinitialisée
    }

    // ---- Menu et rendu dans la fenêtre ----------------------------------------

    void menuHasActionWithShortcutAndRestoresPersistedState() {
        {
            MainWindow window;
            QAction* act = window.findChild<QAction*>("action_realisticView");
            QVERIFY(act != nullptr);
            QVERIFY(act->isCheckable());
            QVERIFY(!act->isChecked());
            QVERIFY(!act->shortcut().isEmpty());
            QVERIFY(window.findChild<QAction*>("action_realisticSettings") != nullptr);
            act->setChecked(true);
        }
        QVERIFY(loadRealisticPreferences().enabled);
        MainWindow again;
        QVERIFY(again.findChild<QAction*>("action_realisticView")->isChecked());
    }

    void enabledRenderPaintsCachedPixmapAndFallsBackToLinesWhenZoomedOut() {
        MainWindow window;
        window.resize(900, 700);
        window.show();
        window.applyLoadedProject(squareProject());
        QVERIFY(window.sequence_.has_value());
        QVERIFY(window.sequence_->commands.size() > 4);

        // Désactivé : aucune image de rendu, uniquement des lignes.
        QCOMPARE(pixmapItemCount(window.scene_), 0);

        window.view_->resetTransform();
        window.view_->scale(12.0, 12.0); // 12 px/mm : fil de 0,35 mm = ~4 px
        window.realisticAct_->setChecked(true);
        QCOMPARE(pixmapItemCount(window.scene_), 1);
        QCOMPARE(window.realistic_.renderCount, 1);

        // Reconstruction de la couche points sans changement : le cache sert.
        window.renderStitches();
        window.renderStitches();
        QCOMPARE(pixmapItemCount(window.scene_), 1);
        QCOMPARE(window.realistic_.renderCount, 1);

        // Changement de réglage : nouveau rendu (après le rafraîchissement différé).
        RealisticPreferences prefs = window.realistic_.prefs;
        prefs.params.relief = 0.2;
        window.applyRealisticPreferences(prefs);
        QTRY_COMPARE_WITH_TIMEOUT(window.realistic_.renderCount, 2, 2000);
        QCOMPARE(pixmapItemCount(window.scene_), 1);

        // Dézoom fort : le fil devient sous-pixel -> lignes, plus d'image.
        window.view_->resetTransform();
        window.view_->scale(1.0, 1.0);
        window.renderStitches();
        QCOMPARE(pixmapItemCount(window.scene_), 0);

        // Désactivation : retour aux lignes et réglage persisté.
        window.view_->scale(12.0, 12.0);
        window.renderStitches();
        QCOMPARE(pixmapItemCount(window.scene_), 1);
        window.realisticAct_->setChecked(false);
        QCOMPARE(pixmapItemCount(window.scene_), 0);
        QVERIFY(!loadRealisticPreferences().enabled);
    }

    void simulationDrawsLinesInsteadOfRealisticView() {
        MainWindow window;
        window.resize(900, 700);
        window.show();
        window.applyLoadedProject(squareProject());
        window.view_->resetTransform();
        window.view_->scale(12.0, 12.0);
        window.realisticAct_->setChecked(true);
        QCOMPARE(pixmapItemCount(window.scene_), 1);
        window.simStep_ = 2;
        window.renderStitches();
        QCOMPARE(pixmapItemCount(window.scene_), 0);
    }

    void dialogOpensFromMenuIsSingleInstanceAndDrivesTheWindow() {
        MainWindow window;
        window.show();
        window.findChild<QAction*>("action_realisticSettings")->trigger();
        QVERIFY(!window.realistic_.dialog.isNull());
        QPointer<RealisticRenderDialog> first = window.realistic_.dialog;
        window.findChild<QAction*>("action_realisticSettings")->trigger();
        QCOMPARE(window.realistic_.dialog.data(), first.data());

        first->findChild<QCheckBox*>("realisticEnabled")->setChecked(true);
        QVERIFY(window.realistic_.prefs.enabled);
        QVERIFY(window.realisticAct_->isChecked());
        first->findChild<QSlider*>("realisticTwist")->setValue(77);
        QCOMPARE(window.realistic_.prefs.params.twist, 0.77);
        QCOMPARE(loadRealisticPreferences().params.twist, 0.77);
    }

    // Aide à la documentation : si OPENSTITCH_REALISTIC_PNG est défini, écrit un
    // échantillon (satin, tatami, contour) rendu en réaliste dans ce fichier PNG.
    // Sans la variable, le test est sans effet.
    void writeSamplePngWhenRequested() {
        const QString path = qEnvironmentVariable("OPENSTITCH_REALISTIC_PNG");
        if (path.isEmpty()) {
            QSKIP("OPENSTITCH_REALISTIC_PNG non défini");
        }
        std::vector<sr::ThreadSegment> segs;
        // Colonne satin rouge : zigzag serré de 30 x 9 mm.
        for (int i = 0; i < 150; ++i) {
            const float x = 4.0F + static_cast<float>(i) * 0.2F;
            const float y0 = (i % 2 == 0) ? 4.0F : 13.0F;
            const float y1 = (i % 2 == 0) ? 13.0F : 4.0F;
            segs.push_back({x, y0, x + 0.2F, y1, {190, 25, 40}});
        }
        // Remplissage tatami bleu : rangées décalées de 0,4 mm.
        for (int row = 0; row < 24; ++row) {
            const float y = 18.0F + static_cast<float>(row) * 0.4F;
            const float shift = static_cast<float>((row * 7) % 5) * 0.8F;
            for (float x = 4.0F + shift - 4.0F; x < 34.0F; x += 4.0F) {
                const float a = std::max(4.0F, x);
                const float b = std::min(34.0F, x + 4.0F);
                if (b > a) {
                    segs.push_back({a, y, b, y, {30, 80, 190}});
                }
            }
        }
        // Contour vert (point droit) autour de l'ensemble.
        const float pts[5][2] = {{2, 2}, {36, 2}, {36, 29}, {2, 29}, {2, 2}};
        for (int i = 0; i < 4; ++i) {
            segs.push_back({pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1], {20, 140, 70}});
        }
        const sr::RasterView view = sr::plan_view({0, 0, 38, 31}, 24.0, 20'000'000);
        const sr::RasterImage img = sr::render_threads(segs, sr::RenderParams{}, view);
        QImage out(img.rgba.data(), img.width, img.height, img.width * 4,
                   QImage::Format_RGBA8888_Premultiplied);
        QVERIFY(out.save(path));
    }

private:
    QTemporaryDir settingsDir_;
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_realistic_view.moc"
