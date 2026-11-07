// SPDX-License-Identifier: Apache-2.0
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import "../.."

// Outil Recadrer : cadre en pixels de l'image de travail, huit poignées + déplacement du cadre
// (MouseArea ; jamais l'attaché Drag). « Recadrer » pousse UNE opération annulable (crop).
Item {
    id: crop
    objectName: "cropOverlay"
    required property var space // ImageSpaceController
    property int viewTick: 0
    visible: space && space.cropActive

    readonly property point topLeft: {
        crop.viewTick
        crop.space ? crop.space.imageToScreen(crop.space.cropX, crop.space.cropY) : Qt.point(0, 0)
    }
    readonly property point bottomRight: {
        crop.viewTick
        crop.space ? crop.space.imageToScreen(crop.space.cropX + crop.space.cropW,
                                              crop.space.cropY + crop.space.cropH)
                   : Qt.point(0, 0)
    }

    function moveEdges(sx, sy, left, right, top, bottom) {
        var p = space.screenToImage(sx, sy)
        var x0 = space.cropX, y0 = space.cropY
        var x1 = space.cropX + space.cropW, y1 = space.cropY + space.cropH
        if (left) x0 = Math.min(p.x, x1 - 1)
        if (right) x1 = Math.max(p.x, x0 + 1)
        if (top) y0 = Math.min(p.y, y1 - 1)
        if (bottom) y1 = Math.max(p.y, y0 + 1)
        space.setCrop(x0, y0, x1 - x0, y1 - y0)
    }

    Rectangle {
        id: frame
        objectName: "cropFrame"
        x: crop.topLeft.x
        y: crop.topLeft.y
        width: crop.bottomRight.x - crop.topLeft.x
        height: crop.bottomRight.y - crop.topLeft.y
        color: "transparent"
        border.color: Theme.accent
        border.width: 2

        MouseArea {
            id: body
            anchors.fill: parent
            property point grab
            cursorShape: Qt.SizeAllCursor
            onPressed: function(mouse) {
                var p = mapToItem(crop, mouse.x, mouse.y)
                var ip = crop.space.screenToImage(p.x, p.y)
                grab = Qt.point(ip.x - crop.space.cropX, ip.y - crop.space.cropY)
            }
            onPositionChanged: function(mouse) {
                if (!pressed) return
                var p = mapToItem(crop, mouse.x, mouse.y)
                var ip = crop.space.screenToImage(p.x, p.y)
                crop.space.setCrop(ip.x - grab.x, ip.y - grab.y, crop.space.cropW, crop.space.cropH)
            }
        }
    }

    Repeater {
        model: [
            { name: "nw", fx: 0, fy: 0, l: true, r: false, t: true, b: false },
            { name: "n", fx: 0.5, fy: 0, l: false, r: false, t: true, b: false },
            { name: "ne", fx: 1, fy: 0, l: false, r: true, t: true, b: false },
            { name: "e", fx: 1, fy: 0.5, l: false, r: true, t: false, b: false },
            { name: "se", fx: 1, fy: 1, l: false, r: true, t: false, b: true },
            { name: "s", fx: 0.5, fy: 1, l: false, r: false, t: false, b: true },
            { name: "sw", fx: 0, fy: 1, l: true, r: false, t: false, b: true },
            { name: "w", fx: 0, fy: 0.5, l: true, r: false, t: false, b: false }
        ]
        delegate: Rectangle {
            id: handle
            required property var modelData
            objectName: "cropHandle_" + modelData.name
            width: 14
            height: 14
            radius: 3
            color: Theme.accent
            border.color: Theme.accentInk
            x: frame.x + modelData.fx * frame.width - width / 2
            y: frame.y + modelData.fy * frame.height - height / 2
            MouseArea {
                anchors.fill: parent
                anchors.margins: -6
                onPositionChanged: function(mouse) {
                    if (!pressed) return
                    var p = mapToItem(crop, mouse.x, mouse.y)
                    crop.moveEdges(p.x, p.y, handle.modelData.l, handle.modelData.r,
                                   handle.modelData.t, handle.modelData.b)
                }
            }
        }
    }

    Rectangle {
        id: bar
        x: Math.max(8, Math.min(crop.width - width - 8, frame.x + frame.width / 2 - width / 2))
        y: Math.min(crop.height - height - 8, frame.y + frame.height + 12)
        width: barRow.implicitWidth + 16
        height: Theme.controlHeight + 12
        radius: Theme.radius
        color: Theme.panel
        border.color: Theme.border
        RowLayout {
            id: barRow
            anchors.centerIn: parent
            spacing: 6
            StudioLabel {
                objectName: "cropSizeLabel"
                text: crop.space ? qsTr("%1 × %2 px").arg(crop.space.cropW).arg(crop.space.cropH) : ""
                tone: "foreground"
                wrapMode: Text.NoWrap
            }
            StudioButton {
                objectName: "cropApply"
                text: qsTr("Recadrer")
                emphasized: true
                onClicked: crop.space.applyCrop()
            }
            StudioButton {
                objectName: "cropCancel"
                text: qsTr("Annuler")
                quiet: true
                onClicked: crop.space.cancelCrop()
            }
        }
    }
}
