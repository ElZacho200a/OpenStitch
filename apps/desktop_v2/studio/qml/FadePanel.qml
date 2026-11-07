// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."

// Fondu de deux fils sur le remplissage sélectionné : créer, modifier, supprimer, courbe de
// transition et poignées de l'axe (valeurs numériques, mm modèle). Adaptateur sur FadeController.
Rectangle {
    id: panel
    objectName: "fadePanel"
    property var controller
    readonly property bool ready: controller !== undefined && controller !== null
    color: Theme.panel
    border.color: Theme.divider
    radius: Theme.radius

    Flickable {
        anchors.fill: parent
        anchors.margins: 10
        contentHeight: column.implicitHeight
        clip: true
        ColumnLayout {
            id: column
            width: parent.width - 12
            spacing: 8
            SectionLabel { text: qsTr("FONDU DE DEUX FILS") }
            Text {
                objectName: "fadeSummary"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.faint
                font.family: Theme.fontFamily
                text: !panel.ready ? "" : panel.controller.hasFade ? panel.controller.summary : panel.controller.incompatibility
            }
            RowLayout {
                Text { text: qsTr("Fil du dessus (#rrggbb)"); color: Theme.foreground; font.family: Theme.fontFamily }
                StudioTextField { id: colorField; objectName: "fadeColorField"; text: "#c81e1e"; Layout.preferredWidth: 90 } // lint-ok: default thread colour (document data)
            }
            StudioCheckBox { id: complementary; objectName: "fadeComplementary"; text: qsTr("Rampes de densité complémentaires"); checked: true }
            Flow {
                Layout.fillWidth: true
                spacing: 4
                StudioButton { objectName: "fadeCreate"; text: qsTr("Créer le fondu"); emphasized: true; enabled: panel.ready && panel.controller.canCreate
                    onClicked: panel.controller.create(colorField.text, complementary.checked) }
                StudioButton { objectName: "fadeEdit"; text: qsTr("Appliquer les réglages"); enabled: panel.ready && panel.controller.hasFade
                    onClicked: panel.controller.edit(colorField.text, complementary.checked) }
                StudioButton { objectName: "fadeRemove"; text: qsTr("Supprimer le fondu"); enabled: panel.ready && panel.controller.hasFade
                    onClicked: panel.controller.remove() }
            }
            SectionLabel { text: qsTr("COURBE DE TRANSITION") }
            Flow {
                Layout.fillWidth: true
                spacing: 4
                enabled: panel.ready && panel.controller.hasFade
                Repeater {
                    model: panel.ready ? panel.controller.curves : []
                    delegate: StudioButton {
                        required property var modelData
                        objectName: "fadeCurve" + modelData.id
                        text: modelData.label
                        checkable: true
                        checked: panel.controller.curve === modelData.id
                        onClicked: panel.controller.setCurve(modelData.id)
                    }
                }
            }
            SectionLabel { text: qsTr("AXE (mm) : poignées début, fin, déplacement, rotation") }
            RowLayout {
                enabled: panel.ready && panel.controller.hasFade
                StudioComboBox { id: handle; objectName: "fadeHandle"; model: [qsTr("Début"), qsTr("Fin"), qsTr("Déplacer"), qsTr("Rotation")] }
                StudioTextField { id: hx; objectName: "fadeHandleX"; Layout.preferredWidth: 64; placeholderText: "x" }
                StudioTextField { id: hy; objectName: "fadeHandleY"; Layout.preferredWidth: 64; placeholderText: "y" }
                StudioButton { objectName: "fadeHandleApply"; text: qsTr("Appliquer")
                    onClicked: panel.controller.commitHandle(handle.currentIndex, parseFloat(hx.text), parseFloat(hy.text), false) }
            }
            Text {
                objectName: "fadeMessage"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: panel.ready ? panel.controller.message : ""
                color: panel.ready && panel.controller.lastOk ? Theme.faint : Theme.accent
                font.family: Theme.fontFamily
            }
        }
    }
}
