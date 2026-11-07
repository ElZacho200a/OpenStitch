// SPDX-License-Identifier: Apache-2.0
// Check box themed by Theme: the label is always readable on the panel background.
import QtQuick
import QtQuick.Controls as Controls
import "."

Controls.CheckBox {
    id: control
    property bool invalid: false

    implicitHeight: Math.max(24, indicator.implicitHeight, contentItem.implicitHeight)
    spacing: 8
    padding: 2
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    font.family: Theme.fontFamily
    font.pixelSize: Theme.smallSize
    Accessible.role: Accessible.CheckBox
    Accessible.name: text

    indicator: Rectangle {
        implicitWidth: 18
        implicitHeight: 18
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: 4
        color: control.checkState === Qt.Unchecked ? (control.hovered ? Theme.hover : Theme.raised) : Theme.accent
        border.width: control.visualFocus ? 2 : 1
        border.color: control.invalid ? Theme.error
                     : control.visualFocus ? Theme.foreground
                     : control.checkState === Qt.Unchecked ? Theme.muted : Theme.accent
        opacity: control.enabled ? 1 : 0.5
        // Tick drawn with two rotated bars (no glyph dependency, no shader).
        Item {
            anchors.centerIn: parent
            visible: control.checkState === Qt.Checked
            width: 10
            height: 10
            Rectangle { x: 0; y: 5; width: 5; height: 2; radius: 1; rotation: 45; color: Theme.accentInk }
            Rectangle { x: 2; y: 4; width: 9; height: 2; radius: 1; rotation: -50; color: Theme.accentInk }
        }
        Rectangle {
            anchors.centerIn: parent
            visible: control.checkState === Qt.PartiallyChecked
            width: 10
            height: 2
            color: Theme.accentInk
        }
    }

    contentItem: Text {
        leftPadding: control.indicator.width + control.spacing
        text: control.text
        font: control.font
        color: !control.enabled ? Theme.faint : control.invalid ? Theme.error : Theme.foreground
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
