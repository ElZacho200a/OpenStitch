// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."

// Script Python sur la multi-sélection (SCRIPT-01) : un exemple à adapter, « Aperçu » liste les
// valeurs qui vont changer (rien n'est modifié), « Appliquer » les pose en UNE étape annulable.
// Adaptateur SANS logique métier au-dessus de ScriptController. Le panneau couvre une partie du
// canevas : il porte donc ses propres raccourcis de sélection (tout, même couleur), et la liste
// des objets reste à droite. Le pied (message, Aperçu, Appliquer) reste toujours visible ; le
// milieu défile et se place sur l'aperçu dès qu'il arrive.
Rectangle {
    id: panel
    objectName: "scriptPanel"
    property var controller
    readonly property bool ready: controller !== undefined && controller !== null
    readonly property int count: ready ? controller.selectionCount : 0
    readonly property var edit: typeof editor !== "undefined" ? editor : null
    color: Theme.panel
    border.color: Theme.divider
    radius: Theme.radius

    component Hint: Text {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: Theme.faint
        font.family: Theme.fontFamily
        font.pixelSize: Theme.smallSize
    }

    // Aperçu reçu : le milieu défile jusqu'aux changements (ou à la sortie du script).
    Connections {
        target: panel.ready ? panel.controller : null
        function onPreviewChanged() { Qt.callLater(panel.revealResults) }
    }
    function revealResults() {
        const target = results.y
        scroller.contentY = Math.max(0, Math.min(target, scroller.contentHeight - scroller.height))
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            SectionLabel { text: qsTr("SCRIPT SUR LA SÉLECTION"); Layout.fillWidth: true }
            Controls.BusyIndicator { implicitWidth: 18; implicitHeight: 18; running: panel.ready && panel.controller.busy; visible: running }
        }

        Flickable {
            id: scroller
            objectName: "scriptScroller"
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: content.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            Controls.ScrollBar.vertical: Controls.ScrollBar {}

            ColumnLayout {
                id: content
                width: scroller.width - 12
                spacing: 8

                Text {
                    objectName: "scriptSelectionCount"
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallSize
                    color: panel.count > 0 ? Theme.foreground : Theme.warning
                    text: panel.count === 1 ? qsTr("1 objet sélectionné")
                        : panel.count > 1 ? qsTr("%1 objets sélectionnés").arg(panel.count)
                        : qsTr("Sélectionnez d'abord des objets brodés : boutons ci-dessous, liste Objets à droite, ou canevas (Ctrl+clic).")
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: panel.edit !== null
                    StudioButton {
                        objectName: "scriptSelectAll"
                        text: qsTr("Tout sélectionner")
                        tooltip: qsTr("Sélectionne tous les objets brodés (Ctrl+A)")
                        onClicked: panel.edit.selectAll()
                    }
                    StudioButton {
                        objectName: "scriptSelectSameColor"
                        text: qsTr("Même couleur")
                        tooltip: qsTr("Ajoute les objets de la même couleur que la sélection")
                        enabled: panel.count > 0
                        disabledReason: qsTr("Sélectionnez d'abord un objet de la couleur voulue")
                        onClicked: panel.edit.selectSameColor()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Text { text: qsTr("Exemple"); color: Theme.muted; font.family: Theme.fontFamily; font.pixelSize: Theme.smallSize }
                    StudioComboBox {
                        id: examples
                        objectName: "scriptExamples"
                        Layout.fillWidth: true
                        model: panel.ready ? panel.controller.examples : []
                        textRole: "title"
                        currentIndex: panel.ready ? panel.controller.exampleIndex : 0
                        displayText: panel.ready && panel.controller.modified ? qsTr("%1 (modifié)").arg(currentText) : currentText
                        Accessible.name: qsTr("Exemple de script")
                        onActivated: function(index) { panel.controller.loadExample(index) }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: panel.ready && panel.controller.canRestore
                    Hint { text: qsTr("Votre script a été remplacé par l'exemple.") }
                    StudioButton {
                        objectName: "scriptRestore"
                        quiet: true
                        text: qsTr("Restaurer mon script")
                        onClicked: panel.controller.restoreSource()
                    }
                }

                // Éditeur : Tab insère quatre espaces ; Échap quitte l'éditeur (le clavier revient
                // au Studio).
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(150, Math.min(320, scroller.height - 150))
                    radius: Theme.radius
                    color: Theme.surface
                    border.color: codeEdit.activeFocus ? Theme.focus : Theme.border
                    Controls.ScrollView {
                        anchors.fill: parent
                        anchors.margins: 1
                        Controls.TextArea {
                            id: codeEdit
                            objectName: "scriptEditor"
                            text: panel.ready ? panel.controller.source : ""
                            onTextChanged: if (panel.ready && text !== panel.controller.source) panel.controller.source = text
                            font.family: "Consolas"
                            font.pixelSize: Theme.smallSize
                            color: Theme.foreground
                            selectionColor: Theme.accentSoft
                            selectedTextColor: Theme.foreground
                            wrapMode: TextEdit.NoWrap
                            selectByMouse: true
                            persistentSelection: true
                            background: null
                            Accessible.name: qsTr("Script Python")
                            Keys.onTabPressed: function(event) { codeEdit.insert(codeEdit.cursorPosition, "    "); event.accepted = true }
                        }
                    }
                }

                // Aperçu : « Remplissage Région 20 · Écart entre rangées (mm) : 0,4 → 0,45 », puis
                // la sortie du script (print).
                ColumnLayout {
                    id: results
                    objectName: "scriptResults"
                    Layout.fillWidth: true
                    spacing: 4
                    Repeater {
                        id: changeList
                        objectName: "scriptChanges"
                        model: panel.ready ? panel.controller.changes : []
                        delegate: Text {
                            required property var modelData
                            required property int index
                            objectName: "scriptChange" + index
                            Layout.fillWidth: true
                            visible: index < 50
                            wrapMode: Text.WordWrap
                            color: Theme.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.smallSize
                            text: qsTr("%1 · %2 : %3 → %4").arg(modelData.name).arg(modelData.label).arg(modelData.before).arg(modelData.after)
                        }
                    }
                    Hint {
                        visible: changeList.count > 50
                        text: qsTr("… et %1 autres changements").arg(changeList.count - 50)
                    }
                    Controls.TextArea {
                        id: output
                        objectName: "scriptOutput"
                        Layout.fillWidth: true
                        visible: panel.ready && panel.controller.output.length > 0
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.Wrap
                        text: panel.ready ? panel.controller.output : ""
                        font.family: "Consolas"
                        font.pixelSize: Theme.smallSize
                        color: Theme.muted
                        background: Rectangle { color: Theme.raised; radius: Theme.radius }
                        Accessible.name: qsTr("Sortie du script")
                    }
                }

                // Aide repliable : ce que le script peut lire et écrire, en français, avec unités.
                StudioButton {
                    id: helpToggle
                    objectName: "scriptHelpToggle"
                    quiet: true
                    checkable: true
                    iconName: "chevron"
                    iconRotation: checked ? 90 : 0
                    text: checked ? qsTr("Masquer l'aide") : qsTr("Aide : ce que le script peut lire")
                }
                Hint {
                    objectName: "scriptHelp"
                    visible: helpToggle.checked
                    textFormat: Text.StyledText
                    text: qsTr("<b>selection</b> : les objets choisis, à modifier. <b>objects</b> : tous les objets (lecture). <b>canvas[\"width_mm\"]</b>, <b>canvas[\"height_mm\"]</b> : taille du cadre.<br>"
                             + "Pour chaque objet <b>o</b> :<br>"
                             + "• <b>o.area_mm2</b> : surface en mm² ; <b>o.width_mm</b>, <b>o.height_mm</b> : largeur et hauteur en mm ;<br>"
                             + "• <b>o.x</b>, <b>o.y</b> : centre en mm (x vers la droite, y vers le haut) ;<br>"
                             + "• <b>o.color.hue</b> : teinte de 0 à 360° (rouge ≈ 0, vert ≈ 120, bleu ≈ 240) ; <b>o.color.saturation</b>, <b>o.color.lightness</b> : 0 à 100 ;<br>"
                             + "• <b>o.type</b> : « tatami », « satin », « running »… ; <b>o.params</b> : réglages actuels (ex. o.params[\"row_spacing_mm\"]).<br>"
                             + "Pour modifier : <b>o.set(row_spacing_mm=0.4)</b> (écart entre rangées, mm), <b>o.set(angle_deg=45)</b> (angle des rangées, °), <b>o.set_color(\"#FF0000\")</b>. Les noms sont ceux de l'inspecteur ; un type qui n'a pas le réglage le refuse à l'aperçu.")
                }
            }
        }

        // Pied fixe : l'état du dernier passage et les deux actions restent toujours visibles.
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.divider }
        Text {
            objectName: "scriptMessage"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            maximumLineCount: 5
            elide: Text.ElideRight
            visible: text.length > 0
            color: panel.ready && !panel.controller.lastOk ? Theme.error : Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallSize
            text: panel.ready ? panel.controller.message : ""
        }
        Text {
            objectName: "scriptStale"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            visible: panel.ready && panel.controller.previewReady && panel.controller.previewStale
            text: qsTr("Le script, la sélection ou le motif ont changé : « Appliquer » relancera le script.")
            color: Theme.warning
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallSize
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            StudioButton {
                objectName: "scriptPreview"
                text: qsTr("Aperçu")
                tooltip: qsTr("Exécute le script et liste les valeurs qui changeraient, sans rien modifier")
                enabled: panel.ready && panel.count > 0 && !panel.controller.busy
                disabledReason: panel.count === 0 ? qsTr("Sélectionnez d'abord des objets") : qsTr("Script en cours")
                onClicked: panel.controller.preview()
            }
            StudioButton {
                objectName: "scriptApply"
                emphasized: true
                text: qsTr("Appliquer")
                tooltip: qsTr("Applique les changements en une étape (Ctrl+Z pour annuler)")
                enabled: panel.ready && panel.count > 0 && !panel.controller.busy
                disabledReason: panel.count === 0 ? qsTr("Sélectionnez d'abord des objets") : qsTr("Script en cours")
                onClicked: panel.controller.apply()
            }
            Item { Layout.fillWidth: true }
            StudioButton {
                text: qsTr("Ignorer")
                tooltip: qsTr("Ignore le résultat du script en cours : rien ne sera modifié")
                quiet: true
                visible: panel.ready && panel.controller.busy
                onClicked: panel.controller.cancel()
            }
        }
    }
}
