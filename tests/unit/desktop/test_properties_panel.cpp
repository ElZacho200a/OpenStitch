// SPDX-License-Identifier: Apache-2.0
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTest>
#include <QWheelEvent>

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
    void editingASpinBoxEmitsParamsEditedWithUpdatedValueAndPreservesOthers();
    void switchingToInfoRemovesThePreviousFormControls();
    void tatamiOffersConversionToDirectionalFill();
    void directionalEditKeepsGuidesAndSeed();
    void directionalGradientControlsRequestMainWindow();
    void directionalSpacingRegularityIsEditable();
    void autoSatinInspectorListsGuidesAndEditsScalars();
    void autoSatinGuideAngleEditAndRemoveEmitDedicatedSignals();
    void autoSatinStateRefreshUpdatesListWithoutRebuildingTheForm();
    void tatamiEditChangesOnlyTheTouchedFieldWithoutRounding();
    void rowSpacingAndLengthsAreBoundedWithRangeTooltips();
    void wheelDoesNotChangeAnUnfocusedField();
    void underlayFieldsAreGreyedWhenTheirBoxIsUnchecked();
    void showsParamsTracksTheDocumentCopy();
    void autoSatinSplitLengthIsBoundedByLmaxAndFollowsIt();
    void selectingAGuideRowAsksForCanvasHighlight();
};

void PropertiesPanelTest::showEmbroideryPopulatesSpinBoxesWithoutEmittingWhileBuilding() {
    PropertiesPanel panel;
    int emitCount = 0;
    QObject::connect(&panel, &PropertiesPanel::paramsEdited, &panel,
                     [&](openstitch::ObjectId, StitchParams) { ++emitCount; });

    panel.showEmbroidery(runningStitchObject(7));

    QCOMPARE(emitCount, 0); // peuplement initial : pas d'édit émis
    const auto spins = panel.findChildren<QDoubleSpinBox*>();
    QCOMPARE(spins.size(), 2); // longueur de point, longueur minimale
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
    QCOMPARE(panel.findChildren<QDoubleSpinBox*>().size(), 2);

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
    // Premier QDoubleSpinBox du formulaire : l'espacement.
    const auto spins = panel.findChildren<QDoubleSpinBox*>();
    QVERIFY(!spins.isEmpty());
    spins.at(0)->setValue(0.55);
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

QTEST_MAIN(PropertiesPanelTest)
#include "test_properties_panel.moc"
