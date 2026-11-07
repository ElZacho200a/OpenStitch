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
        Controls.SpinBox {
            objectName: "sidesSpin"
            from: 3; to: 64
            value: panel.toolCtl ? panel.toolCtl.sides : 6
            onValueModified: panel.toolCtl.sides = value
        }
        Controls.CheckBox {
            objectName: "starCheck"
            text: qsTr("Étoile")
            checked: panel.toolCtl ? panel.toolCtl.star : false
            onToggled: panel.toolCtl.star = checked
        }
    }

    SectionLabel { text: qsTr("ACCROCHAGE") }
    ColumnLayout {
        visible: panel.toolCtl && panel.toolCtl.activeTool === "warp-brush"
        Layout.fillWidth: true
        Text { text: qsTr("Rayon du pinceau (mm)"); color: Theme.muted }
        Controls.SpinBox {
            objectName: "warpRadius"
            from: 5; to: 600; stepSize: 5
            value: panel.toolCtl ? Math.round(panel.toolCtl.warp.radiusMm * 10) : 50
            textFromValue: function(value, locale) { return Number(value / 10).toLocaleString(locale, 'f', 1) }
            valueFromText: function(text, locale) { return Math.round(Number.fromLocaleString(locale, text) * 10) }
            editable: true
            Accessible.name: qsTr("Rayon du pinceau en millimètres")
            onValueModified: panel.toolCtl.warp.radiusMm = value / 10
        }
        Text { text: qsTr("Intensité (%)"); color: Theme.muted }
        Controls.SpinBox {
            objectName: "warpStrength"
            from: 5; to: 100; value: panel.toolCtl ? Math.round(panel.toolCtl.warp.strength * 100) : 60
            editable: true
            Accessible.name: qsTr("Intensité du pinceau en pourcentage")
            onValueModified: panel.toolCtl.warp.strength = value / 100
        }
    }
    Flow {
        Layout.fillWidth: true
        spacing: 6
        Controls.CheckBox {
            objectName: "snapVerticesCheck"
            text: qsTr("Sommets")
            checked: panel.toolCtl ? panel.toolCtl.snapVertices : false
            onToggled: panel.toolCtl.snapVertices = checked
        }
        Controls.CheckBox {
            objectName: "snapGridCheck"
            text: qsTr("Grille")
            checked: panel.toolCtl ? panel.toolCtl.snapGrid : false
            onToggled: panel.toolCtl.snapGrid = checked
        }
        Controls.SpinBox {
            objectName: "snapRadiusSpin"
            from: 2; to: 40
            value: panel.toolCtl ? panel.toolCtl.snapRadiusPx : 8
            onValueModified: panel.toolCtl.snapRadiusPx = value
            Controls.ToolTip.visible: hovered
            Controls.ToolTip.text: qsTr("Rayon d'accrochage (px)")
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
