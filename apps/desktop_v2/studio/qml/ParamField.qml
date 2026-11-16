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

    property bool justApplied: false
    readonly property bool customized: root.f.isDefault === false
    function commit(result) {
        root.error = result;
        if (result === "") {
            justApplied = true;
            appliedTimer.restart();
        } else {
            justApplied = false;
            appliedTimer.stop();
        }
    }
    Timer { id: appliedTimer; interval: 650; onTriggered: root.justApplied = false }

    RowLayout {
        Layout.fillWidth: true
        Text {
            Layout.fillWidth: true
            text: root.f.label + (root.f.unit ? "  (" + root.f.unit + ")" : "")
            color: root.justApplied ? Theme.success : root.customized ? Theme.accent : Theme.muted
            Behavior on color { ColorAnimation { duration: Theme.feedbackSlowMs } }
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
            active: root.f.isDefault === false
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
        visible: root.f.kind === "number" || root.f.kind === "integer" ||
                 (root.f.kind === "optional" && root.f.isSet === true)
        spacing: 8
        StudioSlider {
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
        StudioTextField {
            id: entry
            objectName: "entry_" + root.fieldKey
            Layout.preferredWidth: 64
            wheelStep: root.f.kind === "integer" ? 1 : (root.f.step > 0 ? root.f.step : 0)
            wheelMin: root.f.min
            wheelMax: root.f.max
            wheelDecimals: root.f.kind === "integer" ? 0 : Math.max(0, Math.min(4, Math.ceil(-Math.log(root.f.step > 0 ? root.f.step : 1) / Math.LN10 - 1e-9)))
            text: root.f.text
            horizontalAlignment: TextInput.AlignRight
            color: root.error.length > 0 ? Theme.error : Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallSize
            selectByMouse: true
            Accessible.name: root.f.label
            background: Rectangle {
                radius: 3
                color: Theme.raised
                border.width: 1
                border.color: root.error.length > 0 ? Theme.error : entry.activeFocus ? Theme.accent : Theme.border
            }
            onEditingFinished: {
                const result = root.ctl.setParamText(root.fieldKey, text);
                root.commit(result);
                if (result === "")
                    text = Qt.binding(() => root.f.text);
            }
        }
    }

    // Optional number (sweep angle): a switch « automatic / fixed » above the usual slider.
    RowLayout {
        Layout.fillWidth: true
        visible: root.f.kind === "optional"
        StudioSwitch {
            objectName: "auto_" + root.fieldKey
            checked: root.f.isSet === true
            text: checked ? qsTr("Fixé") : qsTr("Automatique")
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallSize
            palette.windowText: Theme.foreground
            onToggled: root.commit(checked ? root.ctl.setParam(root.fieldKey, root.f.value)
                                           : root.ctl.clearParam(root.fieldKey))
        }
    }

    // Optional point [x, y] in mm (entry / exit / centre): two exact entries and a clear button.
    RowLayout {
        Layout.fillWidth: true
        visible: root.f.kind === "point"
        spacing: 6
        Text {
            text: "X"
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallSize
        }
        StudioTextField {
            id: pointX
            wheelStep: 1
            wheelMin: -1000
            wheelMax: 1000
            objectName: "pointx_" + root.fieldKey
            Layout.fillWidth: true
            text: root.f.isSet ? Number(root.f.pointX).toString() : ""
            placeholderText: qsTr("auto")
            horizontalAlignment: TextInput.AlignRight
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallSize
            selectByMouse: true
            Accessible.name: root.f.label + " X"
            onEditingFinished: pointApply()
        }
        Text {
            text: "Y"
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallSize
        }
        StudioTextField {
            id: pointY
            wheelStep: 1
            wheelMin: -1000
            wheelMax: 1000
            objectName: "pointy_" + root.fieldKey
            Layout.fillWidth: true
            text: root.f.isSet ? Number(root.f.pointY).toString() : ""
            placeholderText: qsTr("auto")
            horizontalAlignment: TextInput.AlignRight
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallSize
            selectByMouse: true
            Accessible.name: root.f.label + " Y"
            onEditingFinished: pointApply()
        }
        StudioButton {
            objectName: "clear_" + root.fieldKey
            visible: root.f.isSet === true
            iconName: "undo"
            tooltip: qsTr("Effacer le point (automatique)")
            quiet: true
            implicitWidth: 22
            implicitHeight: 22
            onClicked: root.commit(root.ctl.clearParam(root.fieldKey))
        }
    }

    // Collections edited on the canvas (guides, break lines, rungs, gradient): count + clear.
    RowLayout {
        Layout.fillWidth: true
        visible: root.f.kind === "list"
        Text {
            Layout.fillWidth: true
            objectName: "count_" + root.fieldKey
            text: root.f.max === 1 ? (root.f.isSet ? qsTr("Défini") : qsTr("Aucun"))
                                   : qsTr("%1 élément(s)").arg(Math.round(root.f.value))
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallSize
        }
        StudioButton {
            objectName: "clear_" + root.fieldKey
            visible: root.f.isSet === true
            iconName: "undo"
            tooltip: qsTr("Tout effacer")
            quiet: true
            implicitWidth: 22
            implicitHeight: 22
            onClicked: root.commit(root.ctl.clearParam(root.fieldKey))
        }
    }

    function pointApply() {
        const x = Number(pointX.text.replace(",", "."));
        const y = Number(pointY.text.replace(",", "."));
        if (pointX.text.trim() === "" || pointY.text.trim() === "" || isNaN(x) || isNaN(y)) {
            if (pointX.text.trim() !== "" || pointY.text.trim() !== "")
                root.commit(qsTr("Indiquez X et Y en millimètres."));
            return;
        }
        root.commit(root.ctl.setPoint(root.fieldKey, x, y));
    }

    StudioSwitch {
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

    StudioComboBox {
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
        color: Theme.error
        wrapMode: Text.WordWrap
        font.family: Theme.fontFamily
        font.pixelSize: Theme.captionSize
    }
}
