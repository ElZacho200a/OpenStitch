// SPDX-License-Identifier: Apache-2.0
#include <QDoubleSpinBox>
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
    p.stitch_length = Micrometers{3'000};  // 3 mm
    p.min_length = Micrometers{500};       // 0.5 mm
    p.repeats = 1;
    e.params = p;
    return e;
}

}  // namespace

// PropertiesPanel est l'inspecteur contextuel : il se reconstruit selon la
// sélection (menu/inspecteur qui "se met à jour selon l'état") et signale les
// édits utilisateur (`paramsEdited`) sans jamais détenir la vérité métier.
class PropertiesPanelTest : public QObject {
    Q_OBJECT

private slots:
    void showEmbroideryPopulatesSpinBoxesWithoutEmittingWhileBuilding();
    void editingASpinBoxEmitsParamsEditedWithUpdatedValueAndPreservesOthers();
    void switchingToInfoRemovesThePreviousFormControls();
};

void PropertiesPanelTest::showEmbroideryPopulatesSpinBoxesWithoutEmittingWhileBuilding() {
    PropertiesPanel panel;
    int emitCount = 0;
    QObject::connect(&panel, &PropertiesPanel::paramsEdited, &panel,
                      [&](openstitch::ObjectId, StitchParams) { ++emitCount; });

    panel.showEmbroidery(runningStitchObject(7));

    QCOMPARE(emitCount, 0);  // peuplement initial : pas d'édit émis
    const auto spins = panel.findChildren<QDoubleSpinBox*>();
    QCOMPARE(spins.size(), 2);  // longueur de point, longueur minimale
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
    lengthSpin->setValue(5.0);  // 5 mm au lieu de 3 mm

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

QTEST_MAIN(PropertiesPanelTest)
#include "test_properties_panel.moc"
