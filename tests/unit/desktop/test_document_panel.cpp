// SPDX-License-Identifier: Apache-2.0
#include <QAction>
#include <QComboBox>
#include <QDropEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QPixmap>
#include <QPushButton>
#include <QScrollBar>
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

// Une forme avec deux couches de couture (tatami puis contour) : le groupe se lit « couches »,
// pas « sections » (HP-OBJ-023).
Project projectWithTwoStitchLayers(std::vector<openstitch::ObjectId>& layerIds) {
    Project project;
    openstitch::document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.name = "Fleur";
    project.vector_objects.push_back(vec);
    for (int i = 0; i < 2; ++i) {
        openstitch::document::EmbroideryObject layer;
        layer.id = project.object_ids.next();
        layer.name = "Fleur couche " + std::to_string(i + 1);
        layer.source_vector = vec.id;
        layer.rgb = {static_cast<std::uint8_t>(40 * (i + 1)), 90, 160};
        if (i == 0) {
            layer.params = openstitch::document::TatamiParams{};
        } else {
            layer.params = openstitch::document::RunningStitchParams{};
        }
        layerIds.push_back(layer.id);
        project.embroidery_objects.push_back(layer);
    }
    return project;
}

} // namespace

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
    void searchFieldHidesRowsThatDoNotMatch();
    void rowsStartWithTheSewingOrderAndShowAFrozenOrderMarker();
    void visibleAndFrozenOrderCheckBoxesEmitSignals();
    void doubleClickRenameEmitsRequestAndRestoresTheFormattedLabel();
    void clickingAGroupNodeSelectsAllItsSections();
    void themedFilterHasMagnifierAndRowsRenderWithoutCrash();
    void ctrlClickOnThreeRowsEmitsThreeIdentifiers();
    void syncObjectsSelectsRowsByTheirSourceShapeWithoutEmitting();
    void unsewnVectorShapesAppearAsIndependentRows();
    void refreshRestoresCollapseSelectionAndScrollByIdentity();
    void reorderedAfterDropMovesTheBlockBeforeOrAfterTheTarget();
    void droppingARowOnAnotherRequestsTheNewSewingOrder();
    void rowContextMenuOffersHideLockAndDeleteAndEmitsRequests();
    void typeIconDiffersPerStitchTypeAndTheTypeFilterHidesOthers();
    void layerGroupRowCountsLayersAndAggregatesTheCheckBoxes();
    void groupCheckBoxRequestsTheChangeForEveryLayer();
    void addLayerButtonFollowsTheSelectionAndTargetsTheTopLayer();
    void rowMenuOffersAddDuplicateAndDeleteOfASingleLayer();
};

void DocumentPanelTest::layerGroupRowCountsLayersAndAggregatesTheCheckBoxes() {
    DocumentPanel panel;
    std::vector<openstitch::ObjectId> ids;
    Project project = projectWithTwoStitchLayers(ids);
    panel.refresh(project);
    auto* objects = objectsList(panel);
    QCOMPARE(objects->topLevelItemCount(), 1);
    QTreeWidgetItem* group = objects->topLevelItem(0);
    QCOMPARE(group->childCount(), 2);
    QVERIFY(group->text(0).contains(QStringLiteral("2 couches")));
    QVERIFY(!group->text(0).contains(QStringLiteral("sections")));
    QCOMPARE(group->checkState(1), Qt::Checked);         // toutes visibles
    QCOMPARE(group->checkState(2), Qt::Unchecked);       // aucun ordre figé
    QVERIFY(group->data(0, Qt::UserRole + 4).isValid()); // pastille du fil de la couche du dessus

    project.embroidery_objects[0].visible = false;
    project.embroidery_objects[1].locked = true;
    panel.refresh(project);
    group = objects->topLevelItem(0);
    QCOMPARE(group->checkState(1), Qt::PartiallyChecked);
    QCOMPARE(group->checkState(2), Qt::PartiallyChecked);
}

void DocumentPanelTest::groupCheckBoxRequestsTheChangeForEveryLayer() {
    DocumentPanel panel;
    std::vector<openstitch::ObjectId> ids;
    Project project = projectWithTwoStitchLayers(ids);
    panel.refresh(project);
    std::optional<std::pair<std::size_t, bool>> hidden;
    QObject::connect(
        &panel, &DocumentPanel::visibilityChangeRequested, &panel,
        [&](const std::vector<openstitch::ObjectId>& v, bool on) { hidden = {v.size(), on}; });
    objectsList(panel)->topLevelItem(0)->setCheckState(1, Qt::Unchecked);
    QTRY_VERIFY(hidden.has_value()); // émission différée
    QCOMPARE(*hidden, (std::pair<std::size_t, bool>{2, false}));
}

