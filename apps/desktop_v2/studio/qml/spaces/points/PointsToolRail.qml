// SPDX-License-Identifier: Apache-2.0
import QtQuick
import "../.."

// Barre latérale de l'espace Points sur la barre commune : sélection au lasso (même outil que
// l'espace Vecteurs), gestes de numérisation (points, satin, barreaux, guide de direction, ligne de
// rupture). L'espace « Points » ne porte pas les outils de dessin vectoriel. Tolérant à l'absence
// du contexte `digitizer`.
CanvasToolRail {
    id: rail
    objectName: "pointsToolRail"
    readonly property var toolCtl: typeof tools !== "undefined" ? tools : null
    readonly property var digitizeCtl: typeof digitizer !== "undefined" ? digitizer : null
    readonly property var satinCtl: typeof satin !== "undefined" ? satin : null
    toolPrefix: "gesture_"
    fitTooltip: qsTr("Ajuster le motif à la fenêtre")
    // Outils du contrôleur d'outils (pas des gestes de numérisation) : id de la barre → id d'outil.
    readonly property var toolIds: ({ "lasso": "select.lasso", "needle": "needle.origin" })
    readonly property var lassoEntry: {
        const spec = rail.toolCtl ? rail.toolCtl.tools.find(t => t.id === "select.lasso") : undefined
        return spec ? [{ id: "lasso", icon: "lasso", label: spec.label, shortcut: spec.shortcut, group: "selection" }] : []
    }
    entries: rail.lassoEntry.concat(rail.digitizeCtl ? [
        { id: "points", icon: "stitches", label: qsTr("Geste points : poser des points à la main"), shortcut: "J", group: "gestes" },
        { id: "satin", icon: "satin", label: qsTr("Geste satin : tracer une colonne satin"), shortcut: "S", group: "gestes" },
        { id: "rungs", icon: "rungs", label: qsTr("Barreaux d'un satin"), shortcut: "", group: "gestes" },
        { id: "guide", icon: "guide", label: rail.satinCtl && rail.satinCtl.isGuidedSatin
            ? qsTr("Ligne centrale du satin") : qsTr("Guide de direction du remplissage"), shortcut: "D", group: "remplissage" },
        { id: "break", icon: "break", label: qsTr("Ligne de rupture du remplissage"), shortcut: "Shift+D", group: "remplissage" },
        { id: "needle", icon: "needle", label: qsTr("Point zéro de l'aiguille : où elle est posée avant de lancer la broderie (par défaut au centre)"), shortcut: "O", group: "machine" }
    ] : [])
    activeTool: rail.toolCtl && rail.toolCtl.activeTool === "needle.origin" ? "needle"
              : rail.toolCtl && rail.toolCtl.activeTool === "select.lasso" ? "lasso"
              : rail.digitizeCtl ? (rail.digitizeCtl.mode === "guided" ? "guide" : rail.digitizeCtl.mode) : ""
    onToolTriggered: id => activateTool(id)
    function activateTool(id) {
        if (rail.toolIds[id] !== undefined) {
            // Lasso et point zéro appartiennent au contrôleur d'outils ; les gestes de
            // numérisation se désactivent pour leur laisser la souris.
            if (rail.digitizeCtl) rail.digitizeCtl.mode = ""
            if (rail.toolCtl) rail.toolCtl.activeTool = rail.toolIds[id]
            return
        }
        if (rail.digitizeCtl)
            rail.digitizeCtl.mode = id === "guide" && rail.satinCtl && rail.satinCtl.isGuidedSatin ? "guided" : id
        if (rail.toolCtl) rail.toolCtl.activeTool = "select"
    }
    onFitRequested: if (rail.canvas) rail.canvas.fitDesign()
}
