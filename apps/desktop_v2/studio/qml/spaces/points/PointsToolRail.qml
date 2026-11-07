// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Layouts
import "../.."

// Rail des OUTILS DE POINTS (l'espace « Points » ne porte plus les outils de dessin vectoriel) :
// sélection et déplacement de la vue, gestes de numérisation (points, fondu, satin, guide de
// direction, ligne de rupture, barreaux), ajustement du motif. Les raccourcis sont des `Shortcut`
// QML indépendants du focus. Tolérant à l'absence du contexte `digitizer`.
ColumnLayout {
    id: rail
    objectName: "pointsToolRail"
    required property var canvas
    readonly property var toolCtl: typeof tools !== "undefined" ? tools : null
    readonly property var digitizeCtl: typeof digitizer !== "undefined" ? digitizer : null
    // [mode, libellé, icône, infobulle, raccourci]
    readonly property var gestures: [
        ["points", qsTr("Points"), "stitches", qsTr("Geste points : poser des points à la main"), "J"],
        ["satin", qsTr("Satin"), "satin", qsTr("Geste satin : tracer une colonne satin"), "S"],
        ["rungs", qsTr("Barreaux"), "rungs", qsTr("Barreaux d'un satin"), ""],
        ["fade", qsTr("Fondu"), "fade", qsTr("Geste fondu : tracer l'axe d'un fondu"), "L"],
        ["guide", qsTr("Guide"), "guide", qsTr("Guide de direction du remplissage"), "D"],
        ["break", qsTr("Rupture"), "break", qsTr("Ligne de rupture du remplissage"), "Shift+D"]
    ]
    spacing: 6

    function toggleMode(mode) {
        if (!digitizeCtl)
            return
        canvas.panMode = false
        digitizeCtl.mode = digitizeCtl.mode === mode ? "" : mode
    }

    StudioButton {
        objectName: "selectToolButton"
        iconName: "select"
        tooltip: studio.selectionTool.label
        shortcutLabel: studio.selectionTool.shortcut
        quiet: true
        active: !rail.canvas.panMode && (!rail.digitizeCtl || rail.digitizeCtl.mode === "")
        onClicked: {
            rail.canvas.panMode = false
            if (rail.digitizeCtl) rail.digitizeCtl.mode = ""
            if (rail.toolCtl) rail.toolCtl.activeTool = "select"
        }
    }
    StudioButton {
        objectName: "panToolButton"
        iconName: "pan"
        tooltip: studio.panTool.label
        shortcutLabel: studio.panTool.shortcut
        quiet: true
        active: rail.canvas.panMode
        onClicked: {
            rail.canvas.panMode = true
            if (rail.digitizeCtl) rail.digitizeCtl.mode = ""
            if (rail.toolCtl) rail.toolCtl.activeTool = "select"
        }
    }

    Rectangle {
        visible: rail.digitizeCtl !== null
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: 4
        Layout.bottomMargin: 4
        implicitWidth: 24
        implicitHeight: 1
        color: Theme.divider
    }
    Repeater {
        model: rail.digitizeCtl ? rail.gestures : []
        delegate: StudioButton {
            required property var modelData
            objectName: "gesture_" + modelData[0]
            iconName: modelData[2]
            quiet: true
            checkable: true
            checked: rail.digitizeCtl !== null && rail.digitizeCtl.mode === modelData[0]
            tooltip: modelData[3]
            shortcutLabel: modelData[4]
            Layout.alignment: Qt.AlignHCenter
            onClicked: rail.toggleMode(modelData[0])
        }
    }

    Rectangle {
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: 4
        Layout.bottomMargin: 4
        implicitWidth: 24
        implicitHeight: 1
        color: Theme.divider
    }
    StudioButton {
        objectName: "fitToolButton"
        iconName: "fit"
        tooltip: qsTr("Ajuster le motif à la fenêtre")
        shortcutLabel: "F"
        quiet: true
        onClicked: rail.canvas.fitDesign()
    }

    Repeater {
        model: rail.digitizeCtl ? rail.gestures : []
        delegate: Item {
            id: holder
            required property var modelData
            Shortcut {
                sequence: holder.modelData[4]
                enabled: sequence !== ""
                onActivated: rail.toggleMode(holder.modelData[0])
            }
        }
    }
}