void DocumentPanelTest::addLayerButtonFollowsTheSelectionAndTargetsTheTopLayer() {
    DocumentPanel panel;
    std::vector<openstitch::ObjectId> ids;
    Project project = projectWithTwoStitchLayers(ids);
    panel.refresh(project);
    auto* button = panel.findChild<QPushButton*>(QStringLiteral("button_documentAddLayer"));
    QVERIFY(button != nullptr);
    QVERIFY(!button->isEnabled()); // rien de choisi
    std::optional<std::pair<std::uint64_t, bool>> asked;
    QObject::connect(
        &panel, &DocumentPanel::addLayerRequested, &panel,
        [&](openstitch::ObjectId id, bool interactive) { asked = {id.value, interactive}; });
    auto* objects = objectsList(panel);
    objects->topLevelItem(0)->child(0)->setSelected(true); // la couche du bas
    QVERIFY(button->isEnabled());
    button->click();
    QVERIFY(asked.has_value());
    QCOMPARE(asked->first, ids[0].value);
    QVERIFY(!asked->second);

    asked.reset();
    objects->clearSelection();
    objects->topLevelItem(0)->setSelected(true); // la ligne de groupe : on empile sur le dessus
    QVERIFY(button->isEnabled());
    button->click();
    QVERIFY(asked.has_value());
    QCOMPARE(asked->first, ids[1].value);

    objects->topLevelItem(0)->child(0)->setSelected(true); // deux lignes : plus de cible unique
    objects->topLevelItem(0)->child(1)->setSelected(true);
    objects->topLevelItem(0)->setSelected(false);
    QVERIFY(!button->isEnabled());
}

void DocumentPanelTest::rowMenuOffersAddDuplicateAndDeleteOfASingleLayer() {
    DocumentPanel panel;
    std::vector<openstitch::ObjectId> ids;
    Project project = projectWithTwoStitchLayers(ids);
    panel.refresh(project);
    panel.resize(300, 300);
    panel.show();
    auto* objects = objectsList(panel);
    objects->doItemsLayout();
    std::optional<std::pair<std::uint64_t, bool>> added;
    std::optional<std::uint64_t> duplicated;
    QObject::connect(
        &panel, &DocumentPanel::addLayerRequested, &panel,
        [&](openstitch::ObjectId id, bool interactive) { added = {id.value, interactive}; });
    QObject::connect(&panel, &DocumentPanel::duplicateLayerRequested, &panel,
                     [&](openstitch::ObjectId id) { duplicated = id.value; });
    QTreeWidgetItem* second = objects->topLevelItem(0)->child(1);
    QMenu* menu = panel.createContextMenu(objects->visualItemRect(second).center());
    QVERIFY(menu != nullptr);
    auto* add = menu->findChild<QAction*>(QStringLiteral("action_docRowAddLayer"));
    auto* ask = menu->findChild<QAction*>(QStringLiteral("action_docRowAddLayerAsk"));
    auto* dup = menu->findChild<QAction*>(QStringLiteral("action_docRowDuplicateLayer"));
    auto* del = menu->findChild<QAction*>(QStringLiteral("action_docRowDelete"));
    QVERIFY(add != nullptr && ask != nullptr && dup != nullptr && del != nullptr);
    QVERIFY(del->text().contains(QStringLiteral("cette couche")));
    add->trigger();
    ask->trigger();
    dup->trigger();
    delete menu;
    QVERIFY(added.has_value() && duplicated.has_value());
    QCOMPARE(added->first, ids[1].value);
    QVERIFY(added->second); // la dernière activée est la version avec dialogues
    QCOMPARE(*duplicated, ids[1].value);

    // Une forme à couche unique : « Supprimer » garde son libellé simple.
    std::vector<openstitch::ObjectId> single;
    Project one = projectWithTwoStitchLayers(single);
    one.embroidery_objects.pop_back();
    panel.refresh(one);
    objects->doItemsLayout();
    QMenu* oneMenu =
        panel.createContextMenu(objects->visualItemRect(objects->topLevelItem(0)).center());
    QVERIFY(oneMenu != nullptr);
    QVERIFY(!oneMenu->findChild<QAction*>(QStringLiteral("action_docRowDelete"))
                 ->text()
                 .contains(QStringLiteral("couche")));
    delete oneMenu;
}

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
    objects->setCurrentItem(objects->topLevelItem(1)); // second objet ("Tige")

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
    const Project project =
        projectWithGroupedSatinSections(sourceVectorId, sectionIds, standaloneId);

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
    const Project project =
        projectWithGroupedSatinSections(sourceVectorId, sectionIds, standaloneId);
    panel.refresh(project);

    std::optional<openstitch::ObjectId> emittedId;
    QObject::connect(&panel, &DocumentPanel::embroiderySelected, &panel,
                     [&](openstitch::ObjectId id) { emittedId = id; });

    auto* objects = objectsList(panel);
    QTreeWidgetItem* group = objects->topLevelItem(0)->childCount() > 0 ? objects->topLevelItem(0)
                                                                        : objects->topLevelItem(1);
    objects->setCurrentItem(group->child(1)); // deuxième section du groupe

    QVERIFY(emittedId.has_value());
    QCOMPARE(emittedId->value, sectionIds[1].value);
}

