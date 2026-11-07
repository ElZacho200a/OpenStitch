// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQml
import QtQuick.Layouts
import "../.."

// Rail des OUTILS DE POINTS (l'espace « Points » ne porte plus les outils de dessin vectoriel) :
// sélection et déplacement de la vue, outils de numérisation (satin, guides de direction, lignes
// de rupture, axe du fondu, retouche de points, texte, pipette, règle), ajustement du motif.
// Libellés, raccourcis et ordre viennent du ToolRegistry (`tools.tools`). Les raccourcis sont des
// `Shortcut` QML indépendants du focus. Tolérant à l'absence du contexte `tools`.
ColumnLayout {
    id: rail
    objectName: "pointsToolRail"
    required property var canvas
    readonly property var toolCtl: typeof tools !== "undefined" ? tools : null
    readonly property var groups: [
        ["satin.column", "fill.direction-guide", "fill.break-line", "fade.axis"],
        ["stitch.points", "click-to-stitch", "text", "eyedropper", "measure"]
    ]
    readonly property var icons: ({
        "satin.column": "satin", "fill.direction-guide": "guide", "fill.break-line": "break",
        "fade.axis": "fade", "stitch.points": "stitches", "click-to-stitch": "needle",
        "text": "text", "eyedropper": "eyedropper", "measure": "measure"
    })
    spacing: 6

    function shortcutOf(text) { return text.replace("Maj+", "Shift+") }
    function entriesOf(ids) {
        const out = []
        if (!toolCtl)
            return out
        const all = toolCtl.tools
        for (let i = 0; i < ids.length; ++i)
            for (let j = 0; j < all.length; ++j)
                if (all[j].id === ids[i])
                    out.push(all[j])
        return out
    }
    function allEntries() { return entriesOf(groups[0]).concat(entriesOf(groups[1])) }
    function pick(id) {
        canvas.panMode = false
        toolCtl.activeTool = toolCtl.activeTool === id ? "select" : id
    }

    StudioButton {
        objectName: "selectToolButton"
        iconName: "select"
        tooltip: studio.selectionTool.label
        shortcutLabel: studio.selectionTool.shortcut
        quiet: true
        active: !rail.canvas.panMode && (!rail.toolCtl || rail.toolCtl.activeTool === "select")
        onClicked: { rail.canvas.panMode = false; if (rail.toolCtl) rail.toolCtl.activeTool = "select" }
    }
    StudioButton {
        objectName: "panToolButton"
        iconName: "pan"
        tooltip: studio.panTool.label
        shortcutLabel: studio.panTool.shortcut
        quiet: true
        active: rail.canvas.panMode
        onClicked: { rail.canvas.panMode = true; if (rail.toolCtl) rail.toolCtl.activeTool = "select" }
    }

    Repeater {
        model: rail.groups
        delegate: ColumnLayout {
            id: group
            required property var modelData
            Layout.alignment: Qt.AlignHCenter
            spacing: 6
            visible: rail.toolCtl !== null
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 4
                Layout.bottomMargin: 4
                implicitWidth: 24
                implicitHeight: 1
                color: Theme.divider
            }
            Repeater {
                model: rail.entriesOf(group.modelData)
                delegate: StudioButton {
                    required property var modelData
                    objectName: "tool_" + modelData.id
                    iconName: rail.icons[modelData.id] || "select"
                    quiet: true
                    tooltip: modelData.label
                    shortcutLabel: modelData.shortcut
                    active: rail.toolCtl && rail.toolCtl.activeTool === modelData.id
                    onClicked: rail.pick(modelData.id)
                }
            }
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

    Instantiator {
        model: rail.toolCtl ? rail.allEntries() : []
        delegate: Shortcut {
            required property var modelData
            sequence: rail.shortcutOf(modelData.shortcut)
            enabled: sequence !== ""
            onActivated: rail.pick(modelData.id)
        }
    }
}
