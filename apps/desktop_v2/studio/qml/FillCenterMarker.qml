// SPDX-License-Identifier: Apache-2.0
import QtQuick
import "."

// Viseur du centre d'un remplissage radial, en spirale ou concentrique (objet seul sélectionné).
// Un seul Canvas, repeint au changement de vue ou de sélection ; le geste (glisser, double-clic)
// est porté par le canevas. Losange (le point zéro de l'aiguille est un cercle) : en pointillés
// pour le centre automatique, plein d'accent pour un centre fixé. Concentrique sans centre : les
// anneaux suivent le bord, le libellé le dit. Libellé sous le viseur, hors des poignées latérales.
Canvas {
    id: overlay
    property var gpu: null
    enabled: false
    renderTarget: Canvas.Image

    Connections {
        target: overlay.gpu
        ignoreUnknownSignals: true
        function onOverlayChanged() { overlay.requestPaint() }
        function onViewChanged() { overlay.requestPaint() }
    }
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d")
        ctx.reset()
        if (!gpu) return
        var c = gpu.fillCenter
        if (!c || !c.visible) return
        var x = c.x, y = c.y
        if (x < -20 || y < -20 || x > width + 20 || y > height + 20) return
        var colour = String(c.fixed ? Theme.accent : Theme.foreground)
        var r = 11
        function shape() {
            ctx.beginPath()
            ctx.moveTo(x, y - r); ctx.lineTo(x + r, y); ctx.lineTo(x, y + r); ctx.lineTo(x - r, y)
            ctx.closePath()
            ctx.moveTo(x - r - 6, y); ctx.lineTo(x - r + 3, y)
            ctx.moveTo(x + r - 3, y); ctx.lineTo(x + r + 6, y)
            ctx.moveTo(x, y - r - 6); ctx.lineTo(x, y - r + 3)
            ctx.moveTo(x, y + r - 3); ctx.lineTo(x, y + r + 6)
        }
        // Liseré clair puis halo sombre : lisible sur un fil foncé comme sur un fil rose ou rouge.
        ctx.lineWidth = 6
        ctx.strokeStyle = String(Theme.foreground)
        ctx.globalAlpha = 0.35
        shape(); ctx.stroke()
        ctx.lineWidth = 4
        ctx.strokeStyle = String(Theme.background)
        ctx.globalAlpha = 0.75
        shape(); ctx.stroke()
        ctx.globalAlpha = 1
        ctx.lineWidth = 2
        ctx.strokeStyle = colour
        ctx.setLineDash(c.fixed ? [] : [4, 3])
        shape(); ctx.stroke()
        ctx.setLineDash([])
        ctx.beginPath()
        ctx.arc(x, y, 3, 0, 2 * Math.PI)
        ctx.fillStyle = colour
        ctx.fill()
        if (!c.dragging) {
            ctx.font = "600 11px sans-serif"
            ctx.textAlign = "center"
            ctx.textBaseline = "top"
            var label = c.fixed ? qsTr("Centre du remplissage")
                      : c.rings ? qsTr("Anneaux du bord · glisser pour centrer")
                      : qsTr("Centre auto · glisser pour déplacer")
            ctx.lineWidth = 3
            ctx.strokeStyle = String(Theme.background)
            ctx.strokeText(label, x, y + r + 9)
            ctx.fillStyle = colour
            ctx.fillText(label, x, y + r + 9)
        }
    }
}
