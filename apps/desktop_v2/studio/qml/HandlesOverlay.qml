// SPDX-License-Identifier: Apache-2.0
import QtQuick
import "."

// Poignées et nuage de points des gestes de numérisation : UN seul Canvas, alimenté par le canevas
// (points déjà élagués à la fenêtre, quelques polylignes). Jamais un objet QML par point ; repeint
// au changement de vue ou d'aperçu seulement.
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
        if (!gpu || !gpu.digitize) return
        var items = gpu.handlesOverlayList()
        for (var i = 0; i < items.length; ++i) {
            var it = items[i], p = it.pts, k, j
            ctx.beginPath()
            if (it.kind === "dots") {
                for (k = 0; k + 1 < p.length; k += 2) ctx.rect(p[k] - 1.5, p[k + 1] - 1.5, 3, 3)
                ctx.fillStyle = String(Theme.muted)
                ctx.fill()
            } else if (it.kind === "selected" || it.kind === "active") {
                for (k = 0; k + 1 < p.length; k += 2) ctx.rect(p[k] - 3.5, p[k + 1] - 3.5, 7, 7)
                ctx.fillStyle = String(Theme.accent)
                ctx.fill()
            } else if (it.kind === "handle") {
                for (k = 0; k + 1 < p.length; k += 2) ctx.rect(p[k] - 4.5, p[k + 1] - 4.5, 9, 9)
                ctx.fillStyle = String(Theme.foreground)
                ctx.fill()
                ctx.strokeStyle = String(Theme.accent)
                ctx.lineWidth = 1.5
                ctx.stroke()
            } else {
                ctx.moveTo(p[0], p[1])
                for (j = 2; j + 1 < p.length; j += 2) ctx.lineTo(p[j], p[j + 1])
                if (it.kind === "closed") ctx.closePath()
                ctx.lineWidth = 1.5
                ctx.strokeStyle = String(it.kind === "ghost" ? Theme.muted : Theme.accent)
                ctx.setLineDash(it.kind === "ghost" || it.kind === "closed" ? [5, 3] : [])
                ctx.stroke()
            }
        }
    }
}
