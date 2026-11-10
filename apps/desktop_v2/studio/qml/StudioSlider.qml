// SPDX-License-Identifier: Apache-2.0
// Slider themed by Theme: groove with accent fill, handle with focus ring.
import QtQuick
import QtQuick.Controls as Controls
import "."

Controls.Slider {
    id: control
    implicitHeight: 24
    implicitWidth: 120
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Slider

    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: 4
        radius: 2
        color: Theme.hover
        border.width: 1
        border.color: Theme.outline
        opacity: control.enabled ? 1 : 0.5
        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: 2
            color: Theme.accent
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        implicitWidth: control.pressed ? 20 : control.hovered ? 18 : 16
        implicitHeight: implicitWidth
        radius: width / 2
        Behavior on implicitWidth { NumberAnimation { duration: Theme.feedbackFastMs } }
        color: control.pressed ? Theme.accentPressed : Theme.accent
        border.width: control.visualFocus ? 3 : 1
        border.color: control.visualFocus ? Theme.foreground : Theme.accentPressed
        opacity: control.enabled ? 1 : 0.5
        Behavior on color { ColorAnimation { duration: Theme.animationMs } }
    }
}
