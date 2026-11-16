// SPDX-License-Identifier: Apache-2.0
// Carte flottante : surface arrondie détachée des bords, posée au-dessus du plan de travail.
// L'ombre est faite de rectangles translucides empilés : elle fonctionne aussi en rendu logiciel
// (pas de ShaderEffect), et ne coûte que trois quads.
import QtQuick
import "."

Item {
    id: card
    default property alias content: body.data
    property int radius: Theme.cardRadius
    property color color: Theme.panel
    property color borderColor: Theme.divider
    property bool elevated: true

    Repeater {
        model: card.elevated ? 3 : 0
        Rectangle {
            required property int index
            readonly property int spread: (index + 1) * 3
            x: -spread
            y: -spread + Theme.shadowOffset
            width: card.width + 2 * spread
            height: card.height + 2 * spread
            radius: card.radius + spread
            color: Theme.shadow
            opacity: Theme.shadowOpacity / (index + 1)
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: card.radius
        color: card.color
        border.width: 1
        border.color: card.borderColor
    }

    Item {
        id: body
        anchors.fill: parent
    }
}
