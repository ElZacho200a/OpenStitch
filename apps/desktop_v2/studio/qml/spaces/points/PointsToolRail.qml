// SPDX-License-Identifier: Apache-2.0
import QtQuick
import "../.."

// Barre latérale de l'espace Points sur la barre commune : gestes de numérisation (points, satin,
// barreaux, guide de direction, ligne de rupture). L'espace « Points » ne porte pas les outils de
// dessin vectoriel. Tolérant à l'absence du contexte `digitizer`.
CanvasToolRail {
    id: rail
    objectName: "pointsToolRail"
    readonly property var toolCtl: typeof tools !== "undefined" ? tools : null
    readonly property var digitizeCtl: typeof digitizer !== "undefined" ? digitizer : null
    toolPrefix: "gesture_"
    fitTooltip: qsTr("Ajuster le motif à la fenêtre")
    entries: rail.digitizeCtl ? [
        { id: "points", icon: "stitches", label: qsTr("Geste points : poser des points à la main"), shortcut: "J", group: "gestes" },
        { id: "satin", icon: "satin", label: qsTr("Geste satin : tracer une colonne satin"), shortcut: "S", group: "gestes" },
        { id: "rungs", icon: "rungs", label: qsTr("Barreaux d'un satin"), shortcut: "", group: "gestes" },
        { id: "guide", icon: "guide", label: qsTr("Guide de direction du remplissage"), shortcut: "D", group: "remplissage" },
        { id: "break", icon: "break", label: qsTr("Ligne de rupture du remplissage"), shortcut: "Shift+D", group: "remplissage" }
    ] : []
    activeTool: rail.digitizeCtl ? rail.digitizeCtl.mode : ""
    onToolTriggered: id => {
        if (rail.digitizeCtl) rail.digitizeCtl.mode = id
        if (rail.toolCtl) rail.toolCtl.activeTool = "select"
    }
    onFitRequested: if (rail.canvas) rail.canvas.fitDesign()
}
