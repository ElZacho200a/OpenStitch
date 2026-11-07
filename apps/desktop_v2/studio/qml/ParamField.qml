// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."

// One engine setting. Everything displayed (value, range, default, mixed state) comes from the
// controller; edits go back through it and are validated there (a refusal is shown, never
// silently corrected).
ColumnLayout {
    id: root
    required property var ctl
    required property string fieldKey
    readonly property var f: { ctl.revision; return ctl.field(fieldKey); }
    readonly property bool mixed: f.mixed === true
    property string error: ""
    objectName: "param_" + fieldKey
    spacing: 3

    function commit(result) { root.error = result; }

    RowLayout {
        Layout.fillWidth: true
        Text {
            Layout.fillWidth: true
            text: root.f.label + (root.f.unit ? "  (" + root.f.unit + ")" : "")
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallSize
            elide: Text.ElideRight
        }
        Text {
            visible: root.mixed
            text: qsTr("Valeurs mixtes")
            color: Theme.accent
            font.family: Theme.fontFamily
            font.pixelSize: Theme.captionSize
        }
        StudioButton {
            objectName: "reset_" + root.fieldKey
            visible: root.f.isDefault === false
            iconName: "undo"
            tooltip: qsTr("Réinitialiser (défaut : %1)").arg(root.f.defaultText)
            quiet: true
            implicitWidth: 22
            implicitHeight: 22
            onClicked: root.commit(root.ctl.resetParam(root.fieldKey))
        }
    }

    // Numbers and integers: slider for dragging + exact typed entry.
    RowLayout {
        Layout.fillWidth: true
        visible: root.f.kind === "number" || root.f.kind === "integer"
        spacing: 8
        Controls.Slider {
            id: slider
            objectName: "slider_" + root.fieldKey
            Layout.fillWidth: true
            from: root.f.min
            to: root.f.max
            stepSize: root.f.kind === "integer" ? 1 : root.f.step
            snapMode: Controls.Slider.SnapAlways
            opacity: root.mixed ? 0.45 : 1
            value: root.f.value
            Accessible.name: root.f.label
            onMoved: root.commit(root.ctl.setParam(root.fieldKey, value))
        }
        Controls.TextField {
            id: entry
            objectName: "entry_" + root.fieldKey
            Layout.preferredWidth: 64
            text: root.f.text
            horizontalAlignment: TextInput.AlignRight
            color: root.error.length > 0 ? "#ff8a8a" : Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallSize
            selectByMouse: true
            Accessible.name: root.f.label
            background: Rectangle {
                radius: 3
                color: Theme.raised
                border.width: 1
                border.color: root.error.length > 0 ? "#ff8a8a" : entry.activeFocus ? Theme.accent : Theme.border
            }
            onEditingFinished: {
                const result = root.ctl.setParamText(root.fieldKey, text);
                root.commit(result);
                if (result === "")
                    text = Qt.binding(() => root.f.text);
            }
        }
    }

    Controls.Switch {
        id: toggle
        objectName: "switch_" + root.fieldKey
        visible: root.f.kind === "bool"
        checked: root.f.value >= 0.5 && !root.mixed
        text: root.mixed ? qsTr("Mixte") : (checked ? qsTr("Oui") : qsTr("Non"))
        font.family: Theme.fontFamily
        font.pixelSize: Theme.smallSize
        palette.windowText: Theme.foreground
        Accessible.name: root.f.label
        onToggled: root.commit(root.ctl.setParam(root.fieldKey, checked ? 1 : 0))
    }

    Controls.ComboBox {
        id: combo
        objectName: "combo_" + root.fieldKey
        Layout.fillWidth: true
        visible: root.f.kind === "choice"
        model: root.f.choices
        textRole: "label"
        currentIndex: root.mixed ? -1 : Math.round(root.f.value)
        displayText: root.mixed ? "—" : currentText
        font.family: Theme.fontFamily
        font.pixelSize: Theme.smallSize
        Accessible.name: root.f.label
        onActivated: index => root.commit(root.ctl.setParam(root.fieldKey, index))
    }

    Text {
        Layout.fillWidth: true
        visible: root.error.length > 0
        objectName: "error_" + root.fieldKey
        text: root.error
        color: "#ff8a8a"
        wrapMode: Text.WordWrap
        font.family: Theme.fontFamily
        font.pixelSize: Theme.captionSize
    }
}
