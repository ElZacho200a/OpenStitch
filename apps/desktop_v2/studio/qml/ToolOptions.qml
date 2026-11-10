// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."

// Options de l'outil actif, accrochage et opérations vectorielles sur la sélection. Tolérant à
// l'absence des contextes `tools` et `vectors`.
ColumnLayout {
    id: panel
    objectName: "toolOptions"
    readonly property var toolCtl: typeof tools !== "undefined" ? tools : null
    readonly property var vec: typeof vectors !== "undefined" ? vectors : null
    visible: toolCtl !== null
    spacing: 6

    component Mini: StudioButton {
        implicitHeight: 26
        implicitWidth: Math.max(30, implicitContentWidth + 14)
    }

    SectionLabel { text: qsTr("OUTIL") }
    Text {
        objectName: "toolHint"
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: Theme.muted
        font.pixelSize: Theme.smallSize
        text: panel.toolCtl ? (panel.toolCtl.hint !== "" ? panel.toolCtl.hint : qsTr("Choisissez un outil dans le rail.")) : ""
    }
    Text {
        objectName: "toolMessage"
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: Theme.faint
        font.pixelSize: Theme.smallSize
        text: panel.toolCtl ? panel.toolCtl.message : ""
        visible: text !== ""
    }

    RowLayout {
        visible: panel.toolCtl && panel.toolCtl.activeTool === "draw.polygon-regular"
        spacing: 6
        Text { text: qsTr("Côtés"); color: Theme.muted; font.pixelSize: Theme.smallSize }
        StudioSpinBox {
            objectName: "sidesSpin"
            from: 3; to: 64
            value: panel.toolCtl ? panel.toolCtl.sides : 6
            onValueModified: panel.toolCtl.sides = value
        }
        StudioCheckBox {
            objectName: "starCheck"
            text: qsTr("Étoile")
            checked: panel.toolCtl ? panel.toolCtl.star : false
            onToggled: panel.toolCtl.star = checked
        }
    }

    SectionLabel { text: qsTr("ACCROCHAGE") }
    Flow {
        Layout.fillWidth: true
        spacing: 6
        StudioCheckBox {
            objectName: "snapVerticesCheck"
            text: qsTr("Sommets")
            checked: panel.toolCtl ? panel.toolCtl.snapVertices : false
            onToggled: panel.toolCtl.snapVertices = checked
        }
        StudioCheckBox {
            objectName: "snapGridCheck"
            text: qsTr("Grille")
            checked: panel.toolCtl ? panel.toolCtl.snapGrid : false
            onToggled: panel.toolCtl.snapGrid = checked
        }
        StudioSpinBox {
            objectName: "snapRadiusSpin"
            from: 2; to: 40
            value: panel.toolCtl ? panel.toolCtl.snapRadiusPx : 8
            onValueModified: panel.toolCtl.snapRadiusPx = value
            StudioToolTip { text: qsTr("Rayon d'accrochage (px)") }
        }
    }

    Flow {
        Layout.fillWidth: true
        spacing: 3
        visible: panel.toolCtl && panel.toolCtl.activeTool === "node.edit"
        Mini { objectName: "nodeCornerButton"; text: qsTr("Anguleux"); shortcutLabel: "C"; enabled: panel.toolCtl && panel.toolCtl.selectedNodeCount > 0; onClicked: panel.toolCtl.setSelectedNodeKind(false) }
        Mini { objectName: "nodeSmoothButton"; text: qsTr("Lisses"); shortcutLabel: "S"; enabled: panel.toolCtl && panel.toolCtl.selectedNodeCount > 0; onClicked: panel.toolCtl.setSelectedNodeKind(true) }
        Mini { objectName: "nodeDeleteButton"; text: qsTr("Suppr. nœud"); enabled: panel.toolCtl && panel.toolCtl.selectedNodeCount > 0; onClicked: panel.toolCtl.deleteSelectedNodes() }
        Mini { objectName: "nodeHoleButton"; text: qsTr("Suppr. trou"); enabled: panel.toolCtl && panel.toolCtl.selectedNodeCount > 0; onClicked: panel.toolCtl.removeHole() }
    }

    SectionLabel { text: qsTr("OPÉRATIONS SUR LES FORMES") }
    Flow {
        Layout.fillWidth: true
        spacing: 3
        enabled: panel.vec !== null
        readonly property bool two: panel.vec && panel.vec.selectedCount > 1
        readonly property bool some: panel.vec && panel.vec.selectedCount > 0
        Mini { objectName: "unionButton"; text: qsTr("Union"); enabled: parent.two; onClicked: panel.vec.booleanOp("union") }
        Mini { objectName: "subtractButton"; text: qsTr("Soustraction"); enabled: parent.two; onClicked: panel.vec.booleanOp("subtract") }
        Mini { objectName: "intersectButton"; text: qsTr("Intersection"); enabled: parent.two; onClicked: panel.vec.booleanOp("intersect") }
        Mini { objectName: "excludeButton"; text: qsTr("Exclusion"); enabled: parent.two; onClicked: panel.vec.booleanOp("exclude") }
        Mini { objectName: "mergeButton"; text: qsTr("Fusionner"); enabled: parent.two; onClicked: panel.vec.mergeObjects() }
        Mini { objectName: "breakApartButton"; text: qsTr("Séparer"); enabled: parent.some; onClicked: panel.vec.breakApart() }
        Mini { objectName: "simplifyButton"; text: qsTr("Simplifier"); enabled: parent.some; onClicked: panel.vec.simplify(0.1) }
        Mini { objectName: "smoothButton"; text: qsTr("Lisser"); enabled: parent.some; onClicked: panel.vec.smooth(0.15) }
        Mini { objectName: "fillHolesButton"; text: qsTr("Combler trous"); enabled: parent.some; onClicked: panel.vec.fillHoles(4) }
        Mini { objectName: "toCurvesButton"; text: qsTr("En courbes"); enabled: parent.some; onClicked: panel.vec.convertToCurves() }
        StudioTextField {
            id: offsetField
            objectName: "offsetMmField"
            implicitWidth: 52
            text: "1.0"
            enabled: parent.some
            invalid: isNaN(parseFloat(text.replace(",", ".")))
            Accessible.name: qsTr("Décalage en millimètres")
        }
        Mini { objectName: "offsetOutButton"; text: qsTr("Décaler +"); enabled: parent.some && !offsetField.invalid; onClicked: panel.vec.offsetShapes(Math.abs(parseFloat(offsetField.text.replace(",", ".")))) }
        Mini { objectName: "offsetInButton"; text: qsTr("Décaler −"); enabled: parent.some && !offsetField.invalid; onClicked: panel.vec.offsetShapes(-Math.abs(parseFloat(offsetField.text.replace(",", ".")))) }
    }
    Text {
        objectName: "vectorMessage"
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: Theme.faint
        font.pixelSize: Theme.smallSize
        text: panel.vec ? panel.vec.message : ""
        visible: text !== ""
    }
}