void DocumentPanelTest::selectingTheGroupHeaderEmitsNothing() {
    DocumentPanel panel;
    openstitch::ObjectId sourceVectorId{};
    std::vector<openstitch::ObjectId> sectionIds;
    openstitch::ObjectId standaloneId{};
    const Project project =
        projectWithGroupedSatinSections(sourceVectorId, sectionIds, standaloneId);
    panel.refresh(project);

    int emitCount = 0;
    QObject::connect(&panel, &DocumentPanel::embroiderySelected, &panel,
                     [&](openstitch::ObjectId) { ++emitCount; });

    auto* objects = objectsList(panel);
    QTreeWidgetItem* group = objects->topLevelItem(0)->childCount() > 0 ? objects->topLevelItem(0)
                                                                        : objects->topLevelItem(1);
    objects->setCurrentItem(group); // le nœud de groupe lui-même, pas une section

    // Aucun ObjectId propre au groupe : rien à sélectionner côté document.
    QCOMPARE(emitCount, 0);
}

void DocumentPanelTest::syncSelectionFindsAGroupedChildAcrossLevels() {
    DocumentPanel panel;
    openstitch::ObjectId sourceVectorId{};
    std::vector<openstitch::ObjectId> sectionIds;
    openstitch::ObjectId standaloneId{};
    const Project project =
        projectWithGroupedSatinSections(sourceVectorId, sectionIds, standaloneId);
    panel.refresh(project);

    int emitCount = 0;
    QObject::connect(&panel, &DocumentPanel::embroiderySelected, &panel,
                     [&](openstitch::ObjectId) { ++emitCount; });

    // La troisième section est un ENFANT d'un nœud de groupe -- syncSelection
    // doit la retrouver en descendant dans l'arbre, pas seulement parcourir
    // les items de premier niveau.
    panel.syncSelection(DocumentPanel::Kind::Embroidery, sectionIds[2].value);

    QCOMPARE(emitCount, 0); // syncSelection ne réémet jamais (évite la boucle)
    auto* objects = objectsList(panel);
    QVERIFY(objects->currentItem() != nullptr);
    QCOMPARE(objects->currentItem()->data(0, Qt::UserRole).toULongLong(),
             static_cast<qulonglong>(sectionIds[2].value));
}

void DocumentPanelTest::searchFieldHidesRowsThatDoNotMatch() {
    DocumentPanel panel;
    openstitch::ObjectId first{};
    openstitch::ObjectId second{};
    panel.refresh(projectWithTwoObjectsAndTwoRegions(first, second));
    auto* filter = panel.findChild<QLineEdit*>(QStringLiteral("edit_documentFilter"));
    QVERIFY(filter != nullptr);
    auto* objects = objectsList(panel);
    QCOMPARE(objects->topLevelItemCount(), 2);

    filter->setText(QStringLiteral("tige")); // insensible à la casse
    QVERIFY(objects->topLevelItem(0)->isHidden());
    QVERIFY(!objects->topLevelItem(1)->isHidden());

    filter->setText(QStringLiteral("absent"));
    QVERIFY(objects->topLevelItem(0)->isHidden());
    QVERIFY(objects->topLevelItem(1)->isHidden());

    filter->clear();
    QVERIFY(!objects->topLevelItem(0)->isHidden());
    QVERIFY(!objects->topLevelItem(1)->isHidden());

    // Le filtre survit à un rafraîchissement (la liste est reconstruite).
    filter->setText(QStringLiteral("feuille"));
    panel.refresh(projectWithTwoObjectsAndTwoRegions(first, second));
    QVERIFY(!objects->topLevelItem(0)->isHidden());
    QVERIFY(objects->topLevelItem(1)->isHidden());
}

