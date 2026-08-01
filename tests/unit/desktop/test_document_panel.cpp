// SPDX-License-Identifier: Apache-2.0
#include <QListWidget>
#include <QTabWidget>
#include <QTest>

#include <optional>

#include "document_panel.hpp"
#include "openstitch/document/project.hpp"

using openstitch::desktop::DocumentPanel;
using openstitch::document::Project;

namespace {

Project projectWithTwoObjectsAndTwoRegions(openstitch::ObjectId& firstObjectId,
                                            openstitch::ObjectId& secondObjectId) {
    Project project;

    openstitch::document::EmbroideryObject a;
    a.id = project.object_ids.next();
    a.name = "Feuille";
    a.params = openstitch::document::RunningStitchParams{};
    firstObjectId = a.id;
    project.embroidery_objects.push_back(a);

    openstitch::document::EmbroideryObject b;
    b.id = project.object_ids.next();
    b.name = "Tige";
    b.params = openstitch::document::TatamiParams{};
    secondObjectId = b.id;
    project.embroidery_objects.push_back(b);

    openstitch::segmentation::Segmentation seg;
    seg.width = 2;
    seg.height = 1;
    seg.labels = {1, 2};
    seg.region_slots.push_back(
        openstitch::segmentation::Region{openstitch::RegionId{1}, {10, 20, 30}, 1});
    seg.region_slots.push_back(
        openstitch::segmentation::Region{openstitch::RegionId{2}, {40, 50, 60}, 1});
    project.segmentation = std::move(seg);

    return project;
}

// tabs_/objectsList_/regionsList_ sont privés (pas de membres publics juste
// pour les tests) : on retrouve les listes par leur ordre d'ajout, seul
// contrat stable observé depuis l'extérieur.
QListWidget* objectsList(DocumentPanel& panel) {
    auto* tabs = panel.findChild<QTabWidget*>();
    return qobject_cast<QListWidget*>(tabs->widget(0));
}
QListWidget* regionsList(DocumentPanel& panel) {
    auto* tabs = panel.findChild<QTabWidget*>();
    return qobject_cast<QListWidget*>(tabs->widget(1));
}

}  // namespace

class DocumentPanelTest : public QObject {
    Q_OBJECT

private slots:
    void refreshPopulatesObjectsAndRegionsLists();
    void selectingAnObjectRowEmitsEmbroiderySelectedWithMatchingId();
    void syncSelectionReflectsSelectionWithoutReemittingSignal();
};

void DocumentPanelTest::refreshPopulatesObjectsAndRegionsLists() {
    DocumentPanel panel;
    openstitch::ObjectId first{};
    openstitch::ObjectId second{};
    const Project project = projectWithTwoObjectsAndTwoRegions(first, second);

    panel.refresh(project);

    auto* objects = objectsList(panel);
    auto* regions = regionsList(panel);
    QVERIFY(objects != nullptr);
    QVERIFY(regions != nullptr);
    QCOMPARE(objects->count(), 2);
    QCOMPARE(regions->count(), 2);
    QCOMPARE(objects->item(0)->data(Qt::UserRole).toULongLong(),
              static_cast<qulonglong>(first.value));
    QCOMPARE(objects->item(1)->data(Qt::UserRole).toULongLong(),
              static_cast<qulonglong>(second.value));
    QVERIFY(objects->item(0)->text().contains(QStringLiteral("Feuille")));
    QVERIFY(objects->item(1)->text().contains(QStringLiteral("Tige")));
}

void DocumentPanelTest::selectingAnObjectRowEmitsEmbroiderySelectedWithMatchingId() {
    DocumentPanel panel;
    openstitch::ObjectId first{};
    openstitch::ObjectId second{};
    const Project project = projectWithTwoObjectsAndTwoRegions(first, second);
    panel.refresh(project);

    // ObjectId n'est pas un type Qt enregistré (le coeur ne dépend jamais de
    // Qt) : on capture via une connexion directe plutôt que QSignalSpy, qui
    // exige un QMetaType pour chaque argument de signal.
    int emitCount = 0;
    std::optional<openstitch::ObjectId> emittedId;
    QObject::connect(&panel, &DocumentPanel::embroiderySelected, &panel,
                      [&](openstitch::ObjectId id) {
                          ++emitCount;
                          emittedId = id;
                      });

    objectsList(panel)->setCurrentRow(1);  // second objet ("Tige")

    QCOMPARE(emitCount, 1);
    QVERIFY(emittedId.has_value());
    QCOMPARE(emittedId->value, second.value);
}

void DocumentPanelTest::syncSelectionReflectsSelectionWithoutReemittingSignal() {
    DocumentPanel panel;
    openstitch::ObjectId first{};
    openstitch::ObjectId second{};
    const Project project = projectWithTwoObjectsAndTwoRegions(first, second);
    panel.refresh(project);

    int embroideryEmits = 0;
    int regionEmits = 0;
    QObject::connect(&panel, &DocumentPanel::embroiderySelected, &panel,
                      [&](openstitch::ObjectId) { ++embroideryEmits; });
    QObject::connect(&panel, &DocumentPanel::regionSelected, &panel,
                      [&](openstitch::RegionId) { ++regionEmits; });

    panel.syncSelection(DocumentPanel::Kind::Embroidery, first.value);

    QCOMPARE(embroideryEmits, 0);
    QCOMPARE(regionEmits, 0);
    QCOMPARE(objectsList(panel)->currentRow(), 0);
}

QTEST_MAIN(DocumentPanelTest)
#include "test_document_panel.moc"
