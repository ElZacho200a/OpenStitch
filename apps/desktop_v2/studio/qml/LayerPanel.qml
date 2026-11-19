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
    // Le calque actif du document : le même dans tous les canevas (clic sur un calque de l'arbre).
    readonly property string layerId: editor.activeLayer
    readonly property bool layerLocked: {
        for (const l of editor.layers) if (l.id === panel.layerId) return l.locked
        return false
    }

    Connections {
        target: studio
        function onDocumentReplaced() {
            objectTree.dragActive = false
            objectTree.anchorId = ""
            renameField.visible = false
            renameField.text = ""
        }
    }

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
            text: qsTr("Dupl.")
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
            tooltip: qsTr("Renommer le calque actif (F2, ou double-clic dans l'arbre)")
            onClicked: panel.startRename()
        }
        StudioButton {
            objectName: "layerRemoveButton"
            text: qsTr("Supprimer")
            implicitHeight: 28
            enabled: editor.layers.length > 1 && panel.layerId.length > 0
            disabledReason: qsTr("Le dernier calque du document ne peut pas être supprimé.")
            onClicked: editor.removeLayer(panel.layerId)
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
            enabled: editor.selectedCount > 0 && panel.layerId.length > 0 && !panel.layerLocked
            disabledReason: panel.layerLocked ? qsTr("Le calque actif est verrouillé.") : ""
            tooltip: qsTr("Déplacer la sélection dans ce calque")
            onClicked: editor.moveSelectionToLayer(panel.layerId)
        }
        StudioButton {
            objectName: "layerIsolateButton"
            text: qsTr("Isoler")
            implicitHeight: 28
            enabled: editor.layers.length > 1 && panel.layerId.length > 0
            tooltip: qsTr("N'afficher que le calque actif ; un second clic réaffiche tout (Alt+clic sur l'oeil)")
            onClicked: editor.isolateLayer(panel.layerId)
        }
        StudioButton {
            objectName: "layerShowAllButton"
            text: qsTr("Tout afficher")
            implicitHeight: 28
            enabled: editor.layers.some(l => !l.visible)
            tooltip: qsTr("Réafficher tous les calques")
            onClicked: editor.showAllLayers()
        }
    }

    function startRename() {
        for (const l of editor.layers) if (l.id === panel.layerId) renameField.text = l.name
        renameField.visible = true
        renameField.forceActiveFocus()
        renameField.selectAll()
    }
    // Raccourcis des calques, actifs quand le panneau est affiché (mêmes dans Vecteurs et Points).
    Shortcut { sequence: "Ctrl+Shift+N"; enabled: panel.visible; onActivated: editor.addLayer(qsTr("Calque %1").arg(editor.layers.length + 1)) }
    Shortcut { sequence: "F2"; enabled: panel.visible && panel.layerId.length > 0; onActivated: panel.startRename() }
    Shortcut { sequence: "Ctrl+Alt+]"; enabled: panel.visible && panel.layerId.length > 0; onActivated: editor.moveLayer(panel.layerId, 1) }
    Shortcut { sequence: "Ctrl+Alt+["; enabled: panel.visible && panel.layerId.length > 0; onActivated: editor.moveLayer(panel.layerId, -1) }

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
