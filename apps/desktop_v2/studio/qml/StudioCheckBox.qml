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
    // Journal d'usage local : jamais de texte saisi, seulement l'identifiant du contrôle.
    Connections {
        target: control
        function onToggled() {
            const id = control.objectName.length > 0 ? control.objectName : control.text
            if (id.length > 0 && typeof usage !== "undefined" && usage)
                usage.track("control", id, control.checked ? "on" : "off")
        }
    }
    font.family: Theme.fontFamily
    font.pixelSize: Theme.smallSize
    Accessible.role: Accessible.CheckBox
    Accessible.name: text

    readonly property string themeState: Theme.stateOf(enabled, down, checkState !== Qt.Unchecked, hovered)
    readonly property var themeStyle: Theme.style("checkbox", "default", themeState)
    readonly property color tickColor: themeStyle.icon !== undefined ? themeStyle.icon : Theme.accentInk

    indicator: ThemedSurface {
        implicitWidth: 18
        implicitHeight: 18
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        themeStyle: control.themeStyle
        focusStyle: Theme.style("checkbox", "default", "focus")
        focused: control.visualFocus
        focusBorder: Theme.foreground
        alert: control.invalid
        radius: 4
        fill: control.checkState === Qt.Unchecked ? (control.hovered ? Theme.hover : Theme.raised) : Theme.accent
        borderWidth: Theme.borderWidth
        border: control.checkState === Qt.Unchecked ? Theme.muted : Theme.accent
        opacity: control.themeStyle.opacity !== undefined ? control.themeStyle.opacity
                 : control.enabled ? (control.down ? 0.8 : 1) : 0.5
        Behavior on opacity { NumberAnimation { duration: Theme.animationMs } }
        // Tick drawn with two rotated bars (no glyph dependency, no shader).
        Item {
            anchors.centerIn: parent
            visible: control.checkState === Qt.Checked
            width: 10
            height: 10
            Rectangle { x: 0; y: 5; width: 5; height: 2; radius: 1; rotation: 45; color: control.tickColor }
            Rectangle { x: 2; y: 4; width: 9; height: 2; radius: 1; rotation: -50; color: control.tickColor }
        }
        Rectangle {
            anchors.centerIn: parent
            visible: control.checkState === Qt.PartiallyChecked
            width: 10
            height: 2
            color: control.tickColor
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
