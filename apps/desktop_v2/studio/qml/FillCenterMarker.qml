// SPDX-License-Identifier: Apache-2.0
import QtQuick
import "."

// Viseur du centre d'un remplissage radial, en spirale ou concentrique (objet seul sélectionné).
// Un seul Canvas, repeint au changement de vue ou de sélection ; le geste (glisser, double-clic)
// est porté par le canevas. Centre automatique : réticule en pointillés et libellé « Centre auto » ;
// centre fixé : réticule plein d'accent et libellé « Centre ».
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
        // Halo sombre sous le réticule : lisible sur n'importe quel fil.
        ctx.lineWidth = 4
        ctx.strokeStyle = String(Theme.background)
        ctx.globalAlpha = 0.65
        ctx.beginPath()
        ctx.arc(x, y, 10, 0, 2 * Math.PI)
        ctx.moveTo(x - 16, y); ctx.lineTo(x + 16, y)
        ctx.moveTo(x, y - 16); ctx.lineTo(x, y + 16)
        ctx.stroke()
        ctx.globalAlpha = 1
        ctx.lineWidth = 2
        ctx.strokeStyle = colour
        ctx.setLineDash(c.fixed ? [] : [4, 3])
        ctx.beginPath()
        ctx.arc(x, y, 10, 0, 2 * Math.PI)
        ctx.moveTo(x - 16, y); ctx.lineTo(x + 16, y)
        ctx.moveTo(x, y - 16); ctx.lineTo(x, y + 16)
        ctx.stroke()
        ctx.setLineDash([])
        ctx.beginPath()
        ctx.arc(x, y, 3, 0, 2 * Math.PI)
        ctx.fillStyle = colour
        ctx.fill()
        if (!c.dragging) {
            ctx.font = "600 11px sans-serif"
            ctx.textAlign = "left"
            ctx.textBaseline = "middle"
            var label = c.fixed ? qsTr("Centre") : qsTr("Centre auto")
            ctx.lineWidth = 3
            ctx.strokeStyle = String(Theme.background)
            ctx.strokeText(label, x + 19, y - 15)
            ctx.fillStyle = colour
            ctx.fillText(label, x + 19, y - 15)
        }
    }
}
