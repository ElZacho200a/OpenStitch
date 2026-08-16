// SPDX-License-Identifier: Apache-2.0
#include <QListWidget>
#include <QTabWidget>
#include <QTest>
#include <QTreeWidget>

#include <optional>
#include <vector>

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
// contrat stable observé depuis l'extérieur. objectsList_ est un QTreeWidget
// depuis §21 (regroupement des sections d'un même plan satin, 2026-08-15) --
// regionsList_ reste un QListWidget (aucun regroupement là).
QTreeWidget* objectsList(DocumentPanel& panel) {
    auto* tabs = panel.findChild<QTabWidget*>();
    return qobject_cast<QTreeWidget*>(tabs->widget(0));
}
QListWidget* regionsList(DocumentPanel& panel) {
    auto* tabs = panel.findChild<QTabWidget*>();
    return qobject_cast<QListWidget*>(tabs->widget(1));
}

// Trois sections satin partageant le MÊME `source_vector` valide (comme
// `satin_planning::create_satin_plan` en produirait pour une forme branchée)
// plus un objet autonome sans rapport (`source_vector` invalide, jamais
// regroupé) -- §21 du plan de refonte satin (2026-08-15).
Project projectWithGroupedSatinSections(openstitch::ObjectId& sourceVectorId,
                                         std::vector<openstitch::ObjectId>& sectionIds,
                                         openstitch::ObjectId& standaloneId) {
    Project project;

    openstitch::document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.name = "Forme branchee";
    sourceVectorId = vec.id;
    project.vector_objects.push_back(vec);

    for (int i = 0; i < 3; ++i) {
        openstitch::document::EmbroideryObject section;
        section.id = project.object_ids.next();
        section.name = "Satin section " + std::to_string(i + 1);
        section.source_vector = sourceVectorId;
        section.params = openstitch::document::SatinParams{};
        section.intent = openstitch::document::EmbroideryIntent::ForcedUserChoice;
        sectionIds.push_back(section.id);
        project.embroidery_objects.push_back(section);
    }

    openstitch::document::EmbroideryObject standalone;
    standalone.id = project.object_ids.next();
    standalone.name = "Contour autonome";
    standalone.params = openstitch::document::RunningStitchParams{};
    standaloneId = standalone.id;
    project.embroidery_objects.push_back(standalone);

    return project;
}

}  // namespace

class DocumentPanelTest : public QObject {
    Q_OBJECT

private slots:
    void refreshPopulatesObjectsAndRegionsLists();
    void selectingAnObjectRowEmitsEmbroiderySelectedWithMatchingId();
    void syncSelectionReflectsSelectionWithoutReemittingSignal();
    void multiSectionSatinPlanGroupsUnderOneParentNode();
    void selectingAGroupedSectionEmitsItsOwnId();
    void selectingTheGroupHeaderEmitsNothing();
    void syncSelectionFindsAGroupedChildAcrossLevels();
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
    // Aucun des deux objets n'a de `source_vector` valide (fixture minimale) :
    // jamais regroupés, deux items de premier niveau -- comportement visuel
    // identique à l'ancienne liste plate.
    QCOMPARE(objects->topLevelItemCount(), 2);
    QCOMPARE(regions->count(), 2);
    QCOMPARE(objects->topLevelItem(0)->data(0, Qt::UserRole).toULongLong(),
              static_cast<qulonglong>(first.value));
    QCOMPARE(objects->topLevelItem(1)->data(0, Qt::UserRole).toULongLong(),
              static_cast<qulonglong>(second.value));
    QVERIFY(objects->topLevelItem(0)->text(0).contains(QStringLiteral("Feuille")));
    QVERIFY(objects->topLevelItem(1)->text(0).contains(QStringLiteral("Tige")));
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

    auto* objects = objectsList(panel);
    objects->setCurrentItem(objects->topLevelItem(1));  // second objet ("Tige")

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
    auto* objects = objectsList(panel);
    QCOMPARE(objects->currentItem(), objects->topLevelItem(0));
}

