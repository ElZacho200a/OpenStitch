// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQml
import QtQuick.Controls as Controls
import "."

// Menu contextuel du canevas (clic droit). Ouvert par `openAt` ; chaque entrée appelle l'EditController.
Controls.Menu {
    id: menu
    objectName: "canvasContextMenu"
    readonly property bool hasSelection: editor.selectedCount > 0
    function openAt(x, y) { popup(x, y) }

    Controls.Menu {
        title: qsTr("Sélection")
        Controls.MenuItem { text: qsTr("Tout sélectionner"); onTriggered: editor.selectAll() }
        Controls.MenuItem { text: qsTr("Inverser la sélection"); onTriggered: editor.invertSelection() }
        Controls.MenuItem { text: qsTr("Même couleur"); enabled: menu.hasSelection; onTriggered: editor.selectSameColor() }
        Controls.MenuItem { text: qsTr("Avec les voisines"); enabled: menu.hasSelection; onTriggered: editor.selectNeighbors() }
        Controls.MenuItem { text: qsTr("Désélectionner"); enabled: menu.hasSelection; onTriggered: editor.selectNone() }
    }
    Controls.Menu {
        id: typeMenu
        title: qsTr("Type de points")
        Instantiator {
            model: editor.stitchTypes
            Controls.MenuItem {
                required property string modelData
                text: modelData
                onTriggered: editor.selectByType(modelData)
            }
            onObjectAdded: function(index, object) { typeMenu.insertItem(index, object) }
            onObjectRemoved: function(index, object) { typeMenu.removeItem(object) }
        }
    }
    Controls.MenuSeparator { }
    Controls.MenuItem { objectName: "contextDuplicate"; text: qsTr("Dupliquer"); enabled: menu.hasSelection; onTriggered: editor.duplicate(5) }
    Controls.MenuItem {
        objectName: "contextDelete"
        text: qsTr("Supprimer %1 objet(s)").arg(editor.selectedCount)
        enabled: menu.hasSelection
        onTriggered: editor.deleteSelection()
    }
    Controls.MenuSeparator { }
    Controls.Menu {
        title: qsTr("Transformer")
        enabled: menu.hasSelection
        Controls.MenuItem { text: qsTr("Pivoter de 90° (horaire)"); onTriggered: editor.rotate90(true) }
        Controls.MenuItem { text: qsTr("Pivoter de 90° (antihoraire)"); onTriggered: editor.rotate90(false) }
        Controls.MenuItem { text: qsTr("Miroir horizontal"); onTriggered: editor.mirror(true) }
        Controls.MenuItem { text: qsTr("Miroir vertical"); onTriggered: editor.mirror(false) }
    }
    Controls.Menu {
        title: qsTr("Ordre de couture")
        enabled: menu.hasSelection
        Controls.MenuItem { text: qsTr("Mettre au premier plan"); onTriggered: editor.stack("front") }
        Controls.MenuItem { text: qsTr("Avancer"); onTriggered: editor.stack("forward") }
        Controls.MenuItem { text: qsTr("Reculer"); onTriggered: editor.stack("backward") }
        Controls.MenuItem { text: qsTr("Mettre à l'arrière-plan"); onTriggered: editor.stack("back") }
    }
    Controls.Menu {
        title: qsTr("Aligner")
        enabled: editor.selectedCount > 1
        Controls.MenuItem { text: qsTr("À gauche"); onTriggered: editor.align("left") }
        Controls.MenuItem { text: qsTr("Centrer horizontalement"); onTriggered: editor.align("centerH") }
        Controls.MenuItem { text: qsTr("À droite"); onTriggered: editor.align("right") }
        Controls.MenuItem { text: qsTr("En haut"); onTriggered: editor.align("top") }
        Controls.MenuItem { text: qsTr("Centrer verticalement"); onTriggered: editor.align("centerV") }
        Controls.MenuItem { text: qsTr("En bas"); onTriggered: editor.align("bottom") }
        Controls.MenuItem { text: qsTr("Répartir horizontalement"); enabled: editor.selectedCount > 2; onTriggered: editor.distribute(true) }
        Controls.MenuItem { text: qsTr("Répartir verticalement"); enabled: editor.selectedCount > 2; onTriggered: editor.distribute(false) }
    }
    Controls.MenuItem { text: qsTr("Grouper"); enabled: editor.selectedCount > 1; onTriggered: editor.group("") }
    Controls.MenuItem { text: qsTr("Dégrouper"); enabled: menu.hasSelection; onTriggered: editor.ungroup() }
    Controls.Menu {
        id: layerMenu
        title: qsTr("Déplacer vers le calque")
        enabled: menu.hasSelection && editor.layers.length > 0
        Instantiator {
            model: editor.layers
            Controls.MenuItem {
                required property var modelData
                text: modelData.name
                onTriggered: editor.moveSelectionToLayer(modelData.id)
            }
            onObjectAdded: function(index, object) { layerMenu.insertItem(index, object) }
            onObjectRemoved: function(index, object) { layerMenu.removeItem(object) }
        }
    }
}
