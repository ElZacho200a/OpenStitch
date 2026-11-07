// SPDX-License-Identifier: Apache-2.0
import QtQuick
import "."

// Surcouche du canevas : rectangle de sélection, boîte et poignées de la sélection, fantôme du
// glisser, repères d'accrochage. Ne reçoit aucun événement : le canevas gère les gestes.
// Un nombre FIXE d'éléments (jamais un par point) ; `canvas` est le GpuCanvas.
Item {
    id: root
    property var canvas
    readonly property rect box: canvas ? canvas.selectionRect : Qt.rect(0, 0, 0, 0)
    readonly property bool hasBox: box.width > 0 || box.height > 0
    enabled: false

    Rectangle {
        objectName: "marqueeRect"
        visible: canvas && canvas.marquee.width > 0
        x: canvas ? canvas.marquee.x : 0
        y: canvas ? canvas.marquee.y : 0
        width: canvas ? canvas.marquee.width : 0
        height: canvas ? canvas.marquee.height : 0
        color: "#22f08cb7"
        border.color: Theme.accent
        border.width: 1
    }

    Rectangle {
        objectName: "selectionBoxRect"
        visible: root.hasBox
        x: root.box.x
        y: root.box.y
        width: root.box.width
        height: root.box.height
        color: "transparent"
        border.color: Theme.accent
        border.width: 1
        opacity: 0.8
    }

    Rectangle {
        objectName: "ghostRect"
        visible: canvas && canvas.ghostRect.width > 0
        x: canvas ? canvas.ghostRect.x : 0
        y: canvas ? canvas.ghostRect.y : 0
        width: canvas ? canvas.ghostRect.width : 0
        height: canvas ? canvas.ghostRect.height : 0
        color: "#18f08cb7"
        border.color: Theme.accent
        border.width: 1
    }

    Repeater {
        model: canvas ? canvas.guideLines : []
        Rectangle {
            required property var modelData
            x: Math.min(modelData.x1, modelData.x2)
            y: Math.min(modelData.y1, modelData.y2)
            width: Math.max(1, Math.abs(modelData.x2 - modelData.x1))
            height: Math.max(1, Math.abs(modelData.y2 - modelData.y1))
            color: "#5ad2ff"
        }
    }

    // 8 poignées (NO N NE E SE S SO O) puis la poignée de rotation, au-dessus de la boîte.
    Repeater {
        model: root.hasBox ? 9 : 0
        Rectangle {
            required property int index
            readonly property real cx: root.box.x + root.box.width / 2
            readonly property real cy: root.box.y + root.box.height / 2
            readonly property var pts: [
                Qt.point(root.box.x, root.box.y), Qt.point(cx, root.box.y),
                Qt.point(root.box.x + root.box.width, root.box.y),
                Qt.point(root.box.x + root.box.width, cy),
                Qt.point(root.box.x + root.box.width, root.box.y + root.box.height),
                Qt.point(cx, root.box.y + root.box.height),
                Qt.point(root.box.x, root.box.y + root.box.height),
                Qt.point(root.box.x, cy), Qt.point(cx, root.box.y - 26)]
            objectName: "handle" + index
            width: 9
            height: 9
            radius: index === 8 ? 5 : 1
            x: pts[index].x - 4.5
            y: pts[index].y - 4.5
            color: index === 8 ? Theme.accent : Theme.foreground
            border.color: Theme.accent
            border.width: 1
        }
    }

    Rectangle {
        visible: canvas && canvas.gestureInfo.length > 0
        x: canvas ? Math.min(canvas.width - width - 4, Math.max(4, canvas.ghostRect.x + 6)) : 0
        y: canvas ? Math.max(4, canvas.ghostRect.y - height - 4) : 0
        width: infoText.implicitWidth + 12
        height: infoText.implicitHeight + 6
        radius: 3
        color: Theme.surface
        border.color: Theme.border
        Text {
            id: infoText
            anchors.centerIn: parent
            text: canvas ? canvas.gestureInfo : ""
            color: Theme.foreground
            font.pixelSize: Theme.smallSize
        }
    }
}
