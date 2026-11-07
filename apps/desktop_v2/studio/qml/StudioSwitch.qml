// SPDX-License-Identifier: Apache-2.0
// Switch themed by Theme (same behavior as Controls.Switch).
import QtQuick
import QtQuick.Controls as Controls
import "."

Controls.Switch {
    id: control
    implicitHeight: Math.max(24, indicator.implicitHeight)
    spacing: 8
    padding: 2
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    font.family: Theme.fontFamily
    font.pixelSize: Theme.smallSize
    Accessible.role: Accessible.CheckBox
    Accessible.name: text

    indicator: Rectangle {
        implicitWidth: 36
        implicitHeight: 20
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: height / 2
        color: control.checked ? Theme.accent : Theme.raised
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus ? Theme.foreground : control.checked ? Theme.accent : Theme.muted
        opacity: control.enabled ? 1 : 0.5
        Rectangle {
            x: control.checked ? parent.width - width - 3 : 3
            anchors.verticalCenter: parent.verticalCenter
            width: 14
            height: 14
            radius: 7
            color: control.checked ? Theme.accentInk : Theme.foreground
            Behavior on x { NumberAnimation { duration: Theme.animationMs } }
        }
    }

    contentItem: Text {
        leftPadding: control.indicator.width + control.spacing
        text: control.text
        font: control.font
        color: control.enabled ? Theme.foreground : Theme.faint
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
