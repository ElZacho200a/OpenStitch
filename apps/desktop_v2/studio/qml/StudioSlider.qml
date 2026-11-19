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
    // La molette règle la valeur par pas (stepSize), mais seulement une fois le curseur
    // focalisé (clic ou Tab) : sinon elle vole le défilement du panneau qui le contient
    // (QA-STUDIO-001 LAY-023).
    wheelEnabled: control.activeFocus
    focusPolicy: Qt.StrongFocus
    function logValue() {
        if (control.objectName.length > 0 && typeof usage !== "undefined" && usage)
            usage.track("control", control.objectName, String(control.value))
    }
    // Journal d'usage local : valeur finale seulement (relâchement, molette ou clavier).
    Connections {
        target: control
        function onMoved() {
            if (!control.pressed)
                control.logValue()
        }
        function onPressedChanged() {
            if (!control.pressed)
                control.logValue()
        }
    }
    Accessible.role: Accessible.Slider

    readonly property string themeState: Theme.stateOf(enabled, pressed, false, hovered)
    readonly property var trackStyle: Theme.style("slider.track", "default", themeState)
    readonly property var handleStyle: Theme.style("slider.handle", "default", themeState)

    background: ThemedSurface {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: control.trackStyle.height !== undefined ? control.trackStyle.height : 4
        themeStyle: control.trackStyle
        radius: 2
        fill: Theme.hover
        borderWidth: Theme.borderWidth
        border: Theme.outline
        opacity: control.enabled ? 1 : 0.5
        ThemedSurface {
            width: Math.max(height, control.visualPosition * parent.width)
            visible: control.visualPosition > 0
            height: parent.height
            themeStyle: Theme.style("slider.fill", "default", control.themeState)
            radius: 2
            fill: Theme.accent
            animated: false
        }
    }

    handle: ThemedSurface {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        implicitWidth: 16
        implicitHeight: implicitWidth
        // Retour au survol / à l'appui : agrandissement (échelle du thème, 18/20 px par défaut).
        scale: Theme.reducedMotion ? 1
               : control.handleStyle.scale !== undefined ? control.handleStyle.scale
               : control.pressed ? 1.25 : control.hovered ? 1.125 : 1
        Behavior on scale { NumberAnimation { duration: Theme.feedbackFastMs; easing.type: Theme.easingEmphasis } }
        themeStyle: control.handleStyle
        focusStyle: Theme.style("slider.handle", "default", "focus")
        focused: control.visualFocus
        focusBorder: Theme.foreground
        radius: width / 2
        fill: control.pressed ? Theme.accentPressed : Theme.accent
        borderWidth: Theme.borderWidth
        border: Theme.accentPressed
        opacity: control.enabled ? 1 : 0.5
        Rectangle {
            objectName: "sliderValueBubble"
            visible: control.pressed
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.top
            anchors.bottomMargin: 7
            width: sliderValue.implicitWidth + 12
            height: sliderValue.implicitHeight + 6
            radius: Theme.radius / 2
            color: Theme.surface
            border.color: Theme.accent
            // Le texte de la bulle ne suit pas l'agrandissement de la poignée.
            scale: 1 / parent.scale
            Text {
                id: sliderValue
                anchors.centerIn: parent
                text: Number(control.value).toLocaleString(Qt.locale(), 'f',
                                                             control.stepSize >= 1 ? 0 : 2)
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
            }
        }
    }
}
