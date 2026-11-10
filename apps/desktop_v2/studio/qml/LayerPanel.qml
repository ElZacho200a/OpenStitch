// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."

// Calques + arbre d'objets : barre d'actions sur le calque courant, puis l'arbre.
ColumnLayout {
    id: panel
    objectName: "layerPanel"
    spacing: 4
    // ObjectTree owns its scrolling. Never let an undersized dock paint over its siblings.
    clip: true
    readonly property alias tree: objectTree
    readonly property string layerId: objectTree.currentLayer.length > 0
                                      ? objectTree.currentLayer
                                      : (editor.layers.length > 0 ? editor.layers[0].id : "")

    Flow {
        objectName: "layerActionsFlow"
        Layout.fillWidth: true
        // Wrapped rows must take their real height above ObjectTree.
        Layout.preferredHeight: childrenRect.height
        Layout.leftMargin: 10
        Layout.rightMargin: 10
        Layout.topMargin: 6
        spacing: 4
        StudioButton {
            objectName: "layerAddButton"
            text: qsTr("+ Calque")
            implicitHeight: 28
            tooltip: qsTr("Créer un calque au-dessus")
            onClicked: editor.addLayer(qsTr("Calque %1").arg(editor.layers.length + 1))
        }
        StudioButton {
            objectName: "layerDuplicateButton"
            text: qsTr("Dupliquer")
            implicitHeight: 28
            enabled: panel.layerId.length > 0
            tooltip: qsTr("Dupliquer le calque et ses objets")
            onClicked: editor.duplicateLayer(panel.layerId)
        }
        StudioButton {
            objectName: "layerRenameButton"
            text: qsTr("Renommer")
            implicitHeight: 28
            enabled: panel.layerId.length > 0
            onClicked: { renameField.text = ""; renameField.visible = true; renameField.forceActiveFocus() }
        }
        StudioButton {
            objectName: "layerRemoveButton"
            text: qsTr("Supprimer")
            implicitHeight: 28
            enabled: editor.layers.length > 1 && panel.layerId.length > 0
            disabledReason: qsTr("Le dernier calque du document ne peut pas être supprimé.")
            onClicked: { editor.removeLayer(panel.layerId); objectTree.currentLayer = "" }
        }
        StudioButton {
            objectName: "layerUpButton"
            text: qsTr("Monter")
            implicitHeight: 28
            enabled: panel.layerId.length > 0
            tooltip: qsTr("Monter le calque")
            onClicked: editor.moveLayer(panel.layerId, 1)
        }
        StudioButton {
            objectName: "layerDownButton"
            text: qsTr("Descendre")
            implicitHeight: 28
            enabled: panel.layerId.length > 0
            tooltip: qsTr("Descendre le calque")
            onClicked: editor.moveLayer(panel.layerId, -1)
        }
        StudioButton {
            objectName: "layerMoveHereButton"
            text: qsTr("Sélection ici")
            implicitHeight: 28
            enabled: editor.selectedCount > 0 && panel.layerId.length > 0
            tooltip: qsTr("Déplacer la sélection dans ce calque")
            onClicked: editor.moveSelectionToLayer(panel.layerId)
        }
    }

    StudioTextField {
        id: renameField
        objectName: "layerRenameField"
        Layout.fillWidth: true
        Layout.leftMargin: 10
        Layout.rightMargin: 10
        visible: false
        placeholderText: qsTr("Nouveau nom du calque")
        color: Theme.foreground
        font.pixelSize: Theme.bodySize
        onAccepted: { editor.renameLayer(panel.layerId, text); visible = false }
        Keys.onEscapePressed: visible = false
    }

    ObjectTree {
        id: objectTree
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 48
    }
}
