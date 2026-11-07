// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import "."

Controls.AbstractButton {
    id: button
    property string iconName: ""
    property string tooltip: ""
    property string shortcutLabel: ""
    property bool emphasized: false
    property bool quiet: false
    property bool active: false
    property bool showShortcutLabel: true
    property color contentColor: !enabled ? Theme.faint
                                       : emphasized ? Theme.accentInk
                                       : checked || active ? Theme.accent
                                       : Theme.foreground
    implicitWidth: Math.max(Theme.controlHeight, content.implicitWidth + (text.length > 0 ? 22 : 16))
    implicitHeight: Theme.controlHeight
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: tooltip.length > 0 ? tooltip : text
    Accessible.description: shortcutLabel.length > 0 ? qsTr("Raccourci : ") + shortcutLabel : ""

    background: Rectangle {
        radius: Theme.radius
        color: button.emphasized ? (button.down ? Theme.accentPressed : Theme.accent)
               : button.checked || button.active ? Theme.accentSoft
               : button.down ? Theme.raised
               : button.hovered ? Theme.hover
               : button.quiet ? "transparent" : Theme.raised
        border.width: button.activeFocus || (!button.quiet && !button.emphasized) ? 1 : 0
        border.color: button.activeFocus ? Theme.accent : Theme.border
        opacity: button.enabled ? 1 : 0.55
        Behavior on color { ColorAnimation { duration: Theme.animationMs; easing.type: Easing.OutCubic } }
    }

    contentItem: Item {
        implicitWidth: content.implicitWidth
        implicitHeight: content.implicitHeight
        Row {
            id: content
            spacing: 8
            anchors.centerIn: parent
            StudioIcon {
                name: button.iconName
                color: button.contentColor
                visible: name.length > 0
                anchors.verticalCenter: parent.verticalCenter
                width: 18
                height: 18
            }
            Text {
                text: button.text
                visible: text.length > 0
                color: button.contentColor
                font.family: Theme.fontFamily
                font.pixelSize: Theme.bodySize
                font.weight: button.emphasized ? Font.DemiBold : Font.Normal
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: button.shortcutLabel
                visible: button.showShortcutLabel && text.length > 0 && button.text.length > 0
                color: button.emphasized ? Theme.accentInk : Theme.faint
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    Controls.ToolTip.visible: hovered && tooltip.length > 0
    Controls.ToolTip.text: tooltip + (shortcutLabel.length > 0 ? "  ·  " + shortcutLabel : "")
    Controls.ToolTip.delay: 600
}
