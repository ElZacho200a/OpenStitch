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

    readonly property string themeState: Theme.stateOf(enabled, down, checked, hovered)

    indicator: ThemedSurface {
        implicitWidth: 36
        implicitHeight: 20
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        themeStyle: Theme.style("switch.track", "default", control.themeState)
        focusStyle: Theme.style("switch.track", "default", "focus")
        focused: control.visualFocus
        focusBorder: Theme.foreground
        radius: height / 2
        fill: control.checked ? Theme.accent : Theme.raised
        borderWidth: Theme.borderWidth
        border: control.checked ? Theme.accent : Theme.muted
        opacity: control.enabled ? (control.down ? 0.8 : 1) : 0.5
        Behavior on opacity { NumberAnimation { duration: Theme.animationMs } }
        ThemedSurface {
            readonly property var thumbStyle: Theme.style("switch.thumb", "default", control.themeState)
            x: control.checked ? parent.width - width - 3 : 3
            anchors.verticalCenter: parent.verticalCenter
            width: 14
            height: 14
            themeStyle: thumbStyle
            radius: 7
            fill: control.checked ? Theme.accentInk : Theme.foreground
            scale: thumbStyle.scale !== undefined && !Theme.reducedMotion ? thumbStyle.scale : 1
            Behavior on x { NumberAnimation { duration: Theme.animationMs; easing.type: Theme.easingEmphasis } }
            Behavior on scale { NumberAnimation { duration: Theme.feedbackFastMs } }
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
