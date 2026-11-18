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
    readonly property var themeStyle: Theme.style("tooltip", "default", "normal")
    contentItem: Text {
        text: tip.text
        color: tip.themeStyle.text !== undefined ? tip.themeStyle.text : Theme.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.smallSize
        wrapMode: Text.WordWrap
    }
    background: ThemedSurface {
        themeStyle: tip.themeStyle
        fill: Theme.raised
        radius: 4
        borderWidth: 1
        border: Theme.border
        animated: false
    }
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.feedbackFastMs }
    }
}
