// SPDX-License-Identifier: Apache-2.0
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTest>

#include <optional>
#include <variant>

#include "openstitch/document/embroidery_object.hpp"
#include "properties_panel.hpp"

using openstitch::Micrometers;
using openstitch::Millimeters;
using openstitch::desktop::PropertiesPanel;
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

QTEST_MAIN(PropertiesPanelTest)
#include "test_properties_panel.moc"