void DocumentPanelTest::rowsStartWithTheSewingOrderAndShowAFrozenOrderMarker() {
    DocumentPanel panel;
    openstitch::ObjectId first{};
    openstitch::ObjectId second{};
    Project project = projectWithTwoObjectsAndTwoRegions(first, second);
    project.embroidery_objects[1].locked = true;
    panel.refresh(project);
    auto* objects = objectsList(panel);
    QVERIFY(objects->topLevelItem(0)->text(0).startsWith(QStringLiteral("1. ")));
    QVERIFY(objects->topLevelItem(1)->text(0).startsWith(QStringLiteral("2. ")));
    QVERIFY(objects->topLevelItem(1)->text(0).contains(QStringLiteral("[ordre figé]")));
    QVERIFY(!objects->topLevelItem(0)->text(0).contains(QStringLiteral("[ordre figé]")));
    // Même libellé que le panneau Ordre (fonction partagée).
    QCOMPARE(objects->topLevelItem(1)->text(0),
             DocumentPanel::itemText(project.embroidery_objects[1], 2));
    QVERIFY(!objects->accessibleName().isEmpty());
    QVERIFY(!regionsList(panel)->accessibleName().isEmpty());
}

void DocumentPanelTest::visibleAndFrozenOrderCheckBoxesEmitSignals() {
    DocumentPanel panel;
    openstitch::ObjectId first{};
    openstitch::ObjectId second{};
    panel.refresh(projectWithTwoObjectsAndTwoRegions(first, second));
    std::optional<std::pair<std::uint64_t, bool>> visible;
    std::optional<std::pair<std::uint64_t, bool>> locked;
    QObject::connect(&panel, &DocumentPanel::visibilityToggled, &panel,
                     [&](openstitch::ObjectId id, bool on) { visible = {id.value, on}; });
    QObject::connect(&panel, &DocumentPanel::orderLockToggled, &panel,
                     [&](openstitch::ObjectId id, bool on) { locked = {id.value, on}; });
    auto* objects = objectsList(panel);
    // Le peuplement initial n'émet rien ; seuls les clics de l'utilisateur le font.
    QVERIFY(!visible.has_value());
    QCOMPARE(objects->topLevelItem(0)->checkState(1), Qt::Checked);
    QCOMPARE(objects->topLevelItem(0)->checkState(2), Qt::Unchecked);
    objects->topLevelItem(1)->setCheckState(1, Qt::Unchecked);
    QTRY_VERIFY(visible.has_value()); // émission différée (la liste est reconstruite ensuite)
    QCOMPARE(visible->first, second.value);
    QVERIFY(!visible->second);
    objects->topLevelItem(0)->setCheckState(2, Qt::Checked);
    QTRY_VERIFY(locked.has_value());
    QCOMPARE(locked->first, first.value);
    QVERIFY(locked->second);
}

void DocumentPanelTest::doubleClickRenameEmitsRequestAndRestoresTheFormattedLabel() {
    DocumentPanel panel;
    openstitch::ObjectId first{};
    openstitch::ObjectId second{};
    panel.refresh(projectWithTwoObjectsAndTwoRegions(first, second));
    std::optional<std::pair<std::uint64_t, QString>> renamed;
    QObject::connect(&panel, &DocumentPanel::renameRequested, &panel,
                     [&](openstitch::ObjectId id, QString name) { renamed = {id.value, name}; });
    auto* objects = objectsList(panel);
    QTreeWidgetItem* item = objects->topLevelItem(0);
    const QString shown = item->text(0);
    emit objects->itemDoubleClicked(item, 0);
    QCOMPARE(item->text(0), QStringLiteral("Feuille")); // nom brut proposé à l'édition
    item->setText(0, QStringLiteral("  Pétale  "));
    QTRY_VERIFY(renamed.has_value());
    QCOMPARE(renamed->first, first.value);
    QCOMPARE(renamed->second, QStringLiteral("Pétale")); // espaces retirés
    QCOMPARE(item->text(0), shown); // libellé formaté restauré (le document fait foi)
}

void DocumentPanelTest::clickingAGroupNodeSelectsAllItsSections() {
    DocumentPanel panel;
    openstitch::ObjectId sourceVectorId{};
    std::vector<openstitch::ObjectId> sectionIds;
    openstitch::ObjectId standaloneId{};
    panel.refresh(projectWithGroupedSatinSections(sourceVectorId, sectionIds, standaloneId));
    std::optional<std::pair<std::uint64_t, int>> grouped;
    QObject::connect(
        &panel, &DocumentPanel::groupSelected, &panel,
        [&](openstitch::ObjectId source, int count) { grouped = {source.value, count}; });
    auto* objects = objectsList(panel);
    QTreeWidgetItem* group = objects->topLevelItem(0)->childCount() > 0 ? objects->topLevelItem(0)
                                                                        : objects->topLevelItem(1);
    objects->setCurrentItem(group);
    QVERIFY(grouped.has_value());
    QCOMPARE(grouped->first, sourceVectorId.value);
    QCOMPARE(grouped->second, 3);
}

