// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import OpenStitch.Studio 1.0
import "."

// Universal command palette (Ctrl+K) over the engine's action catalog. Every catalog action is
// listed; actions with fields open a form generated from the catalog's own field definitions, so
// a new engine action appears here without any QML change.
Controls.Popup {
    id: palette
    objectName: "actionPalette"
    modal: true
    focus: true
    closePolicy: Controls.Popup.CloseOnEscape | Controls.Popup.CloseOnPressOutside
    parent: Controls.Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(560, parent ? parent.width - 32 : 560)
    height: Math.min(520, parent ? parent.height - 32 : 520)
    padding: 12

    property string currentId: ""        // "" = list mode, otherwise the form of that action
    property string currentLabel: ""
    property var currentFields: []
    property string filePickerKey: ""
    property string error: ""

    function openList() {
        currentId = ""
        error = ""
        open()
        search.text = ""
        results.model = actions.searchAll("") // fresh enabled states even if the text was empty
        search.forceActiveFocus()
    }

    function choose(entry) {
        if (!entry.enabled) {
            error = entry.label + (entry.reason ? " : " + entry.reason : "")
            return
        }
        if (entry.kind === "command") {
            // Same path as the buttons and shortcuts: the window's dispatcher runs it.
            close()
            const why = actions.runCommand(entry.id)
            if (why !== "") {
                open()
                error = why
            }
            return
        }
        if (!entry.hasFields) {
            const refusal = actions.run(entry.id, {})
            if (refusal === "")
                close()
            else
                error = refusal
            return
        }
        currentId = entry.id
        currentLabel = entry.label
        currentFields = actions.fields(entry.id)
        const values = {}
        for (let i = 0; i < currentFields.length; ++i)
            values[currentFields[i].key] = currentFields[i].value
        formModel = values
        error = ""
    }

    property var formModel: ({})

    function setValue(key, value) {
        const next = Object.assign({}, formModel)
        next[key] = value
        formModel = next
    }

    function submit() {
        const refusal = actions.run(currentId, formModel)
        if (refusal === "")
            close()
        else
            error = refusal
    }

    background: Rectangle {
        color: Theme.panel
        border.color: Theme.border
        radius: Theme.radius
    }

    FileDialog {
        id: fileDialog
        onAccepted: palette.setValue(palette.filePickerKey, selectedFile.toString().replace(/^file:\/\/(\/[A-Za-z]:)/, "$1").replace(/^file:\/\//, ""))
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        // ---- List mode ----
        StudioTextField {
            id: search
            objectName: "actionSearch"
            visible: palette.currentId === ""
            Layout.fillWidth: true
            placeholderText: qsTr("Rechercher une action (Ctrl+K)")
            Accessible.name: qsTr("Rechercher une action")
            onTextChanged: results.model = actions.searchAll(text)
            onAccepted: {
                if (results.count > 0)
                    palette.choose(results.model[0])
            }
            Keys.onDownPressed: results.forceActiveFocus()
        }

        ListView {
            id: results
            objectName: "actionResults"
            visible: palette.currentId === ""
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: actions.searchAll("")
            section.property: "group"
            section.delegate: Text {
                text: section
                color: Theme.faint
                font.family: Theme.fontFamily
                font.pixelSize: Theme.captionSize
                topPadding: 6
            }
            delegate: Rectangle {
                required property var modelData
                width: ListView.view.width
                height: 30
                radius: 3
                color: hit.containsMouse ? Theme.hover : "transparent"
                opacity: modelData.enabled ? 1 : 0.45
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    text: modelData.label + (!modelData.enabled && modelData.reason ? "  · " + modelData.reason : "")
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.bodySize
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    text: modelData.shortcut || ""
                    color: Theme.faint
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallSize
                }
                MouseArea {
                    id: hit
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: palette.choose(modelData)
                }
                Accessible.role: Accessible.Button
                Accessible.name: modelData.label + (modelData.enabled ? "" : qsTr(" (indisponible : ") + modelData.reason + ")")
                Accessible.description: modelData.shortcut || ""
            }
        }

        // ---- Form mode ----
        Text {
            visible: palette.currentId !== ""
            text: palette.currentLabel
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.headingSize
        }

        Flickable {
            visible: palette.currentId !== ""
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: form.implicitHeight
            ColumnLayout {
                id: form
                width: parent.width
                spacing: 8
                Repeater {
                    model: palette.currentFields
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Text {
                            Layout.preferredWidth: 190
                            text: modelData.label + (modelData.unit ? " (" + modelData.unit + ")" : "")
                            color: Theme.muted
                            wrapMode: Text.WordWrap
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.smallSize
                        }
                        Loader {
                            Layout.fillWidth: true
                            sourceComponent: modelData.kind === "bool" ? boolField
                                : modelData.kind === "choice" ? choiceField
                                : modelData.kind === "file" ? fileField : textField
                            property var field: modelData
                        }
                    }
                }
            }
        }

        Text {
            objectName: "actionError"
            visible: palette.error !== ""
            Layout.fillWidth: true
            text: palette.error
            color: Theme.accent
            wrapMode: Text.WordWrap
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallSize
            Accessible.role: Accessible.AlertMessage
            Accessible.name: text
        }

        RowLayout {
            visible: palette.currentId !== ""
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            StudioButton {
                text: qsTr("Retour")
                quiet: true
                onClicked: palette.openList()
            }
            StudioButton {
                objectName: "actionApply"
                text: qsTr("Appliquer")
                emphasized: true
                onClicked: palette.submit()
            }
        }
    }

    Component {
        id: textField
        StudioTextField {
            text: palette.formModel[field.key] !== undefined ? palette.formModel[field.key] : ""
            inputMethodHints: field.kind === "number" || field.kind === "integer"
                              ? Qt.ImhFormattedNumbersOnly : Qt.ImhNone
            placeholderText: (field.kind === "number" || field.kind === "integer")
                             ? field.min + " … " + field.max : ""
            Accessible.name: field.label
            onTextEdited: palette.setValue(field.key, text)
        }
    }
    Component {
        id: boolField
        StudioCheckBox {
            checked: palette.formModel[field.key] === "1"
            Accessible.name: field.label
            onToggled: palette.setValue(field.key, checked ? "1" : "0")
        }
    }
    Component {
        id: choiceField
        StudioComboBox {
            model: field.choices
            textRole: "label"
            valueRole: "value"
            Accessible.name: field.label
            Component.onCompleted: {
                const index = indexOfValue(palette.formModel[field.key])
                if (index >= 0)
                    currentIndex = index
            }
            onActivated: palette.setValue(field.key, currentValue)
        }
    }
    Component {
        id: fileField
        RowLayout {
            StudioTextField {
                Layout.fillWidth: true
                text: palette.formModel[field.key] !== undefined ? palette.formModel[field.key] : ""
                Accessible.name: field.label
                onTextEdited: palette.setValue(field.key, text)
            }
            StudioButton {
                text: qsTr("Parcourir…")
                onClicked: {
                    palette.filePickerKey = field.key
                    fileDialog.open()
                }
            }
        }
    }
}
