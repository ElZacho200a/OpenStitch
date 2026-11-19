// SPDX-License-Identifier: Apache-2.0
// Surface habillée par le moteur de thèmes (docs/desktop-v2/22-moteur-de-themes.md) : ombre
// douce, aplat ou dégradé, reflet brillant, liseré de relief, texture et bordure.
//
// Les propriétés posées par le composant (fill, border, radius…) sont son style intégré, celui
// de Neo Studio ; `themeStyle` (Theme.style(composant, variante, état)) les surcharge propriété
// par propriété. L'anneau de focus clavier passe par `focused` / `focusStyle` et l'emporte
// toujours sur la bordure d'état : un thème ne peut pas masquer le focus.
//
// Uniquement des Rectangle / Image : pas de ShaderEffect ni de MultiEffect, donc identique en
// rendu logiciel (tests, --software) et GPU, et quelques quads seulement par contrôle. Une
// couche absente (ombre 0, reflet 0, texture vide) n'instancie rien.
import QtQuick
import "."

Item {
    id: surface
    property var themeStyle: ({})
    property var focusStyle: ({})
    property bool focused: false
    // Couleur de focus par défaut du composant (une case cochée remplie d'accent prend le texte).
    property color focusBorder: Theme.focus
    // Erreur de saisie : la bordure d'alerte l'emporte sur tout (focus compris).
    property bool alert: false
    property color alertBorder: Theme.error
    property bool animated: true
    // Faux : aucune ombre, même si le thème en demande une (carte posée à plat).
    property bool shadowEnabled: true

    property color fill: "transparent"
    property color border: "transparent"
    property real borderWidth: 0
    property real radius: 0
    property int shadowLayers: 0
    property real shadowOpacity: Theme.shadowOpacity
    property real shadowOffset: Theme.shadowOffset
    property real shadowSpread: Theme.shadowSpread
    property color shadowColor: Theme.shadow
    property real gloss: 0
    property real glossHeight: 0.5
    property real innerHighlight: 0
    property string texture: ""
    property real textureOpacity: 0

    function pick(key, base) {
        const value = themeStyle ? themeStyle[key] : undefined
        return value === undefined ? base : value
    }
    function pickFocus(key, base) {
        const value = focusStyle ? focusStyle[key] : undefined
        return value === undefined ? base : value
    }
    readonly property color resolvedFill: pick("fill", fill)
    readonly property color resolvedFillEnd: pick("fillEnd", resolvedFill)
    readonly property bool horizontal: pick("gradient", "vertical") === "horizontal"
    readonly property color resolvedBorder: alert ? alertBorder
        : focused ? pickFocus("border", focusBorder) : pick("border", border)
    readonly property real resolvedBorderWidth: focused
        ? Math.max(Theme.focusWidth, pickFocus("borderWidth", Theme.focusWidth))
        : pick("borderWidth", borderWidth)
    readonly property real resolvedRadius: Math.min(pick("radius", radius), Math.min(width, height) / 2)
    readonly property int resolvedShadowLayers: shadowEnabled ? Math.round(pick("shadowLayers", shadowLayers)) : 0
    readonly property real resolvedShadowOpacity: pick("shadowOpacity", shadowOpacity)
    readonly property real resolvedShadowOffset: pick("shadowOffset", shadowOffset)
    readonly property real resolvedShadowSpread: pick("shadowSpread", shadowSpread)
    readonly property color resolvedShadowColor: pick("shadowColor", shadowColor)
    readonly property real resolvedGloss: pick("gloss", gloss)
    readonly property real resolvedGlossHeight: pick("glossHeight", glossHeight)
    readonly property real resolvedInnerHighlight: pick("innerHighlight", innerHighlight)
    readonly property string resolvedTexture: pick("texture", texture)
    readonly property real resolvedTextureOpacity: pick("textureOpacity", textureOpacity)
    readonly property int colorMs: animated ? Theme.animationMs : 0

    // Ombre douce : rectangles translucides empilés, de plus en plus larges et pâles.
    Repeater {
        model: surface.resolvedShadowLayers
        Rectangle {
            required property int index
            readonly property real spread: (index + 1) * surface.resolvedShadowSpread
            x: -spread
            y: -spread + surface.resolvedShadowOffset
            width: surface.width + 2 * spread
            height: surface.height + 2 * spread
            radius: surface.resolvedRadius + spread
            color: surface.resolvedShadowColor
            opacity: surface.resolvedShadowOpacity / (index + 1)
        }
    }

    Rectangle {
        id: body
        anchors.fill: parent
        radius: surface.resolvedRadius
        border.width: surface.resolvedBorderWidth
        border.color: surface.resolvedBorder
        // Toujours un dégradé à deux arrêts : identiques, c'est un aplat.
        gradient: Gradient {
            orientation: surface.horizontal ? Gradient.Horizontal : Gradient.Vertical
            GradientStop {
                position: 0
                color: surface.resolvedFill
                Behavior on color { ColorAnimation { duration: surface.colorMs; easing.type: Theme.easingType } }
            }
            GradientStop {
                position: 1
                color: surface.resolvedFillEnd
                Behavior on color { ColorAnimation { duration: surface.colorMs; easing.type: Theme.easingType } }
            }
        }
        Behavior on border.color { ColorAnimation { duration: surface.animated ? Theme.feedbackFastMs : 0 } }
        Behavior on border.width { NumberAnimation { duration: surface.animated ? Theme.feedbackFastMs : 0 } }
    }

    // Texture en mosaïque, en retrait des coins arrondis (pas de masque sans shader).
    Loader {
        active: surface.resolvedTexture.length > 0 && surface.resolvedTextureOpacity > 0
        anchors.fill: parent
        anchors.margins: Math.max(surface.resolvedBorderWidth, surface.resolvedRadius * 0.3)
        sourceComponent: Image {
            source: surface.resolvedTexture
            fillMode: Image.Tile
            opacity: surface.resolvedTextureOpacity
            asynchronous: true
            smooth: true
        }
    }

    // Reflet « plastique brillant » : bande claire sur le haut, fondue vers le bas.
    Loader {
        active: surface.resolvedGloss > 0.001
        x: inset
        y: inset
        width: parent.width - 2 * inset
        height: Math.max(0, (parent.height - 2 * inset) * surface.resolvedGlossHeight)
        readonly property real inset: Math.max(1, surface.resolvedBorderWidth)
        sourceComponent: Rectangle {
            radius: Math.max(0, surface.resolvedRadius - Math.max(1, surface.resolvedBorderWidth))
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.rgba(Theme.gloss.r, Theme.gloss.g, Theme.gloss.b, surface.resolvedGloss) }
                GradientStop { position: 1; color: Qt.rgba(Theme.gloss.r, Theme.gloss.g, Theme.gloss.b, 0) }
            }
        }
    }

    // Liseré intérieur clair : relief « bombé ».
    Loader {
        active: surface.resolvedInnerHighlight > 0.001
        anchors.fill: parent
        anchors.margins: surface.resolvedBorderWidth
        sourceComponent: Rectangle {
            color: "transparent"
            radius: Math.max(0, surface.resolvedRadius - surface.resolvedBorderWidth)
            border.width: 1
            border.color: Qt.rgba(Theme.gloss.r, Theme.gloss.g, Theme.gloss.b, surface.resolvedInnerHighlight * 0.85)
        }
    }
}
