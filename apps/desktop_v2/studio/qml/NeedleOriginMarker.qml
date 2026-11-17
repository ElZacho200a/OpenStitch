// SPDX-License-Identifier: Apache-2.0
import QtQuick
import "."

// Point zéro de l'aiguille : où elle est posée avant de lancer la broderie. Un seul Canvas, repeint
// au changement de vue ou de point zéro. Choisi par l'utilisateur : réticule plein d'accent et
// libellé ; par défaut (centre de la broderie) : réticule discret en pointillés, sans libellé.
Canvas {
    id: overlay
    property var gpu: null
    // Libellé affiché seulement pour un point choisi (le centre par défaut reste discret).
    property bool labelled: true
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
        var o = gpu.needleOrigin
        if (!o || !o.visible) return
        var x = o.x, y = o.y
        if (x < -20 || y < -20 || x > width + 20 || y > height + 20) return
        var colour = String(o.custom ? Theme.accent : Theme.muted)
        ctx.lineWidth = o.custom ? 2 : 1.25
        ctx.strokeStyle = colour
        ctx.setLineDash(o.custom ? [] : [4, 3])
        ctx.beginPath()
        ctx.arc(x, y, 9, 0, 2 * Math.PI)
        ctx.moveTo(x - 15, y); ctx.lineTo(x + 15, y)
        ctx.moveTo(x, y - 15); ctx.lineTo(x, y + 15)
        ctx.stroke()
        if (o.custom) {
            ctx.setLineDash([])
            ctx.beginPath()
            ctx.arc(x, y, 2.5, 0, 2 * Math.PI)
            ctx.fillStyle = colour
            ctx.fill()
            if (labelled) {
                ctx.font = "600 11px sans-serif"
                ctx.fillStyle = colour
                ctx.textAlign = "left"
                ctx.textBaseline = "middle"
                ctx.fillText(qsTr("Point zéro"), x + 18, y - 14)
            }
        }
    }
}
