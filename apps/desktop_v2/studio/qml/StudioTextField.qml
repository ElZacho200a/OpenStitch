// SPDX-License-Identifier: Apache-2.0
// Text entry themed by Theme: states normal / hover / focus ring / disabled / error.
import QtQuick
import QtQuick.Controls as Controls
import "."

Controls.TextField {
    id: control
    property bool invalid: false
    // Molette (comme les champs numériques de V1) : active si wheelStep > 0 et que le texte est un
    // nombre. Ctrl = pas x10, Maj = pas /10. Le champ est validé (editingFinished) 300 ms après la
    // dernière cran : une seule validation pour une rafale de crans.
    property real wheelStep: 0
    property real wheelMin: -1e9
    property real wheelMax: 1e9
    property int wheelDecimals: 1

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

    background: ThemedSurface {
        themeStyle: Theme.style("input", "default", Theme.stateOf(control.enabled, false, false, control.hovered))
        focusStyle: Theme.style("input", "default", "focus")
        focused: control.activeFocus
        focusBorder: Theme.accent
        alert: control.invalid
        radius: 5
        fill: control.enabled ? Theme.raised : Theme.surface
        borderWidth: Theme.borderWidth
        border: control.hovered && control.enabled ? Theme.muted : Theme.outline
        opacity: control.enabled ? 1 : 0.7
    }

    Timer {
        id: wheelCommit
        interval: 300
        onTriggered: control.editingFinished()
    }
    WheelHandler {
        objectName: "wheelStepper"
        blocking: true // la molette sur un champ numérique ne fait JAMAIS défiler le panneau derrière
        enabled: control.wheelStep > 0 && control.enabled
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: (event) => {
            const current = parseFloat(control.text.replace(",", "."))
            if (isNaN(current) || event.angleDelta.y === 0)
                return
            let factor = 1
            if (event.modifiers & Qt.ControlModifier)
                factor = 10
            else if (event.modifiers & Qt.ShiftModifier)
                factor = 0.1
            const direction = event.angleDelta.y > 0 ? 1 : -1
            const next = Math.min(control.wheelMax, Math.max(control.wheelMin, current + direction * control.wheelStep * factor))
            control.text = next.toFixed(control.wheelDecimals)
            wheelCommit.restart()
        }
    }
}
