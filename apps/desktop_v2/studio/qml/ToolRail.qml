// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQml
import QtQuick.Layouts
import "."

// Rail des outils de dessin et d'édition vectorielle : libellés, raccourcis et ordre viennent du
// ToolRegistry (via ToolController.tools). Tolérant à l'absence du contexte `tools`.
ColumnLayout {
    id: rail
    objectName: "toolRail"
    property bool workspaceActive: true
    readonly property var toolCtl: typeof tools !== "undefined" ? tools : null
    visible: toolCtl !== null
    spacing: 6

    function shortcutOf(text) { return text.replace("Maj+", "Shift+") }

    Repeater {
        model: rail.toolCtl ? rail.toolCtl.tools : []
        delegate: StudioButton {
            required property var modelData
            objectName: "tool_" + modelData.id
            text: modelData.glyph
            quiet: true
            tooltip: modelData.label
            shortcutLabel: modelData.shortcut
            active: rail.toolCtl && rail.toolCtl.activeTool === modelData.id
            Accessible.description: active ? qsTr("Outil actif") : modelData.label
            onClicked: rail.toolCtl.activeTool = active ? "select" : modelData.id
        }
    }

    Instantiator {
        model: rail.toolCtl ? rail.toolCtl.tools : []
        delegate: Shortcut {
            required property var modelData
            sequence: rail.shortcutOf(modelData.shortcut)
            enabled: rail.workspaceActive && sequence !== ""
            onActivated: rail.toolCtl.activeTool = rail.toolCtl.activeTool === modelData.id ? "select" : modelData.id
        }
    }
}
