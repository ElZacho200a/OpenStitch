// SPDX-License-Identifier: Apache-2.0
// Tooltip child of a hoverable control: `StudioToolTip { text: "…" }` shows on hover.
import QtQuick
import QtQuick.Controls as Controls
import "."

Controls.ToolTip {
    id: tip
    visible: parent && parent.hovered === true && text.length > 0
    delay: 600
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
