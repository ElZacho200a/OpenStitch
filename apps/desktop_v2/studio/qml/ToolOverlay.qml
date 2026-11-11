// SPDX-License-Identifier: Apache-2.0
import QtQuick
import "."

// Aperçu du geste d'outil : un seul élément Canvas qui dessine les quelques polylignes fournies par
// le canevas (jamais un objet QML par point). Repeint au changement de vue ou d'aperçu seulement.
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
        if (!gpu || !gpu.tools) return
        var items = gpu.toolOverlayList()
        for (var i = 0; i < items.length; ++i) {
            var it = items[i], p = it.pts
            ctx.beginPath()
            if (it.kind === "point" || it.kind === "selected") {
                // Nœuds : petits disques dont le rayon suit la taille de la forme (gpu.toolOverlay).
                var sel = it.kind === "selected"
                var r = (it.r !== undefined ? it.r : 3) + (sel ? 0.5 : 0)
                ctx.fillStyle = sel ? String(Theme.accent) : String(Theme.foreground)
                ctx.strokeStyle = String(Theme.accentInk)
                ctx.lineWidth = 1
                for (var k = 0; k + 1 < p.length; k += 2) {
                    ctx.beginPath()
                    ctx.arc(p[k], p[k + 1], r, 0, 2 * Math.PI)
                    ctx.fill()
                    ctx.stroke()
                }
                continue
            }
            ctx.moveTo(p[0], p[1])
            for (var j = 2; j + 1 < p.length; j += 2) ctx.lineTo(p[j], p[j + 1])
            if (it.kind === "closed" || it.kind === "ghost") ctx.closePath()
            ctx.lineWidth = it.kind === "tangent" ? 1 : 1.5
            ctx.strokeStyle = it.kind === "tangent" ? String(Theme.muted) : String(Theme.accent)
            ctx.setLineDash(it.kind === "ghost" ? [5, 3] : [])
            ctx.stroke()
        }
    }
}