void DocumentPanelTest::multiSectionSatinPlanGroupsUnderOneParentNode() {
    DocumentPanel panel;
    openstitch::ObjectId sourceVectorId{};
    std::vector<openstitch::ObjectId> sectionIds;
    openstitch::ObjectId standaloneId{};
    const Project project = projectWithGroupedSatinSections(sourceVectorId, sectionIds, standaloneId);

    panel.refresh(project);

    auto* objects = objectsList(panel);
    QVERIFY(objects != nullptr);
    // Un groupe (les 3 sections) + un item de premier niveau autonome --
    // jamais 4 items plats, jamais un groupe autour de l'objet autonome seul.
    QCOMPARE(objects->topLevelItemCount(), 2);

    QTreeWidgetItem* group = nullptr;
    QTreeWidgetItem* standaloneItem = nullptr;
    for (int i = 0; i < objects->topLevelItemCount(); ++i) {
        QTreeWidgetItem* top = objects->topLevelItem(i);
        if (top->childCount() > 0) {
            group = top;
        } else {
            standaloneItem = top;
        }
    }
    QVERIFY(group != nullptr);
    QVERIFY(standaloneItem != nullptr);
    QCOMPARE(group->childCount(), 3);
    // Le nœud de groupe lui-même ne porte pas d'ObjectId (rien à sélectionner).
    QVERIFY(!group->data(0, Qt::UserRole).isValid());
    QCOMPARE(standaloneItem->data(0, Qt::UserRole).toULongLong(),
              static_cast<qulonglong>(standaloneId.value));

    // Chaque enfant porte bien l'ObjectId de SA section, dans l'ordre d'ajout.
    for (int c = 0; c < group->childCount(); ++c) {
        QCOMPARE(group->child(c)->data(0, Qt::UserRole).toULongLong(),
                  static_cast<qulonglong>(sectionIds[static_cast<std::size_t>(c)].value));
    }
}

void DocumentPanelTest::selectingAGroupedSectionEmitsItsOwnId() {
    DocumentPanel panel;
    openstitch::ObjectId sourceVectorId{};
    std::vector<openstitch::ObjectId> sectionIds;
    openstitch::ObjectId standaloneId{};
    const Project project = projectWithGroupedSatinSections(sourceVectorId, sectionIds, standaloneId);
    panel.refresh(project);

    std::optional<openstitch::ObjectId> emittedId;
    QObject::connect(&panel, &DocumentPanel::embroiderySelected, &panel,
                      [&](openstitch::ObjectId id) { emittedId = id; });

    auto* objects = objectsList(panel);
    QTreeWidgetItem* group = objects->topLevelItem(0)->childCount() > 0 ? objects->topLevelItem(0)
                                                                        : objects->topLevelItem(1);
    objects->setCurrentItem(group->child(1));  // deuxième section du groupe

    QVERIFY(emittedId.has_value());
    QCOMPARE(emittedId->value, sectionIds[1].value);
}

void DocumentPanelTest::selectingTheGroupHeaderEmitsNothing() {
    DocumentPanel panel;
    openstitch::ObjectId sourceVectorId{};
    std::vector<openstitch::ObjectId> sectionIds;
    openstitch::ObjectId standaloneId{};
    const Project project = projectWithGroupedSatinSections(sourceVectorId, sectionIds, standaloneId);
    panel.refresh(project);

    int emitCount = 0;
    QObject::connect(&panel, &DocumentPanel::embroiderySelected, &panel,
                      [&](openstitch::ObjectId) { ++emitCount; });

    auto* objects = objectsList(panel);
    QTreeWidgetItem* group = objects->topLevelItem(0)->childCount() > 0 ? objects->topLevelItem(0)
                                                                        : objects->topLevelItem(1);
    objects->setCurrentItem(group);  // le nœud de groupe lui-même, pas une section

    // Aucun ObjectId propre au groupe : rien à sélectionner côté document.
    QCOMPARE(emitCount, 0);
}

void DocumentPanelTest::syncSelectionFindsAGroupedChildAcrossLevels() {
    DocumentPanel panel;
    openstitch::ObjectId sourceVectorId{};
    std::vector<openstitch::ObjectId> sectionIds;
    openstitch::ObjectId standaloneId{};
    const Project project = projectWithGroupedSatinSections(sourceVectorId, sectionIds, standaloneId);
    panel.refresh(project);

    int emitCount = 0;
    QObject::connect(&panel, &DocumentPanel::embroiderySelected, &panel,
                      [&](openstitch::ObjectId) { ++emitCount; });

    // La troisième section est un ENFANT d'un nœud de groupe -- syncSelection
    // doit la retrouver en descendant dans l'arbre, pas seulement parcourir
    // les items de premier niveau.
    panel.syncSelection(DocumentPanel::Kind::Embroidery, sectionIds[2].value);

    QCOMPARE(emitCount, 0);  // syncSelection ne réémet jamais (évite la boucle)
    auto* objects = objectsList(panel);
    QVERIFY(objects->currentItem() != nullptr);
    QCOMPARE(objects->currentItem()->data(0, Qt::UserRole).toULongLong(),
              static_cast<qulonglong>(sectionIds[2].value));
}

QTEST_MAIN(DocumentPanelTest)
#include "test_document_panel.moc"