void DocumentPanelTest::themedFilterHasMagnifierAndRowsRenderWithoutCrash() {
    DocumentPanel panel;
    openstitch::ObjectId first{};
    openstitch::ObjectId second{};
    panel.refresh(projectWithTwoObjectsAndTwoRegions(first, second));
    auto* filter = panel.findChild<QLineEdit*>(QStringLiteral("edit_documentFilter"));
    QVERIFY(filter != nullptr);
    QVERIFY(!filter->actions().isEmpty());
    QVERIFY(!filter->actions().first()->icon().isNull());
    QVERIFY(!panel.styleSheet().isEmpty());
    panel.resize(320, 240);
    panel.syncSelection(DocumentPanel::Kind::Embroidery, first.value);
    // Rendu complet (délégué : pastille de couleur, pastille de type, sélection).
    const QPixmap shot = panel.grab();
    QVERIFY(!shot.isNull());
    // Les données d'item exploitées par MainWindow restent intactes.
    QCOMPARE(objectsList(panel)->topLevelItem(0)->data(0, Qt::UserRole).toULongLong(), first.value);
}

// Quatre objets de types différents, chacun avec sa forme source (comme après une numérisation).
Project projectWithFourTypedObjects(std::vector<openstitch::ObjectId>& embroideryIds,
                                    std::vector<openstitch::ObjectId>& vectorIds) {
    Project project;
    const char* names[] = {"Aile", "Corps", "Tige", "Oeil"};
    for (int i = 0; i < 4; ++i) {
        openstitch::document::VectorObject vec;
        vec.id = project.object_ids.next();
        vec.name = std::string("Forme ") + names[i];
        vectorIds.push_back(vec.id);
        project.vector_objects.push_back(vec);

        openstitch::document::EmbroideryObject e;
        e.id = project.object_ids.next();
        e.name = names[i];
        e.source_vector = vec.id;
        if (i == 0) {
            e.params = openstitch::document::TatamiParams{};
        } else if (i == 1) {
            e.params = openstitch::document::SatinParams{};
        } else {
            e.params = openstitch::document::RunningStitchParams{};
        }
        embroideryIds.push_back(e.id);
        project.embroidery_objects.push_back(e);
    }
    return project;
}

void DocumentPanelTest::ctrlClickOnThreeRowsEmitsThreeIdentifiers() {
    DocumentPanel panel;
    std::vector<openstitch::ObjectId> emb;
    std::vector<openstitch::ObjectId> vec;
    panel.refresh(projectWithFourTypedObjects(emb, vec));
    auto* objects = objectsList(panel);
    QCOMPARE(objects->selectionMode(), QAbstractItemView::ExtendedSelection);

    std::vector<openstitch::ObjectId> gotEmb;
    int multiEmits = 0;
    QObject::connect(
        &panel, &DocumentPanel::objectsSelected, &panel,
        [&](const std::vector<openstitch::ObjectId>& e, const std::vector<openstitch::ObjectId>&) {
            ++multiEmits;
            gotEmb = e;
        });
    objects->topLevelItem(0)->setSelected(true);
    objects->topLevelItem(2)->setSelected(true);
    objects->topLevelItem(3)->setSelected(true);
    QVERIFY(multiEmits >= 1);
    QCOMPARE(gotEmb.size(), std::size_t{3});
    QCOMPARE(gotEmb[0].value, emb[0].value);
    QCOMPARE(gotEmb[1].value, emb[2].value);
    QCOMPARE(gotEmb[2].value, emb[3].value);

    // Désélectionner tout émet l'effacement de la sélection.
    int cleared = 0;
    QObject::connect(&panel, &DocumentPanel::selectionCleared, &panel, [&] { ++cleared; });
    objects->clearSelection();
    QCOMPARE(cleared, 1);
}

void DocumentPanelTest::syncObjectsSelectsRowsByTheirSourceShapeWithoutEmitting() {
    DocumentPanel panel;
    std::vector<openstitch::ObjectId> emb;
    std::vector<openstitch::ObjectId> vec;
    panel.refresh(projectWithFourTypedObjects(emb, vec));
    int emits = 0;
    QObject::connect(&panel, &DocumentPanel::objectsSelected, &panel,
                     [&](const std::vector<openstitch::ObjectId>&,
                         const std::vector<openstitch::ObjectId>&) { ++emits; });
    QObject::connect(&panel, &DocumentPanel::selectionCleared, &panel, [&] { ++emits; });
    panel.syncObjects({vec[1].value, vec[3].value});
    QCOMPARE(emits, 0);
    auto* objects = objectsList(panel);
    QCOMPARE(objects->selectedItems().size(), 2);
    QVERIFY(objects->topLevelItem(1)->isSelected());
    QVERIFY(objects->topLevelItem(3)->isSelected());
    QVERIFY(!objects->topLevelItem(0)->isSelected());
}

