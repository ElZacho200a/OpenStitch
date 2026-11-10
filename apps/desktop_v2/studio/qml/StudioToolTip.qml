// SPDX-License-Identifier: Apache-2.0
// Tooltip child of a hoverable control. Also expose help to keyboard users.
import QtQuick
import QtQuick.Controls as Controls
import "."

Controls.ToolTip {
    id: tip
    visible: parent && (parent.hovered === true || parent.activeFocus === true) && text.length > 0
    delay: parent && parent.activeFocus === true ? 0 : 600
    padding: 6
    contentItem: Text {
        text: tip.text
        color: Theme.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.smallSize
        wrapMode: Text.WordWrap
    }
    background: Rectangle {
        color: Theme.raised
        radius: 4
        border.width: 1
        border.color: Theme.border
    }
}
