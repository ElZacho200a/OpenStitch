// SPDX-License-Identifier: Apache-2.0
import QtQuick
import "."

// Vignette d'un style de fondu, dessinée avec le premier et le dernier fil : un repère visuel
// de l'aspect (rangées mêlées, bord dentelé, points imbriqués…), pas une simulation des points.
// Motif déterministe (aucun aléa), repeint seulement quand le style ou les couleurs changent.
Canvas {
    id: preview
    property int styleIndex: 0
    property color first: Theme.raised
    property color last: Theme.foreground
    enabled: false
    Accessible.ignored: true
    renderTarget: Canvas.Image
    onStyleIndexChanged: requestPaint()
    onFirstChanged: requestPaint()
    onLastChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    // Seuil quasi aléatoire mais reproductible (suite de Weyl au nombre d'or).
    function weyl(i) { return (i * 0.6180339887) % 1 }

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        const w = width, h = height
        if (w <= 0 || h <= 0)
            return
        const a = String(first), b = String(last)
        ctx.lineWidth = 1.4
        ctx.lineCap = "round"
        const stroke = (x0, y0, x1, y1, colour) => {
            ctx.beginPath()
            ctx.moveTo(x0, y0)
            ctx.lineTo(x1, y1)
            ctx.strokeStyle = colour
            ctx.stroke()
        }
        let i = 0
        switch (styleIndex) {
        case 0: // lisse : chaque rangée verticale va au fil dont la part l'emporte
            for (let x = 1; x < w; x += 2.5, ++i)
                stroke(x, 2, x, h - 2, weyl(i) < x / w ? b : a)
            break
        case 1: // dentelé : limite en dents de scie
            for (let y = 1.5; y < h; y += 2.5, ++i) {
                const edge = w * (0.5 + 0.18 * ((i % 4) - 1.5) / 1.5)
                stroke(2, y, edge, y, a)
                stroke(edge, y, w - 2, y, b)
            }
            break
        case 2: // peint : points de longueurs variées qui s'imbriquent
            for (let y = 1.5; y < h; y += 2.5, ++i) {
                const edge = w * (0.25 + 0.5 * weyl(i))
                stroke(2, y, edge, y, a)
                stroke(edge + 1.5, y, w - 2, y, b)
            }
            break
        case 3: // moucheté : petits points dispersés
            ctx.fillStyle = a
            ctx.fillRect(0, 0, w, h)
            ctx.fillStyle = b
            for (let k = 0; k < 140; ++k) {
                const x = weyl(k) * w, y = weyl(k * 7 + 3) * h
                if (weyl(k * 13 + 1) < x / w)
                    ctx.fillRect(x, y, 2, 2)
            }
            break
        case 4: // hachuré : deux couches croisées
            for (let x = -h; x < w; x += 4) {
                stroke(x, h, x + h, 0, a)
                stroke(x, 0, x + h, h, b)
            }
            break
        default: // satin : colonnes dont les fils alternent
            for (let x = 2; x < w - 2; x += 2, ++i)
                stroke(x, 3, x + 2, h - 3, weyl(i) < x / w ? b : a)
            break
        }
    }
}
