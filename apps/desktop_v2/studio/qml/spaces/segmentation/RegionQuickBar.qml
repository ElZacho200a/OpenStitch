// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs
import "../.."

// Barre d'actions flottante du canevas Segmentation, sur le patron de VectorQuickBar : les
// retouches de région les plus utilisées (fusionner, séparer, recolorer, verrouiller, supprimer)
// à portée de main dès qu'une sélection existe, sans faire défiler le panneau « Régions ».
// Aucune logique ici : tout passe par `region` (RegionController).
StudioCard {
    id: bar
    objectName: "regionQuickBar"
    required property var region
    // Raccourcis actifs seulement dans cet espace ; mêmes touches que les actions équivalentes de
    // l'espace Vecteurs (Ctrl++ « réunir », Ctrl+Maj+K « séparer ») pour rester cohérent.
    property bool spaceActive: false
    readonly property int count: region !== null ? region.selectedCount : 0
    readonly property bool choosingTarget: region !== null && region.mergeTargetMode
    readonly property bool locked: region !== null && count === 1 && region.isRegionLocked(region.selected[0])
    visible: region !== null && (count > 0 || choosingTarget)
    implicitWidth: row.implicitWidth + 12
    implicitHeight: row.implicitHeight + 10

    component Op: StudioButton {
        implicitHeight: 28
        implicitWidth: Math.max(30, implicitContentWidth + 14)
        quiet: true
        showShortcutLabel: false
    }

    ColorDialog {
        id: quickColorPicker
        objectName: "regionQuickColorDialog"
        title: qsTr("Couleur de la région")
        onAccepted: bar.region.recolorSelected(selectedColor.toString())
    }

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 2
        Text {
            objectName: "regionQuickCount"
            text: qsTr("%1 région(s)").arg(bar.count)
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallSize
            Layout.rightMargin: 6
            Layout.leftMargin: 4
        }
        Op {
            objectName: "quickRegionMerge"
            text: qsTr("Fusionner")
            shortcutLabel: "Ctrl++"
            enabled: !bar.locked && bar.count > 1
            disabledReason: qsTr("Sélectionnez au moins deux régions")
            tooltip: qsTr("Fusionne la sélection en une seule région")
            onClicked: bar.region.mergeSelected()
        }
        Op {
            objectName: "quickRegionMergeNeighbour"
            text: qsTr("Dans la voisine")
            enabled: !bar.locked && bar.count === 1
            disabledReason: qsTr("Sélectionnez une seule région")
            tooltip: qsTr("Fusionne dans sa voisine la plus proche")
            onClicked: bar.region.mergeWithNeighbour(bar.region.selected[0])
        }
        Op {
            objectName: "quickRegionMergeTarget"
            text: bar.choosingTarget ? qsTr("Annuler cible") : qsTr("Choisir la cible…")
            active: bar.choosingTarget
            enabled: !bar.locked && (bar.count === 1 || bar.choosingTarget)
            disabledReason: qsTr("Sélectionnez une seule région source")
            tooltip: qsTr("Cliquez ensuite la région qui doit absorber la sélection")
            onClicked: bar.region.mergeTargetMode = !bar.region.mergeTargetMode
        }
        Rectangle { implicitWidth: 1; implicitHeight: 18; color: Theme.divider; Layout.leftMargin: 3; Layout.rightMargin: 3 }
        Op {
            objectName: "quickRegionSplit"
            text: qsTr("Séparer")
            shortcutLabel: "Ctrl+Maj+K"
            enabled: !bar.locked && bar.count === 1
            disabledReason: qsTr("Sélectionnez une seule région")
            tooltip: qsTr("Sépare les zones non adjacentes en régions distinctes")
            onClicked: bar.region.splitSelected()
        }
        Op {
            objectName: "quickRegionRecolor"
            text: qsTr("Recolorer…")
            enabled: !bar.locked && bar.count > 0
            tooltip: qsTr("Choisit une nouvelle couleur pour la sélection")
            onClicked: {
                const current = bar.count === 1 ? bar.region.originalColorOf(bar.region.selected[0]) : ""
                if (current.length > 0) quickColorPicker.selectedColor = current
                quickColorPicker.open()
            }
        }
        Op {
            objectName: "quickRegionRestoreColor"
            text: qsTr("Couleur d'origine")
            enabled: !bar.locked && bar.count > 0
            tooltip: qsTr("Reprend la couleur observée au début de la session")
            onClicked: bar.region.restoreSelectedColor()
        }
        Rectangle { implicitWidth: 1; implicitHeight: 18; color: Theme.divider; Layout.leftMargin: 3; Layout.rightMargin: 3 }
        Op {
            objectName: "quickRegionLock"
            text: bar.locked ? qsTr("Déverrouiller") : qsTr("Verrouiller")
            active: bar.locked
            enabled: bar.count === 1
            disabledReason: qsTr("Sélectionnez une seule région")
            onClicked: bar.region.toggleLockRegion(bar.region.selected[0])
        }
        Op {
            objectName: "quickRegionDelete"
            text: qsTr("Supprimer")
            enabled: !bar.locked && bar.count > 0
            tooltip: qsTr("Supprime la sélection (absorbée par les voisines)")
            onClicked: bar.region.deleteSelected()
        }
    }

    // Mêmes touches que les actions équivalentes de l'espace Vecteurs (union, séparer) ; « = »
    // couvre les claviers sans pavé numérique, comme VectorQuickBar.
    Shortcut {
        sequences: ["Ctrl++", "Ctrl+="]
        enabled: bar.spaceActive && !bar.locked && bar.count > 1
        onActivated: bar.region.mergeSelected()
    }
    Shortcut {
        sequences: ["Ctrl+Shift+K"]
        enabled: bar.spaceActive && !bar.locked && bar.count === 1
        onActivated: bar.region.splitSelected()
    }
}
