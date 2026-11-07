// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."

// Contextual inspector driven by the engine's parameter table (param_spec). No business logic
// here: the controller lists the fields, validates and applies each edit as one undo step.
Item {
    id: root
    objectName: "inspector"
    // Absent in hosts that do not provide it (the shell tests of the other families).
    readonly property var ctl: typeof inspector !== "undefined" ? inspector : null
    property var layoutModel: []
    readonly property bool active: ctl !== null && ctl.count > 0

    function refreshLayout() {
        if (ctl === null)
            return;
        const next = ctl.sections.map(s => ({ title: s.title, keys: s.fields.map(f => f.key) }));
        if (JSON.stringify(next) !== JSON.stringify(layoutModel))
            layoutModel = next;
    }
    Connections {
        target: root.ctl
        function onChanged() { root.refreshLayout(); }
    }
    Component.onCompleted: refreshLayout()

    implicitHeight: column.implicitHeight
    visible: active

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.implicitHeight + 24
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Controls.ScrollBar.vertical: Controls.ScrollBar { }

        ColumnLayout {
            id: column
            width: flick.width - 36
            x: 18
            y: 12
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                SectionLabel { text: qsTr("RÉGLAGES DE POINTS") }
                Item { Layout.fillWidth: true }
                Text {
                    objectName: "inspectorCount"
                    text: root.ctl ? qsTr("%1 objet(s) · %2").arg(root.ctl.count).arg(root.ctl.typeLabel) : ""
                    color: Theme.faint
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.captionSize
                }
            }

            // Conversion of the stitch type (applies to the whole selection).
            Controls.ComboBox {
                id: typeBox
                objectName: "typeBox"
                Layout.fillWidth: true
                model: root.ctl ? root.ctl.types : []
                textRole: "label"
                currentIndex: {
                    if (!root.ctl || root.ctl.typeMixed)
                        return -1;
                    const t = root.ctl.types;
                    for (let i = 0; i < t.length; ++i)
                        if (t[i].current)
                            return i;
                    return -1;
                }
                displayText: root.ctl && root.ctl.typeMixed ? qsTr("Types différents")
                             : currentIndex >= 0 ? currentText : (root.ctl ? root.ctl.typeLabel : "")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.bodySize
                Accessible.name: qsTr("Type de points")
                onActivated: index => root.ctl.setStitchType(root.ctl.types[index].id)
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Controls.TextField {
                    id: search
                    objectName: "paramSearch"
                    Layout.fillWidth: true
                    placeholderText: qsTr("Chercher un réglage…")
                    text: root.ctl ? root.ctl.query : ""
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallSize
                    selectByMouse: true
                    Accessible.name: qsTr("Chercher un réglage")
                    background: Rectangle {
                        radius: 3
                        color: Theme.raised
                        border.width: 1
                        border.color: search.activeFocus ? Theme.accent : Theme.border
                    }
                    onTextChanged: if (root.ctl && root.ctl.query !== text) root.ctl.query = text
                }
                StudioButton {
                    objectName: "resetAll"
                    iconName: "undo"
                    tooltip: qsTr("Réinitialiser tous les réglages de ce type")
                    quiet: true
                    implicitWidth: 28
                    implicitHeight: 28
                    onClicked: root.ctl.resetAll()
                }
            }

            Controls.Switch {
                objectName: "advancedSwitch"
                text: qsTr("Réglages avancés") + (root.ctl && root.ctl.hiddenAdvancedCount > 0
                                                  ? " (" + root.ctl.hiddenAdvancedCount + ")" : "")
                checked: root.ctl ? root.ctl.showAdvanced : false
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
                palette.windowText: Theme.foreground
                onToggled: root.ctl.showAdvanced = checked
            }

            Controls.Switch {
                objectName: "expertSwitch"
                text: qsTr("Niveau expert") + (root.ctl && root.ctl.hiddenExpertCount > 0
                                               ? " (" + root.ctl.hiddenExpertCount + ")" : "")
                checked: root.ctl ? root.ctl.showExpert : false
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
                palette.windowText: Theme.foreground
                onToggled: root.ctl.showExpert = checked
            }

            Text {
                Layout.fillWidth: true
                visible: root.ctl && root.ctl.typeMixed
                text: qsTr("La sélection mélange plusieurs types de points : choisissez un type pour les convertir, puis réglez-les ensemble.")
                color: Theme.faint
                wrapMode: Text.WordWrap
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
            }
            Text {
                Layout.fillWidth: true
                objectName: "noParams"
                visible: root.ctl && !root.ctl.typeMixed && root.ctl.totalFieldCount === 0
                text: qsTr("Ce type de points n'expose pas encore de réglages dans l'inspecteur.")
                color: Theme.faint
                wrapMode: Text.WordWrap
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
            }

            Repeater {
                model: root.layoutModel
                delegate: ParamSection {
                    required property var modelData
                    Layout.fillWidth: true
                    ctl: root.ctl
                    title: modelData.title
                    keys: modelData.keys
                }
            }

            Text {
                Layout.fillWidth: true
                objectName: "inspectorMessage"
                visible: text.length > 0
                text: root.ctl ? root.ctl.message : ""
                color: root.ctl && root.ctl.messageOk ? Theme.success : Theme.error
                wrapMode: Text.WordWrap
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.divider }

            // Presets: named setting sets stored in the project (undoable).
            RowLayout {
                Layout.fillWidth: true
                SectionLabel { text: qsTr("PRÉRÉGLAGES") }
                Item { Layout.fillWidth: true }
                Controls.CheckBox {
                    objectName: "allPresets"
                    text: qsTr("Tous les types")
                    checked: root.ctl ? root.ctl.allPresets : false
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.captionSize
                    palette.windowText: Theme.muted
                    onToggled: root.ctl.allPresets = checked
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Controls.TextField {
                    id: presetName
                    objectName: "presetName"
                    Layout.fillWidth: true
                    placeholderText: qsTr("Nom du préréglage")
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallSize
                    selectByMouse: true
                    background: Rectangle {
                        radius: 3
                        color: Theme.raised
                        border.width: 1
                        border.color: presetName.activeFocus ? Theme.accent : Theme.border
                    }
                }
                StudioButton {
                    objectName: "savePreset"
                    iconName: "plus"
                    tooltip: qsTr("Enregistrer les réglages de la sélection")
                    quiet: true
                    implicitWidth: 28
                    implicitHeight: 28
                    onClicked: {
                        if (root.ctl.savePreset(presetName.text) === "")
                            presetName.text = "";
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                visible: presetList.count === 0
                objectName: "noPreset"
                text: qsTr("(aucun préréglage)")
                color: Theme.faint
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
            }
            Repeater {
                id: presetList
                model: root.ctl ? root.ctl.presets : []
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 6
                    objectName: "preset_" + modelData.index
                    Text {
                        Layout.fillWidth: true
                        text: modelData.name + (modelData.matches ? "" : "  · " + modelData.typeLabel)
                        elide: Text.ElideRight
                        color: modelData.matches ? Theme.foreground : Theme.faint
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.smallSize
                    }
                    Controls.Button {
                        objectName: "applyPreset_" + modelData.index
                        text: qsTr("Appliquer")
                        font.pixelSize: Theme.captionSize
                        onClicked: root.ctl.applyPreset(modelData.index)
                    }
                    Controls.Button {
                        objectName: "renamePreset_" + modelData.index
                        text: qsTr("Renommer")
                        font.pixelSize: Theme.captionSize
                        Controls.ToolTip.visible: hovered
                        Controls.ToolTip.text: qsTr("Renomme avec le nom saisi ci-dessus")
                        onClicked: {
                            if (root.ctl.renamePreset(modelData.index, presetName.text) === "")
                                presetName.text = "";
                        }
                    }
                    StudioButton {
                        objectName: "removePreset_" + modelData.index
                        iconName: "close"
                        tooltip: qsTr("Supprimer le préréglage")
                        quiet: true
                        implicitWidth: 24
                        implicitHeight: 24
                        onClicked: root.ctl.removePreset(modelData.index)
                    }
                }
            }
        }
    }
}
