// SPDX-License-Identifier: Apache-2.0
// Text entry themed by Theme: states normal / hover / focus ring / disabled / error.
import QtQuick
import QtQuick.Controls as Controls
import "."

Controls.TextField {
    id: control
    property bool invalid: false

    implicitHeight: Math.max(28, Math.round(Theme.controlHeight * 0.75))
    implicitWidth: 120
    leftPadding: 8
    rightPadding: 8
    topPadding: 4
    bottomPadding: 4
    hoverEnabled: true
    selectByMouse: true
    color: !enabled ? Theme.faint : invalid ? Theme.error : Theme.foreground
    placeholderTextColor: Theme.faint
    selectionColor: Theme.accent
    selectedTextColor: Theme.accentInk
    font.family: Theme.fontFamily
    font.pixelSize: Theme.smallSize
    verticalAlignment: TextInput.AlignVCenter
    Accessible.role: Accessible.EditableText
    Accessible.name: placeholderText

    background: Rectangle {
        radius: 5
        color: control.enabled ? Theme.raised : Theme.surface
        border.width: control.activeFocus ? 2 : 1
        border.color: control.invalid ? Theme.error
                     : control.activeFocus ? Theme.accent
                     : control.hovered && control.enabled ? Theme.muted
                     : Theme.outline
        opacity: control.enabled ? 1 : 0.7
    }
}
