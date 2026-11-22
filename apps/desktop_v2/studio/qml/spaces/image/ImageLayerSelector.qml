// SPDX-License-Identifier: Apache-2.0
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import "../.."

// Sélecteur de calque image (visible seulement si plusieurs calques portent une image, même règle
// que les autres espaces) : le menu déroulant des noms (`imageTools.imageLayers`), complété ligne
// par ligne par l'état visible / verrouillé de chaque calque (`space.imageLayerStates`, le même
// ordre et la même longueur) et un accès direct pour les basculer sans changer d'espace ni passer
// par ObjectTree / LayerPanel.
ColumnLayout {
    id: root
    objectName: "imageLayerSelectorGroup"
    required property var space // ImageSpaceController
    property var layerNames: [] // imageTools.imageLayers
    property int currentIndex: -1 // imageTools.imageLayerIndex
    signal layerActivated(int index)
    spacing: 6
    // Un seul calque : rien à choisir (même règle que les autres espaces).
    visible: root.layerNames.length > 1

    StudioComboBox {
        id: combo
        objectName: "imageLayerSelector"
        Layout.fillWidth: true
        model: root.layerNames
        currentIndex: root.currentIndex
        Accessible.name: qsTr("Calque image actif")
        onActivated: index => root.layerActivated(index)
    }

    // Une ligne par calque image : nom, visibilité, verrou. Les icônes reflètent l'état réel du
    // document ; cliquer bascule l'état (une étape d'annulation), même pour un calque qui n'est
    // pas la sélection courante.
    Repeater {
        model: root.layerNames.length
        delegate: RowLayout {
            id: row
            required property int index
            objectName: "imageLayerRow_" + index
            Layout.fillWidth: true
            spacing: 2
            readonly property var layerState: root.space && index < root.space.imageLayerStates.length
                                               ? root.space.imageLayerStates[index]
                                               : { "visible": true, "locked": false }

            StudioLabel {
                text: index >= 0 && index < root.layerNames.length ? root.layerNames[row.index] : ""
                role: "small"
                tone: row.index === root.currentIndex ? "foreground" : "faint"
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            StudioButton {
                objectName: "imageLayerVisibleToggle_" + row.index
                quiet: true
                iconName: "view"
                active: row.layerState.visible
                tooltip: row.layerState.visible ? qsTr("Masquer ce calque") : qsTr("Afficher ce calque")
                onClicked: if (root.space) root.space.toggleLayerVisible(row.index)
            }
            StudioButton {
                objectName: "imageLayerLockToggle_" + row.index
                quiet: true
                iconName: "lock"
                active: row.layerState.locked
                tooltip: row.layerState.locked ? qsTr("Déverrouiller ce calque") : qsTr("Verrouiller ce calque")
                onClicked: if (root.space) root.space.toggleLayerLocked(row.index)
            }
        }
    }
}