void DocumentPanelTest::unsewnVectorShapesAppearAsIndependentRows() {
    DocumentPanel panel;
    std::vector<openstitch::ObjectId> emb;
    std::vector<openstitch::ObjectId> vec;
    Project project = projectWithFourTypedObjects(emb, vec);
    openstitch::document::VectorObject loose;
    loose.id = project.object_ids.next();
    loose.name = "Forme libre";
    project.vector_objects.push_back(loose);
    panel.refresh(project);
    auto* objects = objectsList(panel);
    QCOMPARE(objects->topLevelItemCount(), 5); // 4 coutures + 1 forme non cousue
    QTreeWidgetItem* row = objects->topLevelItem(4);
    QVERIFY(row->text(0).contains(QStringLiteral("Forme libre")));
    QVERIFY(!row->data(0, Qt::UserRole).isValid()); // pas un objet de broderie

    std::optional<std::uint64_t> picked;
    QObject::connect(&panel, &DocumentPanel::vectorSelected, &panel,
                     [&](openstitch::ObjectId id) { picked = id.value; });
    row->setSelected(true);
    QVERIFY(picked.has_value());
    QCOMPARE(*picked, loose.id.value);

    // Reflet depuis le canevas : la ligne se coche sans réémettre.
    picked.reset();
    panel.syncSelection(DocumentPanel::Kind::None, 0);
    panel.syncSelection(DocumentPanel::Kind::Vector, loose.id.value);
    QVERIFY(!picked.has_value());
    QVERIFY(row->isSelected());
}

void DocumentPanelTest::refreshRestoresCollapseSelectionAndScrollByIdentity() {
    DocumentPanel panel;
    openstitch::ObjectId sourceVectorId{};
    std::vector<openstitch::ObjectId> sectionIds;
    openstitch::ObjectId standaloneId{};
    Project project = projectWithGroupedSatinSections(sourceVectorId, sectionIds, standaloneId);
    // Beaucoup de lignes autonomes : la liste défile.
    for (int i = 0; i < 60; ++i) {
        openstitch::document::EmbroideryObject e;
        e.id = project.object_ids.next();
        e.name = "Ligne " + std::to_string(i);
        e.params = openstitch::document::RunningStitchParams{};
        project.embroidery_objects.push_back(e);
    }
    const openstitch::ObjectId lastId = project.embroidery_objects.back().id;
    panel.resize(300, 200);
    panel.show();
    panel.refresh(project);
    auto* objects = objectsList(panel);

    QTreeWidgetItem* group = nullptr;
    for (int i = 0; i < objects->topLevelItemCount(); ++i) {
        if (objects->topLevelItem(i)->childCount() > 0) {
            group = objects->topLevelItem(i);
        }
    }
    QVERIFY(group != nullptr);
    QVERIFY(group->isExpanded());
    group->setExpanded(false);
    panel.syncSelection(DocumentPanel::Kind::Embroidery, lastId.value);
    objects->doItemsLayout();
    auto* bar = objects->verticalScrollBar();
    QVERIFY(bar->maximum() > 0);
    bar->setValue(bar->maximum());
    const int scrolled = bar->value();

    int emits = 0;
    QObject::connect(&panel, &DocumentPanel::embroiderySelected, &panel,
                     [&](openstitch::ObjectId) { ++emits; });
    QObject::connect(&panel, &DocumentPanel::selectionCleared, &panel, [&] { ++emits; });
    panel.refresh(project);

    QCOMPARE(emits, 0); // la restauration ne se fait jamais passer pour un clic
    QTreeWidgetItem* groupAfter = nullptr;
    for (int i = 0; i < objects->topLevelItemCount(); ++i) {
        if (objects->topLevelItem(i)->childCount() > 0) {
            groupAfter = objects->topLevelItem(i);
        }
    }
    QVERIFY(groupAfter != nullptr);
    QVERIFY(!groupAfter->isExpanded());
    QCOMPARE(objects->selectedItems().size(), 1);
    QCOMPARE(objects->selectedItems().front()->data(0, Qt::UserRole).toULongLong(),
             static_cast<qulonglong>(lastId.value));
    // La position de lecture est conservée (en bas avant : en bas après, au pixel de rang près).
    QVERIFY(scrolled > 0);
    QCOMPARE(objects->verticalScrollBar()->value(), objects->verticalScrollBar()->maximum());
}

