// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQml
import "."

// Barre latérale de l'espace Vecteurs : outils de dessin et d'édition vectorielle sur la barre
// commune. Libellés, raccourcis, groupes et ordre viennent du ToolRegistry (via
// ToolController.tools) ; seule l'icône est choisie ici. Tolérant à l'absence du contexte `tools`.
CanvasToolRail {
    id: rail
    objectName: "toolRail"
    readonly property var toolCtl: typeof tools !== "undefined" ? tools : null
    readonly property var icons: ({
        "draw.rectangle": "rectangle", "draw.ellipse": "ellipse", "draw.polygon": "polygon",
        "draw.polygon-regular": "regular", "draw.bezier": "bezier", "draw.freeform": "freeform",
        "node.edit": "node", "knife": "knife", "warp-brush": "warp", "bucket": "bucket"
    })
    fitTooltip: qsTr("Ajuster le motif à la fenêtre")
    toolPrefix: "tool_"
    entries: {
        if (!rail.toolCtl)
            return []
        return rail.toolCtl.tools.map(t => ({
            id: t.id, icon: rail.icons[t.id] || "edit", label: t.label, shortcut: t.shortcut,
            group: t.group
        }))
    }
    activeTool: rail.toolCtl && rail.toolCtl.activeTool !== "select" ? rail.toolCtl.activeTool : ""
    onToolTriggered: id => { if (rail.toolCtl) rail.toolCtl.activeTool = id === "" ? "select" : id }
    onFitRequested: if (rail.canvas) rail.canvas.fitDesign()
}
