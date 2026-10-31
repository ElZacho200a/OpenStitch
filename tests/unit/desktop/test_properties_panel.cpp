// SPDX-License-Identifier: Apache-2.0
#include <numbers>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QTest>
#include <QToolButton>
#include <QWheelEvent>

#include <cmath>
#include <optional>
#include <variant>

#include "openstitch/document/embroidery_object.hpp"
#include "properties_panel.hpp"

using openstitch::Micrometers;
using openstitch::Millimeters;
using openstitch::desktop::PropertiesPanel;
using openstitch::document::AutoSatinGuide;
using openstitch::document::AutoSatinParams;
using openstitch::document::EmbroideryObject;
using openstitch::document::RunningStitchParams;
using openstitch::document::StitchParams;

namespace {

EmbroideryObject runningStitchObject(std::uint64_t id) {
    EmbroideryObject e;
    e.id = openstitch::ObjectId{id};
    e.name = "contour test";
    RunningStitchParams p;
    p.stitch_length = Micrometers{3'000}; // 3 mm
    p.min_length = Micrometers{500};      // 0.5 mm
    p.repeats = 1;
    e.params = p;
    return e;
}

EmbroideryObject autoSatinObject(std::uint64_t id) {
    EmbroideryObject e;
    e.id = openstitch::ObjectId{id};
    e.name = "satin auto";
    AutoSatinParams p;
    p.guides.push_back({openstitch::Vec2um{Micrometers{10'000}, Micrometers{2'000}},
                        openstitch::Angle{0.5}, true});
    p.guides.push_back({openstitch::Vec2um{Micrometers{20'000}, Micrometers{2'000}},
                        openstitch::Angle{-0.2}, false});
    p.entry_point = openstitch::Vec2um{Micrometers{5}, Micrometers{6}};
    e.params = p;
    return e;
}

} // namespace

// PropertiesPanel est l'inspecteur contextuel : il se reconstruit selon la
// sélection (menu/inspecteur qui "se met à jour selon l'état") et signale les
// édits utilisateur (`paramsEdited`) sans jamais détenir la vérité métier.
class PropertiesPanelTest : public QObject {
    Q_OBJECT

private slots:
    void showEmbroideryPopulatesSpinBoxesWithoutEmittingWhileBuilding();
    void headerShowsColorDotAndTypePillOnlyForAnEmbroideryObject();
    void editingASpinBoxEmitsParamsEditedWithUpdatedValueAndPreservesOthers();
    void switchingToInfoRemovesThePreviousFormControls();
    void tatamiOffersConversionToDirectionalFill();
    void directionalEditKeepsGuidesAndSeed();
    void runningStyleComboEmitsStyleAndStemOffset();
    void contourFillEditsEmitParamsAndConversionIsOffered();
    void motifPanelsEmitEditedParams();
    void stitchTypeMoreMenuOffersEveryOtherType();
    void radialSpiralCrosshatchEditsEmitParamsAndConversionIsOffered();
    void directionalGradientControlsRequestMainWindow();
    void directionalSpacingRegularityIsEditable();
    void autoSatinInspectorListsGuidesAndEditsScalars();
    void autoSatinGuideAngleEditAndRemoveEmitDedicatedSignals();
    void autoSatinStateRefreshUpdatesListWithoutRebuildingTheForm();
    void tatamiEditChangesOnlyTheTouchedFieldWithoutRounding();
    void engineSettingsEmitDedicatedEdits();
    void directionalPullAndZigzagUnderlayAreEditable();
    void borderSatinInspectorEmitsRegenerationRequests();
    void satinSpacingAndCornersAreEditableOnBothSatinKinds();
    void rowSpacingAndLengthsAreBoundedWithRangeTooltips();
    void wheelDoesNotChangeAnUnfocusedField();
    void underlayFieldsAreGreyedWhenTheirBoxIsUnchecked();
    void showsParamsTracksTheDocumentCopy();
    void autoSatinSplitLengthIsBoundedByLmaxAndFollowsIt();
    void selectingAGuideRowAsksForCanvasHighlight();
    void autoSatinMethodChoiceEmitsTheStrategy();
    void autoSatinSweepAngleIsOnlyEditableForTheSweepMethod();
    void autoSatinCompareButtonAsksMainWindowAndIsAbsentForAGroup();
    void blanketInspectorEditsWidthSpacingStyleAndSide();
};

void PropertiesPanelTest::showEmbroideryPopulatesSpinBoxesWithoutEmittingWhileBuilding() {
    PropertiesPanel panel;
    int emitCount = 0;
    QObject::connect(&panel, &PropertiesPanel::paramsEdited, &panel,
                     [&](openstitch::ObjectId, StitchParams) { ++emitCount; });

    panel.showEmbroidery(runningStitchObject(7));

    QCOMPARE(emitCount, 0); // peuplement initial : pas d'édit émis
    const auto spins = panel.findChildren<QDoubleSpinBox*>();
    QCOMPARE(spins.size(), 3); // longueur de point, longueur minimale, decalage du point tige
    QCOMPARE(spins.at(0)->value(), 3.0);
    QCOMPARE(spins.at(1)->value(), 0.5);
}

void PropertiesPanelTest::editingASpinBoxEmitsParamsEditedWithUpdatedValueAndPreservesOthers() {
    PropertiesPanel panel;
    panel.showEmbroidery(runningStitchObject(9));

    int emitCount = 0;
    std::optional<openstitch::ObjectId> emittedId;
    std::optional<StitchParams> emittedParams;
    QObject::connect(&panel, &PropertiesPanel::paramsEdited, &panel,
                     [&](openstitch::ObjectId id, StitchParams params) {
                         ++emitCount;
                         emittedId = id;
                         emittedParams = params;
                     });

    auto* lengthSpin = panel.findChildren<QDoubleSpinBox*>().at(0);
    lengthSpin->setValue(5.0); // 5 mm au lieu de 3 mm

    QCOMPARE(emitCount, 1);
    QVERIFY(emittedId.has_value());
    QCOMPARE(emittedId->value, static_cast<std::uint64_t>(9));
    QVERIFY(emittedParams.has_value());
    QVERIFY(std::holds_alternative<RunningStitchParams>(*emittedParams));
    const auto& r = std::get<RunningStitchParams>(*emittedParams);
    QCOMPARE(r.stitch_length, Micrometers{5'000});
    // Les autres champs, non touchés, restent inchangés.
    QCOMPARE(r.min_length, Micrometers{500});
    QCOMPARE(r.repeats, 1);
}

void PropertiesPanelTest::switchingToInfoRemovesThePreviousFormControls() {
    PropertiesPanel panel;
    panel.showEmbroidery(runningStitchObject(3));
    QCOMPARE(panel.findChildren<QDoubleSpinBox*>().size(), 3);

    panel.showInfo(QStringLiteral("Région"), QStringLiteral("détails"));

    QCOMPARE(panel.findChildren<QDoubleSpinBox*>().size(), 0);
}

void PropertiesPanelTest::tatamiOffersConversionToDirectionalFill() {
    PropertiesPanel panel;
    EmbroideryObject e;
    e.id = openstitch::ObjectId{11};
    e.params = openstitch::document::TatamiParams{};
    panel.showEmbroidery(e);
    auto* button = panel.findChild<QPushButton*>(QStringLiteral("button_convertToDirectional"));
    QVERIFY(button != nullptr);
    std::optional<std::uint64_t> requested;
    int edits = 0;
    QObject::connect(&panel, &PropertiesPanel::convertToDirectionalRequested, &panel,
                     [&](openstitch::ObjectId id) { requested = id.value; });
    QObject::connect(&panel, &PropertiesPanel::paramsEdited, &panel,
                     [&](openstitch::ObjectId, StitchParams) { ++edits; });
    button->click();
    QCOMPARE(requested.value_or(0), static_cast<std::uint64_t>(11));
    QCOMPARE(edits, 0); // la conversion passe par MainWindow, jamais par un edit direct
}

void PropertiesPanelTest::contourFillEditsEmitParamsAndConversionIsOffered() {
    // Le tatami propose la conversion, jamais par un edit direct.
    {
        PropertiesPanel panel;
        EmbroideryObject e;
        e.id = openstitch::ObjectId{21};
        e.params = openstitch::document::TatamiParams{};
        panel.showEmbroidery(e);
        auto* button = panel.findChild<QPushButton*>(QStringLiteral("button_convertToContourFill"));
        QVERIFY(button != nullptr);
        std::optional<std::uint64_t> requested;
        QObject::connect(&panel, &PropertiesPanel::convertToContourFillRequested, &panel,
                         [&](openstitch::ObjectId id) { requested = id.value; });
        button->click();
        QCOMPARE(requested.value_or(0), static_cast<std::uint64_t>(21));
    }
    // Le panneau du remplissage concentrique émet les réglages modifiés (µm entiers).
    PropertiesPanel panel;
    EmbroideryObject e;
    e.id = openstitch::ObjectId{22};
    e.params = openstitch::document::ContourFillParams{};
    panel.showEmbroidery(e);
    auto* spacing = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin_contourSpacing"));
    auto* spiral = panel.findChild<QCheckBox*>(QStringLiteral("check_contourSpiral"));
    QVERIFY(spacing != nullptr);
    QVERIFY(spiral != nullptr);
    QVERIFY(spiral->isChecked());
    std::optional<StitchParams> last;
    QObject::connect(&panel, &PropertiesPanel::paramsEdited, &panel,
                     [&](openstitch::ObjectId, StitchParams params) { last = params; });
    spacing->setValue(0.6);
    QVERIFY(last.has_value());
    QCOMPARE(std::get<openstitch::document::ContourFillParams>(*last).row_spacing.value, 600);
    spiral->setChecked(false);
    QVERIFY(!std::get<openstitch::document::ContourFillParams>(*last).spiral);
}

void PropertiesPanelTest::motifPanelsEmitEditedParams() {
    // HP-STI-002 : motif le long du contour.
    {
        PropertiesPanel panel;
        EmbroideryObject e;
        e.id = openstitch::ObjectId{31};
        e.params = openstitch::document::MotifRunParams{};
        panel.showEmbroidery(e);
        auto* size = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin_motifRunSize"));
        auto* alt = panel.findChild<QCheckBox*>(QStringLiteral("check_motifRunAlternate"));
        auto* motif = panel.findChild<QComboBox*>(QStringLiteral("combo_motifRunMotif"));
        QVERIFY(size != nullptr && alt != nullptr && motif != nullptr);
        std::optional<StitchParams> last;
        QObject::connect(&panel, &PropertiesPanel::paramsEdited, &panel,
                         [&](openstitch::ObjectId, StitchParams params) { last = params; });
        size->setValue(5.5);
        QVERIFY(last.has_value());
        QCOMPARE(std::get<openstitch::document::MotifRunParams>(*last).size.value, 5500);
        alt->setChecked(true);
        QVERIFY(std::get<openstitch::document::MotifRunParams>(*last).alternate);
        motif->setCurrentIndex(2);
        QVERIFY(std::get<openstitch::document::MotifRunParams>(*last).motif ==
                openstitch::document::MotifId::Leaf);
    }
    // HP-STI-006 : remplissage a motif.
    PropertiesPanel panel;
    EmbroideryObject e;
    e.id = openstitch::ObjectId{32};
    e.params = openstitch::document::MotifFillParams{};
    panel.showEmbroidery(e);
    auto* spacing = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin_motifFillSpacing"));
    auto* angle = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin_motifFillAngle"));
    auto* stagger = panel.findChild<QCheckBox*>(QStringLiteral("check_motifFillStagger"));
    QVERIFY(spacing != nullptr && angle != nullptr && stagger != nullptr);
    std::optional<StitchParams> last;
    QObject::connect(&panel, &PropertiesPanel::paramsEdited, &panel,
                     [&](openstitch::ObjectId, StitchParams params) { last = params; });
    spacing->setValue(7.0);
    QVERIFY(last.has_value());
    QCOMPARE(std::get<openstitch::document::MotifFillParams>(*last).spacing.value, 7000);
    angle->setValue(30.0);
    QVERIFY(std::abs(std::get<openstitch::document::MotifFillParams>(*last).angle.radians -
                     30.0 * 3.14159265358979 / 180.0) < 1e-6);
    stagger->setChecked(false);
    QVERIFY(!std::get<openstitch::document::MotifFillParams>(*last).stagger);
}

void PropertiesPanelTest::radialSpiralCrosshatchEditsEmitParamsAndConversionIsOffered() {
    // Le tatami propose les trois conversions (jamais d'edit direct).
    {
        PropertiesPanel panel;
        EmbroideryObject e;
        e.id = openstitch::ObjectId{31};
        e.params = openstitch::document::TatamiParams{};
        panel.showEmbroidery(e);
        std::vector<int> kinds;
        QObject::connect(&panel, &PropertiesPanel::convertToFillKindRequested, &panel,
                         [&](openstitch::ObjectId id, int kind) {
                             QCOMPARE(id.value, static_cast<std::uint64_t>(31));
                             kinds.push_back(kind);
                         });
        const char* names[] = {"button_convertToRadialFill", "button_convertToSpiralFill",
                               "button_convertToCrosshatchFill"};
        for (const char* n : names) {
            auto* button = panel.findChild<QPushButton*>(QString::fromLatin1(n));
            QVERIFY(button != nullptr);
            button->click();
        }
        QCOMPARE(kinds, (std::vector<int>{0, 1, 2}));
    }
    std::optional<StitchParams> last;
    // Radial : espacement et centre fixe (mm -> um entiers).
    {
        PropertiesPanel panel;
        EmbroideryObject e;
        e.id = openstitch::ObjectId{32};
        e.params = openstitch::document::RadialFillParams{};
        panel.showEmbroidery(e);
        QObject::connect(&panel, &PropertiesPanel::paramsEdited, &panel,
                         [&](openstitch::ObjectId, StitchParams params) { last = params; });
        auto* spacing = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin_radialSpacing"));
        auto* autoCenter = panel.findChild<QCheckBox*>(QStringLiteral("check_radialAutoCenter"));
        auto* cx = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin_radialCenterX"));
        QVERIFY(spacing != nullptr && autoCenter != nullptr && cx != nullptr);
        QVERIFY(autoCenter->isChecked());
        QVERIFY(!cx->isEnabled());
        spacing->setValue(0.7);
        QCOMPARE(std::get<openstitch::document::RadialFillParams>(*last).row_spacing.value, 700);
        autoCenter->setChecked(false);
        QVERIFY(cx->isEnabled());
        QVERIFY(std::get<openstitch::document::RadialFillParams>(*last).center.has_value());
        cx->setValue(12.5);
        QCOMPARE(std::get<openstitch::document::RadialFillParams>(*last).center->x.value, 12'500);
        autoCenter->setChecked(true);
        QVERIFY(!std::get<openstitch::document::RadialFillParams>(*last).center.has_value());
    }
    // Spirale : sens horaire.
    {
        PropertiesPanel panel;
        EmbroideryObject e;
        e.id = openstitch::ObjectId{33};
        e.params = openstitch::document::SpiralFillParams{};
        panel.showEmbroidery(e);
        QObject::connect(&panel, &PropertiesPanel::paramsEdited, &panel,
                         [&](openstitch::ObjectId, StitchParams params) { last = params; });
        auto* cw = panel.findChild<QCheckBox*>(QStringLiteral("check_spiralClockwise"));
        QVERIFY(cw != nullptr);
        cw->setChecked(true);
        QVERIFY(std::get<openstitch::document::SpiralFillParams>(*last).clockwise);
    }
    // Croise : couches et angle.
    {
        PropertiesPanel panel;
        EmbroideryObject e;
        e.id = openstitch::ObjectId{34};
        e.params = openstitch::document::CrosshatchFillParams{};
        panel.showEmbroidery(e);
        QObject::connect(&panel, &PropertiesPanel::paramsEdited, &panel,
                         [&](openstitch::ObjectId, StitchParams params) { last = params; });
        auto* layers = panel.findChild<QSpinBox*>(QStringLiteral("spin_crosshatchLayers"));
        auto* angle = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin_crosshatchAngle"));
        QVERIFY(layers != nullptr && angle != nullptr);
        layers->setValue(3);
        QCOMPARE(std::get<openstitch::document::CrosshatchFillParams>(*last).layers, 3);
        angle->setValue(45.0);
        QVERIFY(std::abs(std::get<openstitch::document::CrosshatchFillParams>(*last).angle.radians -
                         std::numbers::pi / 4.0) < 1e-9);
    }
}

void PropertiesPanelTest::directionalEditKeepsGuidesAndSeed() {
    PropertiesPanel panel;
    EmbroideryObject e;
    e.id = openstitch::ObjectId{12};
    openstitch::document::DirectionalFillParams dp;
    openstitch::geometry::Path guide;
    guide.closed = false;
    guide.nodes = {{openstitch::Vec2um{Micrometers{0}, Micrometers{0}},
                    openstitch::geometry::NodeType::Corner,
                    {},
                    {}},
                   {openstitch::Vec2um{Micrometers{5'000}, Micrometers{0}},
                    openstitch::geometry::NodeType::Corner,
                    {},
                    {}}};
    dp.guides.push_back(guide);
    dp.seed = 77;
    e.params = dp;
    panel.showEmbroidery(e);

    std::optional<StitchParams> emitted;
    QObject::connect(&panel, &PropertiesPanel::paramsEdited, &panel,
                     [&](openstitch::ObjectId, StitchParams params) { emitted = params; });
    auto* handmade = panel.findChild<QCheckBox*>(QStringLiteral("check_handmade"));
    auto* intensity = panel.findChild<QSpinBox*>(QStringLiteral("spin_handmadeIntensity"));
    QVERIFY(handmade != nullptr);
    QVERIFY(intensity != nullptr);
    QVERIFY(!intensity->isEnabled());
    handmade->setChecked(true);
    QVERIFY(intensity->isEnabled());
    QVERIFY(emitted.has_value());
    const auto& out = std::get<openstitch::document::DirectionalFillParams>(*emitted);
    QVERIFY(out.handmade);
    QCOMPARE(out.guides.size(), std::size_t{1}); // guides conservés
    QCOMPARE(out.seed, 77U);                     // graine conservée

    auto* reseed = panel.findChild<QPushButton*>(QStringLiteral("button_handmadeReseed"));
    QVERIFY(reseed != nullptr);
    reseed->click();
    const auto& reseeded = std::get<openstitch::document::DirectionalFillParams>(*emitted);
    QVERIFY(reseeded.seed != 77U);
    QVERIFY(reseeded.handmade);

    // Bords effilochés : contour cousu par-dessus, guides et graine conservés.
    auto* outline = panel.findChild<QCheckBox*>(QStringLiteral("check_directionalEdgeOutline"));
    QVERIFY(outline != nullptr);
    QVERIFY(!outline->isChecked());
    outline->setChecked(true);
    const auto& outlined = std::get<openstitch::document::DirectionalFillParams>(*emitted);
    QVERIFY(outlined.edge_outline);
    QCOMPARE(outlined.guides.size(), std::size_t{1});
}

void PropertiesPanelTest::directionalGradientControlsRequestMainWindow() {
    PropertiesPanel panel;
    EmbroideryObject e;
    e.id = openstitch::ObjectId{13};
    e.params = openstitch::document::DirectionalFillParams{}; // row_spacing 0,4 mm
    panel.showEmbroidery(e);

    auto* on = panel.findChild<QCheckBox*>(QStringLiteral("check_densityGradient"));
    auto* end = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin_gradientEndSpacing"));
    auto* angle = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin_gradientAngle"));
    QVERIFY(on != nullptr && end != nullptr && angle != nullptr);
    QVERIFY(!on->isChecked());
    QVERIFY(!end->isEnabled());
    QVERIFY(!angle->isEnabled());

    int requests = 0;
    std::optional<std::tuple<std::uint64_t, bool, double, double>> last;
    int edits = 0;
    QObject::connect(&panel, &PropertiesPanel::densityGradientRequested, &panel,
                     [&](openstitch::ObjectId id, bool enabled, double deg, double mm) {
                         ++requests;
                         last = std::tuple{id.value, enabled, deg, mm};
                     });
    QObject::connect(&panel, &PropertiesPanel::paramsEdited, &panel,
                     [&](openstitch::ObjectId, StitchParams) { ++edits; });

    on->setChecked(true);
    QVERIFY(end->isEnabled());
    QVERIFY(angle->isEnabled());
    QCOMPARE(requests, 1);
    QVERIFY(last.has_value());
    QCOMPARE(std::get<0>(*last), std::uint64_t{13});
    QVERIFY(std::get<1>(*last));
    QCOMPARE(std::get<2>(*last), 90.0); // axe vertical par défaut
    QCOMPARE(std::get<3>(*last), 0.8);  // 2 x l'espacement de référence

    end->setValue(1.5);
    QCOMPARE(requests, 2);
    QCOMPARE(std::get<3>(*last), 1.5);
    angle->setValue(0.0);
    QCOMPARE(requests, 3);
    QCOMPARE(std::get<2>(*last), 0.0);
    QCOMPARE(edits, 0); // le dégradé passe par MainWindow, jamais par un edit direct

    on->setChecked(false);
    QCOMPARE(requests, 4);
    QVERIFY(!std::get<1>(*last));
}

void PropertiesPanelTest::directionalSpacingRegularityIsEditable() {
    PropertiesPanel panel;
    EmbroideryObject e;
    e.id = openstitch::ObjectId{14};
    openstitch::document::DirectionalFillParams dp;
    dp.seed = 5;
    dp.spacing_regularity = 0.25;
    e.params = dp;
    panel.showEmbroidery(e);

    auto* spin = panel.findChild<QSpinBox*>(QStringLiteral("spin_spacingRegularity"));
    QVERIFY(spin != nullptr);
    QCOMPARE(spin->value(), 25); // valeur du modèle reflétée

    std::optional<StitchParams> emitted;
    QObject::connect(&panel, &PropertiesPanel::paramsEdited, &panel,
                     [&](openstitch::ObjectId, StitchParams params) { emitted = params; });
    spin->setValue(60);
    QVERIFY(emitted.has_value());
    const auto& out = std::get<openstitch::document::DirectionalFillParams>(*emitted);
    QCOMPARE(out.spacing_regularity, 0.6);
    QCOMPARE(out.seed, 5U); // les autres champs sont conservés
}

void PropertiesPanelTest::autoSatinInspectorListsGuidesAndEditsScalars() {
    PropertiesPanel panel;
    panel.showEmbroidery(autoSatinObject(21));

    auto* list = panel.findChild<QListWidget*>(QStringLiteral("list_satinGuides"));
    QVERIFY(list != nullptr);
    QCOMPARE(list->count(), 2);

    std::optional<StitchParams> emitted;
    QObject::connect(&panel, &PropertiesPanel::paramsEdited, &panel,
                     [&](openstitch::ObjectId, StitchParams params) { emitted = params; });
    // L'espacement se retrouve par son nom : l'ordre des enfants suit celui du formulaire, qui
    // commence maintenant par la section « Méthode » (angle des fils).
    auto* spacingSpin = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin_rowSpacing"));
    QVERIFY(spacingSpin != nullptr);
    spacingSpin->setValue(0.55);
    QVERIFY(emitted.has_value());
    const auto& out = std::get<AutoSatinParams>(*emitted);
    QCOMPARE(out.spacing.value, 550);
    // Les guides ne sont PAS édités par ce flux (MainWindow reprend ceux du document) :
    // la copie du panneau les contient encore, mais seul le document fait foi.
    QCOMPARE(out.guides.size(), std::size_t{2});
}

void PropertiesPanelTest::autoSatinGuideAngleEditAndRemoveEmitDedicatedSignals() {
    PropertiesPanel panel;
    panel.showEmbroidery(autoSatinObject(22));
    auto* list = panel.findChild<QListWidget*>(QStringLiteral("list_satinGuides"));
    auto* angle = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin_satinGuideAngle"));
    auto* absolute = panel.findChild<QCheckBox*>(QStringLiteral("check_satinGuideAbsolute"));
    auto* remove = panel.findChild<QPushButton*>(QStringLiteral("button_satinGuideRemove"));
    auto* place = panel.findChild<QPushButton*>(QStringLiteral("button_editSatinGuides"));
    QVERIFY(list && angle && absolute && remove && place);
    QVERIFY(!angle->isEnabled()); // aucun guide sélectionné
    QVERIFY(!remove->isEnabled());

    int changeCount = 0;
    int lastIndex = -1;
    double lastAngle = 0.0;
    bool lastAbsolute = false;
    QObject::connect(&panel, &PropertiesPanel::satinGuideChangeRequested, &panel,
                     [&](openstitch::ObjectId, int index, double deg, bool abs) {
                         ++changeCount;
                         lastIndex = index;
                         lastAngle = deg;
                         lastAbsolute = abs;
                     });
    int removedIndex = -1;
    QObject::connect(&panel, &PropertiesPanel::satinGuideRemoveRequested, &panel,
                     [&](openstitch::ObjectId, int index) { removedIndex = index; });
    int placeCount = 0;
    QObject::connect(&panel, &PropertiesPanel::editSatinGuidesRequested, &panel,
                     [&](openstitch::ObjectId) { ++placeCount; });

    list->setCurrentRow(1); // second guide : relatif, -0,2 rad (~ -11°)
    QVERIFY(angle->isEnabled());
    QVERIFY(remove->isEnabled());
    QVERIFY(!absolute->isChecked());
    QCOMPARE(changeCount, 0); // sélectionner ne modifie rien

    angle->setValue(25);
    QCOMPARE(changeCount, 1);
    QCOMPARE(lastIndex, 1);
    QCOMPARE(lastAngle, 25.0);
    QVERIFY(!lastAbsolute);

    absolute->setChecked(true);
    QCOMPARE(changeCount, 2);
    QVERIFY(lastAbsolute);

    remove->click();
    QCOMPARE(removedIndex, 1);
    place->click();
    QCOMPARE(placeCount, 1);
}

void PropertiesPanelTest::autoSatinStateRefreshUpdatesListWithoutRebuildingTheForm() {
    PropertiesPanel panel;
    const EmbroideryObject obj = autoSatinObject(23);
    panel.showEmbroidery(obj);
    auto* list = panel.findChild<QListWidget*>(QStringLiteral("list_satinGuides"));
    auto* summary = panel.findChild<QLabel*>(QStringLiteral("label_autoSatinSummary"));
    QVERIFY(list != nullptr);
    QVERIFY(summary != nullptr);
    QCOMPARE(list->count(), 2);

    AutoSatinParams updated = std::get<AutoSatinParams>(obj.params);
    updated.guides.pop_back();
    panel.setAutoSatinState(obj.id, &updated, QStringLiteral("2 colonne(s) · couverture 99 %"));
    QCOMPARE(panel.findChild<QListWidget*>(QStringLiteral("list_satinGuides")),
             list); // même widget
    QCOMPARE(list->count(), 1);
    QCOMPARE(summary->text(), QStringLiteral("2 colonne(s) · couverture 99 %"));

    // Un identifiant différent est ignoré (retard d'affichage évité).
    AutoSatinParams other = updated;
    other.guides.clear();
    panel.setAutoSatinState(openstitch::ObjectId{999}, &other, QStringLiteral("autre"));
    QCOMPARE(list->count(), 1);
    QCOMPARE(summary->text(), QStringLiteral("2 colonne(s) · couverture 99 %"));
}

void PropertiesPanelTest::tatamiEditChangesOnlyTheTouchedFieldWithoutRounding() {
    PropertiesPanel panel;
    EmbroideryObject e;
    e.id = openstitch::ObjectId{31};
    openstitch::document::TatamiParams tp;
    tp.angle = openstitch::Angle{0.5876}; // 33,67 deg : non entier
    tp.inset = Micrometers{237};          // non multiple de 10 um
    tp.row_spacing = Micrometers{437};
    e.params = tp;
    panel.showEmbroidery(e);

    std::optional<StitchParams> emitted;
    QString field;
    QObject::connect(&panel, &PropertiesPanel::paramsEdited, &panel,
                     [&](openstitch::ObjectId, StitchParams p, QString f) {
                         emitted = p;
                         field = f;
                     });
    panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin_rowSpacing"))->setValue(0.60);
    QVERIFY(emitted.has_value());
    const auto& out = std::get<openstitch::document::TatamiParams>(*emitted);
    QCOMPARE(out.row_spacing.value, 600);
    QCOMPARE(out.angle.radians, 0.5876); // ni arrondi ni relu depuis le widget
    QCOMPARE(out.inset.value, 237);
    QCOMPARE(field, QStringLiteral("Espacement des rangées"));

    // L'angle s'édite à 0,1 degré près (et non plus en entiers).
    auto* angle = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin_tatamiAngle"));
    QVERIFY(angle != nullptr);
    QCOMPARE(angle->decimals(), 1);
    angle->setValue(45.5);
    QCOMPARE(std::get<openstitch::document::TatamiParams>(*emitted).angle.radians,
             45.5 * 3.14159265358979323846 / 180.0);
}

void PropertiesPanelTest::runningStyleComboEmitsStyleAndStemOffset() {
    PropertiesPanel panel;
    EmbroideryObject e;
    e.id = openstitch::ObjectId{41};
    e.params = openstitch::document::RunningStitchParams{};
    panel.showEmbroidery(e);

    std::optional<StitchParams> emitted;
    QObject::connect(&panel, &PropertiesPanel::paramsEdited, &panel,
                     [&](openstitch::ObjectId, StitchParams p, QString) { emitted = p; });
    auto* style = panel.findChild<QComboBox*>(QStringLiteral("combo_runningStyle"));
    QVERIFY(style != nullptr);
    QCOMPARE(style->count(), 4);
    style->setCurrentIndex(3);
    QVERIFY(emitted.has_value());
    QVERIFY(std::get<openstitch::document::RunningStitchParams>(*emitted).style ==
            openstitch::document::RunningStyle::Stem);
    auto* off = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin_stemOffset"));
    QVERIFY(off != nullptr);
    off->setValue(0.5);
    QCOMPARE(std::get<openstitch::document::RunningStitchParams>(*emitted).stem_offset.value, 500);
}

void PropertiesPanelTest::engineSettingsEmitDedicatedEdits() {
    PropertiesPanel panel;
    EmbroideryObject e;
    e.id = openstitch::ObjectId{40};
    e.params = openstitch::document::TatamiParams{};
    panel.showEmbroidery(e);

    std::optional<StitchParams> emitted;
    QObject::connect(&panel, &PropertiesPanel::paramsEdited, &panel,
                     [&](openstitch::ObjectId, StitchParams p, QString) { emitted = p; });
    auto* pull = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin_tatamiPull"));
    QVERIFY(pull != nullptr);
    QCOMPARE(pull->maximum(), 3.0);
    QVERIFY(pull->toolTip().contains(QStringLiteral("Plage")));
    pull->setValue(0.3);
    QVERIFY(emitted.has_value());
    QCOMPARE(std::get<openstitch::document::TatamiParams>(*emitted).pull_compensation.value, 300);

    // HP-ENG-001.b : le mode Automatique grise la compensation manuelle.
    auto* pullMode = panel.findChild<QComboBox*>(QStringLiteral("combo_pullMode"));
    QVERIFY(pullMode != nullptr);
    QCOMPARE(pullMode->currentIndex(), 0);
    QVERIFY(pull->isEnabled());
    pullMode->setCurrentIndex(1);
    QCOMPARE(std::get<openstitch::document::TatamiParams>(*emitted).pull_mode,
             openstitch::document::PullMode::Auto);
    QVERIFY(!pull->isEnabled());
    // Mode Adaptatif : compensation locale, le réglage manuel est aussi grisé.
    pullMode->setCurrentIndex(2);
    QCOMPARE(std::get<openstitch::document::TatamiParams>(*emitted).pull_mode,
             openstitch::document::PullMode::Adaptive);
    QVERIFY(!pull->isEnabled());
    // Mode Prédit (niveau 3) : champ de déplacement, réglage manuel grisé.
    pullMode->setCurrentIndex(3);
    QCOMPARE(std::get<openstitch::document::TatamiParams>(*emitted).pull_mode,
             openstitch::document::PullMode::Predicted);
    QVERIFY(!pull->isEnabled());
    pullMode->setCurrentIndex(0);
    QVERIFY(pull->isEnabled());

    // Bords effilochés : écrêtage des bouts de rangée et contour cousu par-dessus.
    auto* clip = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin_tatamiRowEndClip"));
    QVERIFY(clip != nullptr);
    QCOMPARE(clip->maximum(), 5.0);
    clip->setValue(1.0);
    QCOMPARE(std::get<openstitch::document::TatamiParams>(*emitted).row_end_clip.value, 1'000);
    auto* outline = panel.findChild<QCheckBox*>(QStringLiteral("check_tatamiEdgeOutline"));
    QVERIFY(outline != nullptr);
    QVERIFY(!outline->isChecked());
    outline->setChecked(true);
    QVERIFY(std::get<openstitch::document::TatamiParams>(*emitted).edge_outline);

    auto* mode = panel.findChild<QComboBox*>(QStringLiteral("combo_underlayMode"));
    QVERIFY(mode != nullptr);
    mode->setCurrentIndex(1);
    QCOMPARE(std::get<openstitch::document::TatamiParams>(*emitted).underlay_mode,
             openstitch::document::UnderlayMode::Auto);
    QVERIFY(!panel.findChild<QCheckBox*>(QStringLiteral("check_underlayEdge"))->isEnabled());

    int joinMode = -1;
    QObject::connect(&panel, &PropertiesPanel::joinModeEdited, &panel,
                     [&](openstitch::ObjectId, int m) { joinMode = m; });
    panel.findChild<QComboBox*>(QStringLiteral("combo_joinMode"))->setCurrentIndex(2);
    QCOMPARE(joinMode, 2);
    // Resynchronisation sans émission (annulation).
    joinMode = -1;
    panel.setJoinMode(e.id, openstitch::document::JoinMode::Auto);
    QCOMPARE(joinMode, -1);
    QCOMPARE(panel.findChild<QComboBox*>(QStringLiteral("combo_joinMode"))->currentIndex(), 1);

    // HP-SEQ-005 : connecteur après l'objet -- signal dédié, resynchronisation sans émission.
    int connectorMode = -1;
    QObject::connect(&panel, &PropertiesPanel::connectorModeEdited, &panel,
                     [&](openstitch::ObjectId, int m) { connectorMode = m; });
    auto* connector = panel.findChild<QComboBox*>(QStringLiteral("combo_connectorMode"));
    QVERIFY(connector != nullptr);
    QCOMPARE(connector->currentIndex(), 0);
    connector->setCurrentIndex(2);
    QCOMPARE(connectorMode, 2);
    connectorMode = -1;
    panel.setConnectorMode(e.id, openstitch::document::ConnectorMode::AlwaysTrim);
    QCOMPARE(connectorMode, -1);
    QCOMPARE(connector->currentIndex(), 1);

    // HP-ENG-011 : « Retirer les points cachés » (tatami), marge activée avec la case.
    e.params = openstitch::document::TatamiParams{};
    panel.showEmbroidery(e);
    auto* knockout = panel.findChild<QCheckBox*>(QStringLiteral("check_knockout"));
    auto* margin = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin_knockoutMargin"));
    QVERIFY(knockout != nullptr);
    QVERIFY(margin != nullptr);
    QVERIFY(!knockout->isChecked());
    QVERIFY(!margin->isEnabled());
    QCOMPARE(margin->value(), 0.3);
    bool knockoutOn = false;
    double knockoutMm = -1.0;
    QObject::connect(&panel, &PropertiesPanel::knockoutEdited, &panel,
                     [&](openstitch::ObjectId, bool on, double mm) {
                         knockoutOn = on;
                         knockoutMm = mm;
                     });
    knockout->setChecked(true);
    QVERIFY(knockoutOn);
    QCOMPARE(knockoutMm, 0.3);
    QVERIFY(margin->isEnabled());
    // Resynchronisation sans émission (annulation).
    knockoutOn = true;
    panel.setKnockout(e.id, false, 0.5);
    QVERIFY(knockoutOn);
    QVERIFY(!knockout->isChecked());
    QCOMPARE(margin->value(), 0.5);
    // Un contour n'a rien à retirer : pas de case.
    e.params = openstitch::document::RunningStitchParams{};
    panel.showEmbroidery(e);
    QVERIFY(panel.findChild<QCheckBox*>(QStringLiteral("check_knockout")) == nullptr);
}

// HP-ENG-001 / HP-ENG-002 : tirage du directionnel et sous-couche en zigzag dans l'inspecteur.
void PropertiesPanelTest::directionalPullAndZigzagUnderlayAreEditable() {
    PropertiesPanel panel;
    EmbroideryObject e;
    e.id = openstitch::ObjectId{41};
    e.params = openstitch::document::DirectionalFillParams{};
    panel.showEmbroidery(e);
    std::optional<StitchParams> emitted;
    QObject::connect(&panel, &PropertiesPanel::paramsEdited, &panel,
                     [&](openstitch::ObjectId, StitchParams p, QString) { emitted = p; });

    auto* pull = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin_directionalPull"));
    QVERIFY(pull != nullptr);
    QCOMPARE(pull->maximum(), 3.0);
    pull->setValue(0.3);
    QVERIFY(emitted.has_value());
    QCOMPARE(
        std::get<openstitch::document::DirectionalFillParams>(*emitted).pull_compensation.value,
        300);

    auto* zig = panel.findChild<QCheckBox*>(QStringLiteral("check_directionalUnderlayZigzag"));
    QVERIFY(zig != nullptr);
    QVERIFY(!zig->isChecked());
    zig->setChecked(true);
    QVERIFY(std::get<openstitch::document::DirectionalFillParams>(*emitted).underlay_zigzag);
    // Mode automatique : le zigzag manuel est ignore, donc grise.
    auto* mode = panel.findChild<QComboBox*>(QStringLiteral("combo_underlayMode"));
    QVERIFY(mode != nullptr);
    mode->setCurrentIndex(1);
    QVERIFY(!zig->isEnabled());

    PropertiesPanel tatamiPanel;
    EmbroideryObject t;
    t.id = openstitch::ObjectId{42};
    t.params = openstitch::document::TatamiParams{};
    tatamiPanel.showEmbroidery(t);
    std::optional<StitchParams> tatamiEmitted;
    QObject::connect(&tatamiPanel, &PropertiesPanel::paramsEdited, &tatamiPanel,
                     [&](openstitch::ObjectId, StitchParams p, QString) { tatamiEmitted = p; });
    auto* tzig = tatamiPanel.findChild<QCheckBox*>(QStringLiteral("check_underlayZigzag"));
    QVERIFY(tzig != nullptr);
    tzig->setChecked(true);
    QVERIFY(tatamiEmitted.has_value());
    QVERIFY(std::get<openstitch::document::TatamiParams>(*tatamiEmitted).underlay_zigzag);
}

void PropertiesPanelTest::satinSpacingAndCornersAreEditableOnBothSatinKinds() {
    namespace doc = openstitch::document;
    {
        PropertiesPanel panel;
        EmbroideryObject e;
        e.id = openstitch::ObjectId{52};
        e.params = doc::SatinParams{};
        panel.showEmbroidery(e);
        auto* spacing = panel.findChild<QCheckBox*>(QStringLiteral("chk_satinAutoSpacing"));
        auto* corner = panel.findChild<QComboBox*>(QStringLiteral("combo_satinCorner"));
        QVERIFY(spacing != nullptr && corner != nullptr);
        QVERIFY(!spacing->isChecked());
        QCOMPARE(corner->currentIndex(), 0);
        std::optional<StitchParams> emitted;
        QObject::connect(&panel, &PropertiesPanel::paramsEdited, &panel,
                         [&](openstitch::ObjectId, StitchParams params) { emitted = params; });
        spacing->setChecked(true);
        QVERIFY(emitted.has_value());
        QVERIFY(std::get<doc::SatinParams>(*emitted).auto_spacing);
        emitted.reset();
        corner->setCurrentIndex(3);
        QVERIFY(emitted.has_value());
        QCOMPARE(std::get<doc::SatinParams>(*emitted).corner_mode, doc::SatinCorner::Cut);
        auto* threshold =
            panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin_satinCornerThreshold"));
        QVERIFY(threshold != nullptr);
        QCOMPARE(threshold->value(), 60.0);
        emitted.reset();
        threshold->setValue(100.0);
        QVERIFY(emitted.has_value());
        QVERIFY(std::abs(std::get<doc::SatinParams>(*emitted).corner_threshold.radians -
                         100.0 * std::numbers::pi / 180.0) < 1e-9);
    }
    {
        PropertiesPanel panel;
        panel.showEmbroidery(autoSatinObject(53));
        auto* spacing = panel.findChild<QCheckBox*>(QStringLiteral("chk_autoSatinAutoSpacing"));
        auto* corner = panel.findChild<QComboBox*>(QStringLiteral("combo_autoSatinCorner"));
        QVERIFY(spacing != nullptr && corner != nullptr);
        std::optional<StitchParams> emitted;
        QObject::connect(&panel, &PropertiesPanel::paramsEdited, &panel,
                         [&](openstitch::ObjectId, StitchParams params) { emitted = params; });
        corner->setCurrentIndex(1);
        QVERIFY(emitted.has_value());
        QCOMPARE(std::get<AutoSatinParams>(*emitted).corner_mode, doc::SatinCorner::Miter);
        auto* threshold =
            panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin_autoSatinCornerThreshold"));
        QVERIFY(threshold != nullptr);
        emitted.reset();
        threshold->setValue(80.0);
        QVERIFY(emitted.has_value());
        QVERIFY(std::abs(std::get<AutoSatinParams>(*emitted).corner_threshold.radians -
                         80.0 * std::numbers::pi / 180.0) < 1e-9);
        emitted.reset();
        spacing->setChecked(true);
        QVERIFY(emitted.has_value());
        QVERIFY(std::get<AutoSatinParams>(*emitted).auto_spacing);
    }
    {
        // HP-STI-022 : bord irregulier (satin a rails puis auto-satin).
        PropertiesPanel panel;
        EmbroideryObject e;
        e.id = openstitch::ObjectId{54};
        e.params = doc::SatinParams{};
        panel.showEmbroidery(e);
        auto* jagged = panel.findChild<QComboBox*>(QStringLiteral("combo_satinJagged"));
        auto* amp = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin_satinJaggedAmplitude"));
        auto* seed = panel.findChild<QSpinBox*>(QStringLiteral("spin_satinJaggedSeed"));
        QVERIFY(jagged != nullptr && amp != nullptr && seed != nullptr);
        QCOMPARE(jagged->currentIndex(), 0);
        QCOMPARE(amp->value(), 1.5);
        QCOMPARE(seed->value(), 1);
        std::optional<StitchParams> emitted;
        QObject::connect(&panel, &PropertiesPanel::paramsEdited, &panel,
                         [&](openstitch::ObjectId, StitchParams params) { emitted = params; });
        jagged->setCurrentIndex(2);
        QVERIFY(emitted.has_value());
        QCOMPARE(std::get<doc::SatinParams>(*emitted).jagged, doc::SatinJagged::SideB);
        emitted.reset();
        amp->setValue(3.0);
        QVERIFY(emitted.has_value());
        QCOMPARE(std::get<doc::SatinParams>(*emitted).jagged_amplitude,
                 openstitch::Micrometers{3000});
        emitted.reset();
        seed->setValue(77);
        QVERIFY(emitted.has_value());
        QCOMPARE(std::get<doc::SatinParams>(*emitted).jagged_seed, 77u);
    }
    {
        PropertiesPanel panel;
        panel.showEmbroidery(autoSatinObject(55));
        auto* jagged = panel.findChild<QComboBox*>(QStringLiteral("combo_autoSatinJagged"));
        auto* seed = panel.findChild<QSpinBox*>(QStringLiteral("spin_autoSatinJaggedSeed"));
        QVERIFY(jagged != nullptr && seed != nullptr);
        QVERIFY(panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin_autoSatinJaggedAmplitude")));
        std::optional<StitchParams> emitted;
        QObject::connect(&panel, &PropertiesPanel::paramsEdited, &panel,
                         [&](openstitch::ObjectId, StitchParams params) { emitted = params; });
        jagged->setCurrentIndex(3);
        QVERIFY(emitted.has_value());
        QCOMPARE(std::get<AutoSatinParams>(*emitted).jagged, doc::SatinJagged::Both);
        emitted.reset();
        seed->setValue(5);
        QVERIFY(emitted.has_value());
        QCOMPARE(std::get<AutoSatinParams>(*emitted).jagged_seed, 5u);
    }
}

void PropertiesPanelTest::borderSatinInspectorEmitsRegenerationRequests() {
    PropertiesPanel panel;
    EmbroideryObject e;
    e.id = openstitch::ObjectId{41};
    openstitch::document::SatinParams satin;
    satin.border = openstitch::document::BorderSatinSpec{};
    e.params = satin;
    panel.showEmbroidery(e);
    double widthMm = 0;
    int side = -1;
    QObject::connect(&panel, &PropertiesPanel::borderSatinEdited, &panel,
                     [&](openstitch::ObjectId, double w, int s, int) {
                         widthMm = w;
                         side = s;
                     });
    panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin_borderWidth"))->setValue(4.5);
    QCOMPARE(widthMm, 4.5);
    panel.findChild<QComboBox*>(QStringLiteral("combo_borderSide"))->setCurrentIndex(1);
    QCOMPARE(side, 1);

    // Objet vectoriel : création.
    bool requested = false;
    QObject::connect(&panel, &PropertiesPanel::createBorderSatinRequested, &panel,
                     [&](openstitch::ObjectId, double w, int, int) {
                         requested = true;
                         widthMm = w;
                     });
    panel.showVectorObject(openstitch::ObjectId{7}, QStringLiteral("forme"), QStringLiteral("d"),
                           QRectF(0, 0, 10, 10));
    panel.findChild<QPushButton*>(QStringLiteral("button_createBorderSatin"))->click();
    QVERIFY(requested);
    QCOMPARE(widthMm, 3.0);
}

void PropertiesPanelTest::rowSpacingAndLengthsAreBoundedWithRangeTooltips() {
    PropertiesPanel panel;
    EmbroideryObject e;
    e.id = openstitch::ObjectId{32};
    e.params = openstitch::document::TatamiParams{};
    panel.showEmbroidery(e);
    auto* spacing = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin_rowSpacing"));
    QVERIFY(spacing != nullptr);
    QVERIFY(spacing->minimum() >= 0.1);
    spacing->setValue(0.0);
    QVERIFY(spacing->value() >= 0.1); // 0 mm = amas de fil : refusé
    QVERIFY(spacing->toolTip().contains(QStringLiteral("Plage")));

    PropertiesPanel directional;
    EmbroideryObject d;
    d.id = openstitch::ObjectId{33};
    d.params = openstitch::document::DirectionalFillParams{};
    directional.showEmbroidery(d);
    QVERIFY(directional.findChild<QDoubleSpinBox*>(QStringLiteral("spin_rowSpacing"))->minimum() >=
            0.1);

    PropertiesPanel running;
    running.showEmbroidery(runningStitchObject(34));
    for (auto* spin : running.findChildren<QDoubleSpinBox*>()) {
        QVERIFY(spin->minimum() >= 0.1);
    }
    QVERIFY(running.findChildren<QDoubleSpinBox*>().at(0)->minimum() >= 0.5);
}

void PropertiesPanelTest::wheelDoesNotChangeAnUnfocusedField() {
    PropertiesPanel panel;
    panel.showEmbroidery(runningStitchObject(35));
    panel.show();
    auto* spin = panel.findChildren<QDoubleSpinBox*>().at(0);
    QCOMPARE(spin->focusPolicy(), Qt::StrongFocus);
    const double before = spin->value();
    QVERIFY(!spin->hasFocus());
    QWheelEvent wheel(QPointF(5, 5), spin->mapToGlobal(QPointF(5, 5)), QPoint(), QPoint(0, 120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(spin, &wheel);
    QCOMPARE(spin->value(), before);
}

void PropertiesPanelTest::underlayFieldsAreGreyedWhenTheirBoxIsUnchecked() {
    PropertiesPanel panel;
    EmbroideryObject e;
    e.id = openstitch::ObjectId{36};
    e.params = openstitch::document::TatamiParams{};
    panel.showEmbroidery(e);
    auto* edge = panel.findChild<QCheckBox*>(QStringLiteral("check_underlayEdge"));
    auto* inset = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin_underlayInset"));
    auto* par = panel.findChild<QCheckBox*>(QStringLiteral("check_underlayParallel"));
    auto* spacing = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin_underlaySpacing"));
    QVERIFY(edge && inset && par && spacing);
    QVERIFY(!inset->isEnabled());
    QVERIFY(!spacing->isEnabled());
    edge->setChecked(true);
    QVERIFY(inset->isEnabled());
    par->setChecked(true);
    QVERIFY(spacing->isEnabled());
    edge->setChecked(false);
    QVERIFY(!inset->isEnabled());
}

void PropertiesPanelTest::showsParamsTracksTheDocumentCopy() {
    PropertiesPanel panel;
    EmbroideryObject e;
    e.id = openstitch::ObjectId{37};
    e.params = openstitch::document::TatamiParams{};
    QVERIFY(!panel.showsParams(e.params)); // rien affiché
    panel.showEmbroidery(e);
    QVERIFY(panel.showsParams(e.params));
    openstitch::document::TatamiParams other;
    other.row_spacing = Micrometers{999};
    QVERIFY(!panel.showsParams(other)); // document modifié ailleurs (undo...) : périmé
    QVERIFY(!panel.showsParams(openstitch::document::DirectionalFillParams{})); // autre type
    panel.adoptParams(e.id, other);
    QVERIFY(panel.showsParams(other));
}

void PropertiesPanelTest::autoSatinSplitLengthIsBoundedByLmaxAndFollowsIt() {
    PropertiesPanel panel;
    panel.showEmbroidery(autoSatinObject(38));
    auto* lmax = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin_autoSatinThreshold"));
    auto* seg = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin_autoSatinSplitLength"));
    QVERIFY(lmax && seg);
    QCOMPARE(seg->maximum(), lmax->value());

    std::optional<StitchParams> emitted;
    QObject::connect(&panel, &PropertiesPanel::paramsEdited, &panel,
                     [&](openstitch::ObjectId, StitchParams p, QString) { emitted = p; });
    lmax->setValue(3.0);
    QCOMPARE(seg->maximum(), 3.0);
    QVERIFY(seg->value() <= 3.0);
    QVERIFY(emitted.has_value());
    const auto& out = std::get<AutoSatinParams>(*emitted);
    QVERIFY(out.split_length.value <= out.split_threshold.value);
}

void PropertiesPanelTest::selectingAGuideRowAsksForCanvasHighlight() {
    PropertiesPanel panel;
    panel.showEmbroidery(autoSatinObject(39));
    int selected = -1;
    QObject::connect(&panel, &PropertiesPanel::satinGuideSelected, &panel,
                     [&](openstitch::ObjectId, int index) { selected = index; });
    panel.findChild<QListWidget*>(QStringLiteral("list_satinGuides"))->setCurrentRow(1);
    QCOMPARE(selected, 1);
}

void PropertiesPanelTest::headerShowsColorDotAndTypePillOnlyForAnEmbroideryObject() {
    PropertiesPanel panel;
    panel.show();
    auto* dot = panel.findChild<QLabel*>(QStringLiteral("props_colorDot"));
    auto* pill = panel.findChild<QLabel*>(QStringLiteral("props_typePill"));
    QVERIFY(dot != nullptr && pill != nullptr);
    QVERIFY(!dot->isVisible() && !pill->isVisible());

    panel.showEmbroidery(runningStitchObject(5));
    QVERIFY(dot->isVisible());
    QVERIFY(pill->isVisible());
    QVERIFY(!pill->text().isEmpty());
    QVERIFY(!panel.styleSheet().isEmpty()); // feuille locale construite depuis les jetons

    panel.showInfo(QStringLiteral("Info"), QStringLiteral("details"));
    QVERIFY(!dot->isVisible() && !pill->isVisible());
}

void PropertiesPanelTest::autoSatinMethodChoiceEmitsTheStrategy() {
    PropertiesPanel panel;
    panel.showEmbroidery(autoSatinObject(40));
    auto* combo = panel.findChild<QComboBox*>(QStringLiteral("combo_autoSatinStrategy"));
    QVERIFY(combo != nullptr);
    QCOMPARE(combo->count(), 3);
    QCOMPARE(combo->currentIndex(), 0); // le squelette reste le défaut
    for (int i = 0; i < combo->count(); ++i) {
        QVERIFY(!combo->itemData(i, Qt::ToolTipRole).toString().isEmpty());
    }

    std::optional<StitchParams> emitted;
    QObject::connect(&panel, &PropertiesPanel::paramsEdited, &panel,
                     [&](openstitch::ObjectId, StitchParams params) { emitted = params; });
    combo->setCurrentIndex(1);
    QVERIFY(emitted.has_value());
    QCOMPARE(std::get<AutoSatinParams>(*emitted).strategy,
             openstitch::document::AutoSatinStrategy::ParallelSweep);
    emitted.reset();
    combo->setCurrentIndex(2);
    QVERIFY(emitted.has_value());
    QCOMPARE(std::get<AutoSatinParams>(*emitted).strategy,
             openstitch::document::AutoSatinStrategy::MedialAxis);
}

void PropertiesPanelTest::autoSatinSweepAngleIsOnlyEditableForTheSweepMethod() {
    PropertiesPanel panel;
    panel.showEmbroidery(autoSatinObject(41));
    auto* combo = panel.findChild<QComboBox*>(QStringLiteral("combo_autoSatinStrategy"));
    auto* automatic = panel.findChild<QCheckBox*>(QStringLiteral("check_autoSatinSweepAuto"));
    auto* angle = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin_autoSatinSweepAngle"));
    QVERIFY(combo != nullptr && automatic != nullptr && angle != nullptr);
    // Squelette : l'angle n'a pas de sens.
    QVERIFY(!automatic->isEnabled());
    QVERIFY(!angle->isEnabled());

    combo->setCurrentIndex(1);
    QVERIFY(automatic->isEnabled());
    QVERIFY(automatic->isChecked());
    QVERIFY(!angle->isEnabled()); // automatique : pas d'angle fixe

    std::optional<StitchParams> emitted;
    QObject::connect(&panel, &PropertiesPanel::paramsEdited, &panel,
                     [&](openstitch::ObjectId, StitchParams params) { emitted = params; });
    automatic->setChecked(false);
    QVERIFY(angle->isEnabled());
    QVERIFY(emitted.has_value());
    QVERIFY(std::get<AutoSatinParams>(*emitted).sweep_angle.has_value());
    angle->setValue(30.0);
    QVERIFY(emitted.has_value());
    const auto& out = std::get<AutoSatinParams>(*emitted);
    QVERIFY(out.sweep_angle.has_value());
    QVERIFY(std::abs(out.sweep_angle->radians - 30.0 * 3.14159265358979 / 180.0) < 1e-6);
    // Retour à l'automatique : l'angle est effacé.
    automatic->setChecked(true);
    QVERIFY(!std::get<AutoSatinParams>(*emitted).sweep_angle.has_value());
}

void PropertiesPanelTest::autoSatinCompareButtonAsksMainWindowAndIsAbsentForAGroup() {
    PropertiesPanel panel;
    panel.showEmbroidery(autoSatinObject(42));
    auto* compare = panel.findChild<QPushButton*>(QStringLiteral("button_autoSatinCompare"));
    QVERIFY(compare != nullptr);
    std::optional<openstitch::ObjectId> asked;
    QObject::connect(&panel, &PropertiesPanel::compareSatinMethodsRequested, &panel,
                     [&](openstitch::ObjectId id) { asked = id; });
    compare->click();
    QVERIFY(asked.has_value());
    QCOMPARE(asked->value, std::uint64_t{42});
}

// HP-STI-003 : l'inspecteur du blanket / zigzag émet des StitchParams complets (champ touché
// seulement), sans rien muter lui-même.
void PropertiesPanelTest::blanketInspectorEditsWidthSpacingStyleAndSide() {
    namespace doc = openstitch::document;
    PropertiesPanel panel;
    EmbroideryObject e;
    e.id = openstitch::ObjectId{77};
    e.name = "blanket";
    e.params = doc::BlanketStitchParams{};
    panel.showEmbroidery(e);

    std::optional<doc::BlanketStitchParams> last;
    int count = 0;
    QObject::connect(&panel, &PropertiesPanel::paramsEdited, &panel,
                     [&](openstitch::ObjectId, StitchParams params) {
                         ++count;
                         last = std::get<doc::BlanketStitchParams>(params);
                     });

    auto* width = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin_blanketWidth"));
    auto* spacing = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin_blanketSpacing"));
    auto* style = panel.findChild<QComboBox*>(QStringLiteral("combo_blanketStyle"));
    auto* side = panel.findChild<QComboBox*>(QStringLiteral("combo_blanketSide"));
    QVERIFY(width != nullptr && spacing != nullptr && style != nullptr && side != nullptr);
    QCOMPARE(width->value(), 3.0);
    QCOMPARE(spacing->value(), 2.0);
    QCOMPARE(count, 0);

    width->setValue(4.5);
    QCOMPARE(count, 1);
    QCOMPARE(last->width.value, 4'500);
    QCOMPARE(last->spacing.value, 2'000); // le reste est conservé
    spacing->setValue(1.2);
    QCOMPARE(last->spacing.value, 1'200);
    style->setCurrentIndex(1);
    QVERIFY(last->style == doc::BlanketStyle::Zigzag);
    side->setCurrentIndex(2);
    QVERIFY(last->side == doc::BlanketSide::Center);
    QCOMPARE(last->width.value, 4'500);
}

void PropertiesPanelTest::stitchTypeMoreMenuOffersEveryOtherType() {
    // Les types sans bouton direct (motifs, concentrique, radial, spirale, croise) passent par
    // le menu « Plus » de la rangee de types : chacun emet son index.
    PropertiesPanel panel;
    EmbroideryObject e;
    e.id = openstitch::ObjectId{60};
    e.params = openstitch::document::TatamiParams{};
    panel.showEmbroidery(e);
    auto* more = panel.findChild<QToolButton*>(QStringLiteral("button_stitchTypeMore"));
    QVERIFY(more != nullptr);
    QVERIFY(more->menu() != nullptr);
    QCOMPARE(more->menu()->actions().size(), 6);
    std::vector<int> requested;
    QObject::connect(&panel, &PropertiesPanel::stitchTypeRequested, &panel,
                     [&](int type) { requested.push_back(type); });
    for (QAction* act : more->menu()->actions()) {
        act->trigger();
    }
    QCOMPARE(requested, (std::vector<int>{5, 6, 7, 8, 9, 10}));
    // Un objet concentrique affiche son type dans le bouton « Plus ».
    EmbroideryObject c;
    c.id = openstitch::ObjectId{61};
    c.params = openstitch::document::ContourFillParams{};
    panel.showEmbroidery(c);
    more = panel.findChild<QToolButton*>(QStringLiteral("button_stitchTypeMore"));
    QVERIFY(more != nullptr);
    QVERIFY(more->isChecked());
    QVERIFY(more->text().contains(QStringLiteral("concentrique")));
}

QTEST_MAIN(PropertiesPanelTest)
#include "test_properties_panel.moc"