void DocumentPanelTest::reorderedAfterDropMovesTheBlockBeforeOrAfterTheTarget() {
    using openstitch::ObjectId;
    const std::vector<ObjectId> order{ObjectId{1}, ObjectId{2}, ObjectId{3}, ObjectId{4}};
    const auto ids = [](const std::vector<ObjectId>& v) {
        std::vector<int> out;
        for (const auto& id : v) {
            out.push_back(static_cast<int>(id.value));
        }
        return out;
    };
    QCOMPARE(ids(DocumentPanel::reorderedAfterDrop(order, {ObjectId{4}}, ObjectId{2}, false)),
             (std::vector<int>{1, 4, 2, 3}));
    QCOMPARE(ids(DocumentPanel::reorderedAfterDrop(order, {ObjectId{1}}, ObjectId{3}, true)),
             (std::vector<int>{2, 3, 1, 4}));
    // Plusieurs lignes : ordre relatif d'origine conservé, quel que soit l'ordre fourni.
    QCOMPARE(ids(DocumentPanel::reorderedAfterDrop(order, {ObjectId{3}, ObjectId{1}}, ObjectId{4},
                                                   true)),
             (std::vector<int>{2, 4, 1, 3}));
    // Cible invalide = à la fin ; déposer sur soi-même = inchangé.
    QCOMPARE(ids(DocumentPanel::reorderedAfterDrop(order, {ObjectId{1}}, ObjectId{}, false)),
             (std::vector<int>{2, 3, 4, 1}));
    QCOMPARE(ids(DocumentPanel::reorderedAfterDrop(order, {ObjectId{2}}, ObjectId{2}, true)),
             (std::vector<int>{1, 2, 3, 4}));
}

void DocumentPanelTest::droppingARowOnAnotherRequestsTheNewSewingOrder() {
    DocumentPanel panel;
    std::vector<openstitch::ObjectId> emb;
    std::vector<openstitch::ObjectId> vec;
    panel.refresh(projectWithFourTypedObjects(emb, vec));
    panel.resize(300, 300);
    panel.show();
    auto* objects = objectsList(panel);
    objects->doItemsLayout();
    std::optional<std::vector<openstitch::ObjectId>> requested;
    QObject::connect(&panel, &DocumentPanel::orderChangeRequested, &panel,
                     [&](const std::vector<openstitch::ObjectId>& order) { requested = order; });

    // La dernière ligne glissée sur le haut de la première : elle passe en tête.
    objects->clearSelection();
    objects->topLevelItem(3)->setSelected(true);
    const QRect first = objects->visualItemRect(objects->topLevelItem(0));
    panel.handleDrop(QPoint(first.center().x(), first.top() + 1));
    QTRY_VERIFY(requested.has_value());
    QCOMPARE(requested->size(), std::size_t{4});
    QCOMPARE((*requested)[0].value, emb[3].value);
    QCOMPARE((*requested)[1].value, emb[0].value);
    // La liste ne bouge pas d'elle-même : seul le document, après la commande, la reconstruit.
    QCOMPARE(objects->topLevelItem(0)->data(0, Qt::UserRole).toULongLong(),
             static_cast<qulonglong>(emb[0].value));
}

