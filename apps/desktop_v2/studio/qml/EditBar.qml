// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQml
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."

// Actions d'édition de la sélection : sélection, transformations, ordre, alignement, placement en mm.
ColumnLayout {
    id: bar
    objectName: "editBar"
    spacing: 6
    readonly property bool some: editor.selectedCount > 0
    readonly property bool many: editor.selectedCount > 1
    readonly property var box: editor.selectionBox
    readonly property bool hasBox: box.width !== undefined

    component Mini: StudioButton {
        implicitHeight: 26
        implicitWidth: Math.max(30, implicitContentWidth + 14)
    }
    component Num: StudioTextField {
        implicitWidth: 52
        implicitHeight: 26
        color: Theme.foreground
        font.pixelSize: Theme.smallSize
        selectByMouse: true
        validator: DoubleValidator { notation: DoubleValidator.StandardNotation; locale: "C" }
        background: Rectangle { color: Theme.raised; border.color: Theme.border; radius: 3 }
    }

    SectionLabel { text: qsTr("ÉDITION") }
    Flow {
        Layout.fillWidth: true
        spacing: 3
        Mini { objectName: "selectAllButton"; text: qsTr("Tout"); tooltip: qsTr("Tout sélectionner"); shortcutLabel: "Ctrl+A"; onClicked: editor.selectAll() }
        Mini { objectName: "invertButton"; text: qsTr("Inverser"); shortcutLabel: "Ctrl+I"; onClicked: editor.invertSelection() }
        Mini { objectName: "sameColorButton"; text: qsTr("Même couleur"); enabled: bar.some; onClicked: editor.selectSameColor() }
        Mini { objectName: "neighborsButton"; text: qsTr("Voisines"); enabled: bar.some; onClicked: editor.selectNeighbors() }
        Mini { text: qsTr("Type ▾"); onClicked: typeMenu.popup() }
        Mini { text: qsTr("Mémoriser"); enabled: bar.some; onClicked: editor.saveSelection(qsTr("Sélection %1").arg(editor.savedSelections.length + 1)) }
        Mini { text: qsTr("Rappeler ▾"); enabled: editor.savedSelections.length > 0; onClicked: recallMenu.popup() }
    }
    Flow {
        Layout.fillWidth: true
        spacing: 3
        Mini { objectName: "rotateCwButton"; text: "⟳90"; enabled: bar.some; tooltip: qsTr("Pivoter de 90° (horaire)"); shortcutLabel: "R"; onClicked: editor.rotate90(true) }
        Mini { objectName: "rotateCcwButton"; text: "⟲90"; enabled: bar.some; tooltip: qsTr("Pivoter de 90° (antihoraire)"); shortcutLabel: "Maj+R"; onClicked: editor.rotate90(false) }
        Mini { objectName: "mirrorHButton"; text: "⇋"; enabled: bar.some; tooltip: qsTr("Miroir horizontal"); onClicked: editor.mirror(true) }
        Mini { objectName: "mirrorVButton"; text: "⇅"; enabled: bar.some; tooltip: qsTr("Miroir vertical"); onClicked: editor.mirror(false) }
        Mini { objectName: "duplicateButton"; text: qsTr("Dupliquer"); enabled: bar.some; shortcutLabel: "Ctrl+D"; onClicked: editor.duplicate(5) }
        Mini { objectName: "deleteButton"; text: qsTr("Supprimer"); enabled: bar.some; shortcutLabel: "Suppr"; onClicked: editor.deleteSelection() }
        Mini { objectName: "frontButton"; text: qsTr("Devant"); enabled: bar.some; shortcutLabel: "Fin"; onClicked: editor.stack("front") }
        Mini { objectName: "backButton"; text: qsTr("Derrière"); enabled: bar.some; shortcutLabel: "Origine"; onClicked: editor.stack("back") }
        Mini { objectName: "groupButton"; text: qsTr("Grouper"); enabled: bar.many; shortcutLabel: "Ctrl+G"; onClicked: editor.group("") }
        Mini { objectName: "ungroupButton"; text: qsTr("Dégrouper"); enabled: bar.some; shortcutLabel: "Ctrl+Maj+G"; onClicked: editor.ungroup() }
        Mini { text: qsTr("Aligner ▾"); enabled: bar.many; onClicked: alignMenu.popup() }
        Mini { objectName: "pipetteButton"; text: qsTr("Pipette"); checkable: true; checked: editor.eyedropper; enabled: bar.some; tooltip: qsTr("Copier la couleur d'un objet vers la sélection"); shortcutLabel: "E"; onClicked: editor.eyedropper = !editor.eyedropper }
        Mini { objectName: "snapButton"; text: qsTr("Accrochage"); checkable: true; checked: editor.snapEnabled; tooltip: qsTr("Accrochage et repères au glisser (Alt = temporaire)"); onClicked: editor.snapEnabled = !editor.snapEnabled }
    }

    // Placement numérique (centre et taille de la boîte, en mm modèle : Y vers le haut).
    RowLayout {
        Layout.fillWidth: true
        spacing: 4
        visible: bar.hasBox
        Text { text: "X"; color: Theme.faint; font.pixelSize: Theme.smallSize }
        Num { id: xField; objectName: "placeX"; text: bar.hasBox ? bar.box.cx.toFixed(2) : "" }
        Text { text: "Y"; color: Theme.faint; font.pixelSize: Theme.smallSize }
        Num { id: yField; objectName: "placeY"; text: bar.hasBox ? (-bar.box.cy).toFixed(2) : "" }
        Text { text: "L"; color: Theme.faint; font.pixelSize: Theme.smallSize }
        Num { id: wField; objectName: "placeW"; text: bar.hasBox ? bar.box.width.toFixed(2) : "" }
        Text { text: "H"; color: Theme.faint; font.pixelSize: Theme.smallSize }
        Num { id: hField; objectName: "placeH"; text: bar.hasBox ? bar.box.height.toFixed(2) : "" }
    }
    RowLayout {
        Layout.fillWidth: true
        visible: bar.hasBox
        spacing: 4
        Text { text: "∠°"; color: Theme.faint; font.pixelSize: Theme.smallSize }
        Num { id: angleField; objectName: "placeAngle"; text: "0" }
        Mini {
            objectName: "placeApply"
            text: qsTr("Placer")
            tooltip: qsTr("Appliquer position, taille et rotation (mm)")
            onClicked: {
                const same = function(field, value) { return Math.abs(parseFloat(field.text) - value) < 0.005 }
                editor.placeSelection(
                    same(xField, bar.box.cx) ? NaN : parseFloat(xField.text),
                    same(yField, -bar.box.cy) ? NaN : parseFloat(yField.text),
                    same(wField, bar.box.width) ? NaN : parseFloat(wField.text),
                    same(hField, bar.box.height) ? NaN : parseFloat(hField.text),
                    parseFloat(angleField.text) || 0)
                angleField.text = "0"
            }
        }
    }
    Text {
        Layout.fillWidth: true
        visible: editor.message.length > 0
        text: editor.message
        color: Theme.muted
        font.pixelSize: Theme.smallSize
        wrapMode: Text.Wrap
    }

    Controls.Menu {
        id: typeMenu
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
    Controls.Menu {
        id: recallMenu
        Instantiator {
            model: editor.savedSelections
            Controls.MenuItem {
                required property string modelData
                text: modelData
                onTriggered: editor.recallSelection(modelData)
            }
            onObjectAdded: function(index, object) { recallMenu.insertItem(index, object) }
            onObjectRemoved: function(index, object) { recallMenu.removeItem(object) }
        }
    }
    Controls.Menu {
        id: alignMenu
        Controls.MenuItem { text: qsTr("À gauche"); onTriggered: editor.align("left") }
        Controls.MenuItem { text: qsTr("Centrer horizontalement"); onTriggered: editor.align("centerH") }
        Controls.MenuItem { text: qsTr("À droite"); onTriggered: editor.align("right") }
        Controls.MenuItem { text: qsTr("En haut"); onTriggered: editor.align("top") }
        Controls.MenuItem { text: qsTr("Centrer verticalement"); onTriggered: editor.align("centerV") }
        Controls.MenuItem { text: qsTr("En bas"); onTriggered: editor.align("bottom") }
        Controls.MenuItem { text: qsTr("Répartir horizontalement"); enabled: editor.selectedCount > 2; onTriggered: editor.distribute(true) }
        Controls.MenuItem { text: qsTr("Répartir verticalement"); enabled: editor.selectedCount > 2; onTriggered: editor.distribute(false) }
    }
}
