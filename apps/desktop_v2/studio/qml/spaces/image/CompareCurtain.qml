// SPDX-License-Identifier: Apache-2.0
import QtQuick
import "../.."

// Rideau avant / après sur le canevas : « avant » à gauche de la ligne, « après » à droite. La
// ligne se saisit par sa poignée (MouseArea ; jamais l'attaché Drag). Seul le rectangle de
// l'image est découpé par la couche du canevas : ici on ne fait que positionner la poignée.
Item {
    id: curtain
    objectName: "compareCurtain"
    required property var space  // ImageSpaceController
    property int viewTick: 0     // incrémenté à chaque pan/zoom du canevas
    visible: space && space.hasImage && space.compareMode === 2

    readonly property real lineX: {
        curtain.viewTick
        if (!space || !space.hasImage) return 0
        return space.imageToScreen(space.curtain * space.imageWidthPx, 0).x
    }

    Rectangle {
        x: curtain.lineX - 1
        width: 2
        height: parent.height
        color: Theme.accent
    }
    Rectangle {
        id: grip
        objectName: "curtainGrip"
        x: curtain.lineX - width / 2
        y: curtain.height / 2 - height / 2
        width: 28
        height: 56
        radius: Theme.radius
        color: Theme.accent
        StudioLabel {
            anchors.centerIn: parent
            text: "⇔"
            tone: "foreground"
            color: Theme.accentInk
            wrapMode: Text.NoWrap
        }
        MouseArea {
            anchors.fill: parent
            anchors.margins: -6
            cursorShape: Qt.SizeHorCursor
            onPositionChanged: function(mouse) {
                if (!pressed) return
                var p = mapToItem(curtain, mouse.x, mouse.y)
                var ip = curtain.space.screenToImage(p.x, p.y)
                curtain.space.curtain = ip.x / curtain.space.imageWidthPx
            }
        }
    }
    StudioLabel {
        x: curtain.lineX - width - 8
        y: 8
        text: qsTr("Avant")
        tone: "foreground"
    }
    StudioLabel {
        x: curtain.lineX + 8
        y: 8
        text: qsTr("Après")
        tone: "foreground"
    }
}