void DocumentPanelTest::rowContextMenuOffersHideLockAndDeleteAndEmitsRequests() {
    DocumentPanel panel;
    std::vector<openstitch::ObjectId> emb;
    std::vector<openstitch::ObjectId> vec;
    Project project = projectWithFourTypedObjects(emb, vec);
    openstitch::document::VectorObject loose;
    loose.id = project.object_ids.next();
    loose.name = "Forme libre";
    project.vector_objects.push_back(loose);
    panel.refresh(project);
    panel.resize(300, 300);
    panel.show();
    auto* objects = objectsList(panel);
    objects->doItemsLayout();

    std::optional<std::pair<std::size_t, bool>> hidden;
    std::optional<std::pair<std::size_t, bool>> locked;
    std::optional<std::pair<std::size_t, std::size_t>> deleted;
    QObject::connect(
        &panel, &DocumentPanel::visibilityChangeRequested, &panel,
        [&](const std::vector<openstitch::ObjectId>& ids, bool on) { hidden = {ids.size(), on}; });
    QObject::connect(
        &panel, &DocumentPanel::orderLockChangeRequested, &panel,
        [&](const std::vector<openstitch::ObjectId>& ids, bool on) { locked = {ids.size(), on}; });
    QObject::connect(
        &panel, &DocumentPanel::deleteRequested, &panel,
        [&](const std::vector<openstitch::ObjectId>& e,
            const std::vector<openstitch::ObjectId>& v) { deleted = {e.size(), v.size()}; });

    // Deux lignes sélectionnées : le menu agit sur les deux.
    objects->clearSelection();
    objects->topLevelItem(0)->setSelected(true);
    objects->topLevelItem(1)->setSelected(true);
    QMenu* menu =
        panel.createContextMenu(objects->visualItemRect(objects->topLevelItem(0)).center());
    QVERIFY(menu != nullptr);
    auto* vis = menu->findChild<QAction*>(QStringLiteral("action_docRowVisibility"));
    auto* lock = menu->findChild<QAction*>(QStringLiteral("action_docRowLock"));
    auto* del = menu->findChild<QAction*>(QStringLiteral("action_docRowDelete"));
    QVERIFY(vis != nullptr && lock != nullptr && del != nullptr);
    QVERIFY(vis->text().contains(QStringLiteral("Masquer")));
    QVERIFY(lock->text().contains(QStringLiteral("Figer")));
    vis->trigger();
    lock->trigger();
    del->trigger();
    delete menu;
    QVERIFY(hidden.has_value() && locked.has_value() && deleted.has_value());
    QCOMPARE(*hidden, (std::pair<std::size_t, bool>{2, false}));
    QCOMPARE(*locked, (std::pair<std::size_t, bool>{2, true}));
    QCOMPARE(*deleted, (std::pair<std::size_t, std::size_t>{2, 0}));

    // Clic droit sur une forme non cousue : seule la suppression est proposée.
    deleted.reset();
    QMenu* vecMenu =
        panel.createContextMenu(objects->visualItemRect(objects->topLevelItem(4)).center());
    QVERIFY(vecMenu != nullptr);
    QVERIFY(vecMenu->findChild<QAction*>(QStringLiteral("action_docRowVisibility")) == nullptr);
    vecMenu->findChild<QAction*>(QStringLiteral("action_docRowDelete"))->trigger();
    delete vecMenu;
    QVERIFY(deleted.has_value());
    QCOMPARE(*deleted, (std::pair<std::size_t, std::size_t>{0, 1}));
}

void DocumentPanelTest::typeIconDiffersPerStitchTypeAndTheTypeFilterHidesOthers() {
    DocumentPanel panel;
    std::vector<openstitch::ObjectId> emb;
    std::vector<openstitch::ObjectId> vec;
    panel.refresh(projectWithFourTypedObjects(emb, vec));
    auto* objects = objectsList(panel);
    // Tatami (0), satin (1), contour (2 et 3) : icônes distinctes, identiques entre contours.
    QVERIFY(!objects->topLevelItem(0)->icon(0).isNull());
    QVERIFY(objects->topLevelItem(0)->icon(0).cacheKey() !=
            objects->topLevelItem(1)->icon(0).cacheKey());
    QVERIFY(objects->topLevelItem(1)->icon(0).cacheKey() !=
            objects->topLevelItem(2)->icon(0).cacheKey());
    QCOMPARE(objects->topLevelItem(2)->icon(0).cacheKey(),
             objects->topLevelItem(3)->icon(0).cacheKey());

    auto* combo = panel.findChild<QComboBox*>(QStringLiteral("combo_documentTypeFilter"));
    QVERIFY(combo != nullptr);
    const int satin = combo->findText(QStringLiteral("Satin"));
    QVERIFY(satin > 0);
    combo->setCurrentIndex(satin);
    QVERIFY(objects->topLevelItem(0)->isHidden());
    QVERIFY(!objects->topLevelItem(1)->isHidden());
    QVERIFY(objects->topLevelItem(2)->isHidden());
    // Cumul avec la recherche texte, et survie à un rafraîchissement.
    auto* filter = panel.findChild<QLineEdit*>(QStringLiteral("edit_documentFilter"));
    filter->setText(QStringLiteral("absent"));
    QVERIFY(objects->topLevelItem(1)->isHidden());
    filter->clear();
    std::vector<openstitch::ObjectId> emb2;
    std::vector<openstitch::ObjectId> vec2;
    panel.refresh(projectWithFourTypedObjects(emb2, vec2));
    QVERIFY(objects->topLevelItem(0)->isHidden());
    QVERIFY(!objects->topLevelItem(1)->isHidden());
    combo->setCurrentIndex(0);
    QVERIFY(!objects->topLevelItem(0)->isHidden());
}

QTEST_MAIN(DocumentPanelTest)
#include "test_document_panel.moc"
