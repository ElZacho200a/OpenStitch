// SPDX-License-Identifier: Apache-2.0
// Spin box themed by Theme: dark field, light text, +/- buttons with visible states.
import QtQuick
import QtQuick.Controls as Controls
import "."

Controls.SpinBox {
    id: control
    property bool invalid: false

    implicitHeight: Math.max(28, Math.round(Theme.controlHeight * 0.75))
    implicitWidth: 110
    hoverEnabled: true
    // La molette change la valeur, comme les champs numériques de V1, mais seulement une fois
    // le champ focalisé (clic ou Tab) : sinon elle vole le défilement du panneau qui le
    // contient (QA-STUDIO-001 LAY-023).
    wheelEnabled: control.activeFocus
    focusPolicy: Qt.StrongFocus
    // Journal d'usage local : jamais de texte saisi, seulement l'identifiant du contrôle.
    Connections {
        target: control
        function onValueModified() {
            const id = control.objectName.length > 0 ? control.objectName : ""
            if (id.length > 0 && typeof usage !== "undefined" && usage)
                usage.track("control", id, String(control.value))
        }
    }
    font.family: Theme.fontFamily
    font.pixelSize: Theme.smallSize
    leftPadding: 4
    rightPadding: 4
    Accessible.role: Accessible.SpinBox

    contentItem: TextInput {
        text: control.textFromValue(control.value, control.locale)
        font: control.font
        color: !control.enabled ? Theme.faint : control.invalid ? Theme.error : Theme.foreground
        selectionColor: Theme.accent
        selectedTextColor: Theme.accentInk
        horizontalAlignment: Qt.AlignHCenter
        verticalAlignment: Qt.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: Qt.ImhFormattedNumbersOnly
    }

    up.indicator: Rectangle {
        x: control.mirrored ? 0 : parent.width - width
        height: parent.height
        implicitWidth: 24
        implicitHeight: 24
        radius: 5
        color: control.up.pressed ? Theme.hover : control.up.hovered ? Theme.hover : Theme.raised
        border.width: 1
        border.color: Theme.outline
        opacity: control.enabled ? 1 : 0.5
        Rectangle { anchors.centerIn: parent; width: 10; height: 2; color: control.up.indicator.enabled ? Theme.foreground : Theme.faint }
        Rectangle { anchors.centerIn: parent; width: 2; height: 10; color: control.up.indicator.enabled ? Theme.foreground : Theme.faint }
    }

    down.indicator: Rectangle {
        x: control.mirrored ? parent.width - width : 0
        height: parent.height
        implicitWidth: 24
        implicitHeight: 24
        radius: 5
        color: control.down.pressed ? Theme.hover : control.down.hovered ? Theme.hover : Theme.raised
        border.width: 1
        border.color: Theme.outline
        opacity: control.enabled ? 1 : 0.5
        Rectangle { anchors.centerIn: parent; width: 10; height: 2; color: control.down.indicator.enabled ? Theme.foreground : Theme.faint }
    }

    background: Rectangle {
        implicitWidth: 110
        radius: 5
        color: control.enabled ? Theme.surface : Theme.background
        border.width: control.activeFocus ? 2 : 1
        border.color: control.invalid ? Theme.error
                     : control.activeFocus ? Theme.accent
                     : control.hovered ? Theme.muted
                     : Theme.outline
    }
}
