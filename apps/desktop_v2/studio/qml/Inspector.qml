// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "."

// Contextual inspector driven by the engine's parameter table (param_spec). No business logic
// here: the controller lists the fields, validates and applies each edit as one undo step.
Item {
    id: root
    objectName: "inspector"
    // Absent in hosts that do not provide it (the shell tests of the other families).
    readonly property var ctl: typeof inspector !== "undefined" ? inspector : null
    readonly property var fadeCtl: typeof fade !== "undefined" ? fade : null
    property var layoutModel: []
    // Transitional virtual entry: persisted gradient objects will replace this adapter.
    property bool gradientEditorRequested: false
    readonly property bool gradientSelected: fadeCtl !== null && fadeCtl.hasFade
    readonly property var stitchTypeChoices: {
        const choices = ctl ? ctl.types.slice(0) : []
        choices.push({ id: "multicolor_gradient", label: qsTr("Fondu multicolore"),
                       current: gradientSelected })
        return choices
    }
    // Folded sections by title; kept here so the state survives selection changes.
    property var collapsedSections: ({})
    readonly property bool searching: ctl !== null && ctl.query.trim().length > 0
    readonly property bool active: ctl !== null && ctl.count > 0

    function setAllCollapsed(value) {
        const next = {};
        for (let i = 0; i < layoutModel.length; ++i)
            next[layoutModel[i].title] = value;
        collapsedSections = next;
    }
    function toggleSection(title) {
        const next = Object.assign({}, collapsedSections);
        next[title] = !next[title];
        collapsedSections = next;
    }

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
        objectName: "inspectorFlick"
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
            StudioComboBox {
                id: typeBox
                objectName: "typeBox"
                Layout.fillWidth: true
                model: root.stitchTypeChoices
                textRole: "label"
                currentIndex: {
                    if (!root.ctl || root.ctl.typeMixed)
                        return -1;
                    const t = root.stitchTypeChoices;
                    for (let i = 0; i < t.length; ++i)
                        if (t[i].current)
                            return i;
                    return -1;
                }
                displayText: root.gradientEditorRequested || root.gradientSelected
                             ? qsTr("Fondu multicolore")
                             : root.ctl && root.ctl.typeMixed ? qsTr("Types différents")
                             : currentIndex >= 0 ? currentText : (root.ctl ? root.ctl.typeLabel : "")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.bodySize
                Accessible.name: qsTr("Type de points")
                onActivated: index => {
                    const type = root.stitchTypeChoices[index].id
                    if (type === "multicolor_gradient") {
                        root.gradientEditorRequested = true
                        flick.contentY = Math.max(0, fadeInspector.y - 24)
                    } else {
                        root.gradientEditorRequested = false
                        root.ctl.setStitchType(type)
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                StudioTextField {
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

            // Couleur de toute la sélection (types de points différents compris).
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Rectangle {
                    objectName: "inspectorColourSwatch"
                    implicitWidth: 24
                    implicitHeight: 24
                    radius: 5
                    border.width: 1
                    border.color: Theme.outline
                    color: root.ctl && root.ctl.colour !== "" ? root.ctl.colour : Theme.raised
                    Text {
                        anchors.centerIn: parent
                        visible: root.ctl && root.ctl.colourMixed
                        text: "≠"
                        color: Theme.foreground
                        font.pixelSize: Theme.smallSize
                    }
                }
                Text {
                    objectName: "inspectorColourLabel"
                    Layout.fillWidth: true
                    text: !root.ctl ? "" : root.ctl.colourMixed ? qsTr("Couleurs différentes") : root.ctl.colour
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallSize
                    elide: Text.ElideRight
                }
                StudioButton {
                    objectName: "inspectorColourButton"
                    text: qsTr("Couleur…")
                    tooltip: qsTr("Changer la couleur de la sélection")
                    onClicked: {
                        if (root.ctl.colour !== "")
                            colourDialog.selectedColor = root.ctl.colour
                        colourDialog.open()
                    }
                }
            }
            ColorDialog {
                id: colourDialog
                objectName: "inspectorColourDialog"
                title: qsTr("Couleur de la sélection")
                onAccepted: root.ctl.setColour(selectedColor.toString())
            }

            // Le fondu de FILS est une propriété de l'objet logique, pas un
            // type de point ni un "dégradé" de densité directionnelle.
            // Même éditeur que dans le tiroir Points, sans nested scrolling.
            Text {
                objectName: "inspectorGradientModeHelp"
                Layout.fillWidth: true
                visible: root.gradientEditorRequested && !root.gradientSelected
                text: qsTr("Configurez les fils ci-dessous puis créez le fondu. "
                           + "Le type de points sous-jacent reste inchangé avant la création.")
                color: Theme.faint
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
                wrapMode: Text.WordWrap
            }
            FadeProperties {
                id: fadeInspector
                objectName: "inspectorFadeSection"
                controller: root.fadeCtl
                prefix: "inspectorFade"
                embedded: true
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                // Keep the editor visible for a single selected object even when
                // the fade service rejects it. FadeProperties already displays
                // fadeCtl.incompatibility and disables unsupported actions;
                // hiding the entire section made supported techniques look absent.
                visible: root.fadeCtl !== null && root.ctl !== null && root.ctl.count === 1
            }
            Text {
                objectName: "inspectorFadeDistinction"
                Layout.fillWidth: true
                visible: fadeInspector.visible
                text: qsTr("Les paramètres de densité et d'orientation des points restent dans les réglages du remplissage ci-dessous.")
                color: Theme.faint
                font.family: Theme.fontFamily
                font.pixelSize: Theme.captionSize
                wrapMode: Text.WordWrap
            }

            StudioSwitch {
                objectName: "advancedSwitch"
                text: qsTr("Réglages avancés") + (root.ctl && root.ctl.hiddenAdvancedCount > 0
                                                  ? " (" + root.ctl.hiddenAdvancedCount + ")" : "")
                checked: root.ctl ? root.ctl.showAdvanced : false
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
                palette.windowText: Theme.foreground
                onToggled: root.ctl.showAdvanced = checked
            }

            StudioSwitch {
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
                text: qsTr("Types différents : seuls les réglages communs à tous les types sont affichés ; ils s'appliquent à chaque objet. Choisissez un type pour les convertir et accéder à tous leurs réglages.")
                color: Theme.faint
                wrapMode: Text.WordWrap
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
            }
            Text {
                Layout.fillWidth: true
                objectName: "noParams"
                visible: root.ctl && root.ctl.totalFieldCount === 0
                text: root.ctl && root.ctl.typeMixed
                      ? qsTr("Ces types de points n'ont aucun réglage en commun : convertissez-les en un même type.")
                      : qsTr("Ce type de points n'expose pas encore de réglages dans l'inspecteur.")
                color: Theme.faint
                wrapMode: Text.WordWrap
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
            }

            Text {
                objectName: "inspectorSearchEmpty"
                Layout.fillWidth: true
                visible: root.ctl && root.ctl.query.trim().length > 0
                         && root.layoutModel.length === 0 && root.ctl.totalFieldCount > 0
                text: qsTr("Aucun réglage ne correspond à cette recherche.")
                color: Theme.warning
                wrapMode: Text.WordWrap
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }

            RowLayout {
                Layout.fillWidth: true
                visible: root.layoutModel.length > 1
                spacing: 6
                StudioButton {
                    objectName: "collapseAllSections"
                    text: qsTr("Tout replier")
                    tooltip: qsTr("Replier toutes les sections de réglages")
                    onClicked: root.setAllCollapsed(true)
                }
                StudioButton {
                    objectName: "expandAllSections"
                    text: qsTr("Tout déplier")
                    tooltip: qsTr("Déplier toutes les sections de réglages")
                    onClicked: root.setAllCollapsed(false)
                }
                Item { Layout.fillWidth: true }
            }

            Repeater {
                model: root.layoutModel
                delegate: ParamSection {
                    required property var modelData
                    Layout.fillWidth: true
                    ctl: root.ctl
                    title: modelData.title
                    keys: modelData.keys
                    // A search always shows its matches.
                    collapsed: !root.searching && !!root.collapsedSections[modelData.title]
                    onToggleRequested: root.toggleSection(modelData.title)
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
                StudioCheckBox {
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
                StudioTextField {
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
                    StudioButton {
                        objectName: "applyPreset_" + modelData.index
                        text: qsTr("Appliquer")
                        onClicked: root.ctl.applyPreset(modelData.index)
                    }
                    StudioButton {
                        objectName: "renamePreset_" + modelData.index
                        text: qsTr("Renommer")
                        tooltip: qsTr("Renomme avec le nom saisi ci-dessus")
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
