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

    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: 4
        radius: 2
        color: Theme.hover
        border.width: 1
        border.color: Theme.outline
        opacity: control.enabled ? 1 : 0.5
        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: 2
            color: Theme.accent
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        implicitWidth: control.pressed ? 20 : control.hovered ? 18 : 16
        implicitHeight: implicitWidth
        radius: width / 2
        Behavior on implicitWidth { NumberAnimation { duration: Theme.feedbackFastMs } }
        color: control.pressed ? Theme.accentPressed : Theme.accent
        border.width: control.visualFocus ? 3 : 1
        border.color: control.visualFocus ? Theme.foreground : Theme.accentPressed
        opacity: control.enabled ? 1 : 0.5
        Behavior on color { ColorAnimation { duration: Theme.animationMs } }
        Rectangle {
            objectName: "sliderValueBubble"
            visible: control.pressed
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.top
            anchors.bottomMargin: 7
            width: sliderValue.implicitWidth + 12
            height: sliderValue.implicitHeight + 6
            radius: 4
            color: Theme.surface
            border.color: Theme.accent
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
