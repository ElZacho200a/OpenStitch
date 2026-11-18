// SPDX-License-Identifier: Apache-2.0
import QtQuick
import "."

// Frontières des fondus à la frontière de la zone principale (docs/studio-fondu-frontiere-spec.md
// §8) : UN seul Canvas, repeint au changement de vue ou de fondus. Le fondu édité est en trait
// plein d'accent ; les autres fondus de la zone en pointillés discrets. Aucun calcul ici : les
// polylignes viennent du contrôleur `boundaryBlend`, la projection du canevas.
Canvas {
    id: overlay
    property var gpu: null
    property var controller: null
    readonly property var frontiers: controller ? controller.allFrontiers : []
    enabled: false
    renderTarget: Canvas.Image

    Connections {
        target: overlay.gpu
        ignoreUnknownSignals: true
        function onViewChanged() { overlay.requestPaint() }
    }
    onFrontiersChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    function strokeAll(ctx, current) {
        for (var i = 0; i < frontiers.length; ++i) {
            var f = frontiers[i]
            if (f.current !== current) continue
            for (var j = 0; j < f.polylines.length; ++j) {
                var p = gpu.modelToScreen(f.polylines[j])
                if (p.length < 4) continue
                ctx.beginPath()
                ctx.moveTo(p[0], p[1])
                for (var k = 2; k + 1 < p.length; k += 2) ctx.lineTo(p[k], p[k + 1])
                ctx.stroke()
            }
        }
    }

    onPaint: {
        var ctx = getContext("2d")
        ctx.reset()
        if (!gpu || !frontiers || frontiers.length === 0) return
        ctx.lineCap = "round"
        ctx.lineJoin = "round"
        // Les autres fondus d'abord, le fondu édité par-dessus.
        ctx.lineWidth = 1.5
        ctx.strokeStyle = String(Theme.muted)
        ctx.setLineDash([5, 4])
        strokeAll(ctx, false)
        ctx.lineWidth = 2.5
        ctx.strokeStyle = String(Theme.accent)
        ctx.setLineDash([])
        strokeAll(ctx, true)
    }
}
