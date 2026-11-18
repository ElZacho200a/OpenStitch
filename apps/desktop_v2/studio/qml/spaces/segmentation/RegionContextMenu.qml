// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import "../.."

// Menu contextuel (clic droit) d'une région de segmentation : reprend des actions déjà présentes
// dans RegionTools.qml, là où la main est déjà, plutôt que d'en ajouter d'autres. Aucune logique
// ici : tout passe par `region` (RegionController). Ouvert par SegmentationSpace.qml après avoir
// sélectionné la région sous le curseur (sélection = périmètre des actions, comme le panneau).
Controls.Menu {
    id: menu
    objectName: "regionContextMenu"
    required property var region
    property double regionId: -1
    readonly property bool locked: region !== null && region.isRegionLocked(regionId)
    // `region` est nul quand RegionController n'est pas chargé (contrôleurs optionnels absents,
    // cf. spaceOpensWithoutOptionalControllers) : toujours passer par ce total, jamais
    // `menu.region.selectedCount` directement dans un binding.
    readonly property int selectedCount: region !== null ? region.selectedCount : 0

    function openFor(id, x, y) {
        menu.regionId = id
        popup(x, y)
    }

    Controls.MenuItem {
        objectName: "regionContextMergeNeighbour"
        text: qsTr("Fusionner dans la voisine")
        enabled: !menu.locked
        onTriggered: menu.region.mergeWithNeighbour(menu.regionId)
    }
    Controls.MenuItem {
        objectName: "regionContextChooseTarget"
        text: qsTr("Choisir la cible…")
        enabled: !menu.locked && menu.selectedCount === 1
        onTriggered: menu.region.mergeTargetMode = true
    }
    Controls.MenuItem {
        objectName: "regionContextSplit"
        text: qsTr("Séparer en morceaux")
        enabled: !menu.locked && menu.selectedCount === 1
        onTriggered: menu.region.splitSelected()
    }
    Controls.MenuSeparator { }
    Controls.MenuItem {
        objectName: "regionContextLock"
        text: menu.locked ? qsTr("Déverrouiller") : qsTr("Verrouiller")
        onTriggered: menu.region.toggleLockRegion(menu.regionId)
    }
    Controls.MenuSeparator { }
    Controls.MenuItem {
        objectName: "regionContextDelete"
        text: qsTr("Supprimer")
        enabled: !menu.locked && menu.selectedCount > 0
        onTriggered: menu.region.deleteSelected()
    }
}
