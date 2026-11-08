// SPDX-License-Identifier: Apache-2.0
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import "../.."

// Pile d'opérations de l'image (non destructive) : chaque ligne est un traitement rejoué depuis
// l'original. Supprimer / réordonner = UNE étape d'annulation (commande du document), jamais de
// logique ici. Désactiver une opération sans la supprimer n'existe pas dans le modèle de document.
ColumnLayout {
    id: stack
    objectName: "imageOpStack"
    required property var space // ImageSpaceController
    spacing: 4

    SectionLabel { text: qsTr("PILE DE TRAITEMENTS") }
    Repeater {
        model: stack.space ? stack.space.opStack : []
        delegate: Rectangle {
            id: row
            required property var modelData
            required property int index
            objectName: "opRow_" + index
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.controlHeight
            radius: Theme.radius
            color: Theme.surface
            border.color: Theme.divider
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 4
                spacing: 4
                StudioLabel {
                    text: (row.index + 1) + "."
                    role: "small"
                    Layout.preferredWidth: 22
                }
                StudioLabel {
                    objectName: "opName_" + row.index
                    text: row.modelData.name
                    role: "small"
                    tone: "foreground"
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                    Layout.fillWidth: true
                }
                StudioButton {
                    objectName: "opUp_" + row.index
                    text: "↑"
                    quiet: true
                    tooltip: qsTr("Remonter ce traitement")
                    enabled: row.index > 0
                    onClicked: stack.space.moveOp(row.index, -1)
                }
                StudioButton {
                    objectName: "opDown_" + row.index
                    text: "↓"
                    quiet: true
                    tooltip: qsTr("Descendre ce traitement")
                    enabled: row.index < stack.space.opStack.length - 1
                    onClicked: stack.space.moveOp(row.index, 1)
                }
                StudioButton {
                    objectName: "opDelete_" + row.index
                    text: "✕"
                    quiet: true
                    tooltip: qsTr("Supprimer ce traitement (annulable)")
                    onClicked: stack.space.removeOp(row.index)
                }
            }
        }
    }
    StudioLabel {
        objectName: "opStackEmpty"
        visible: !stack.space || stack.space.opStack.length === 0
        text: qsTr("Aucun traitement : l'image d'origine est utilisée telle quelle.")
        role: "small"
        tone: "faint"
        Layout.fillWidth: true
    }
    StudioLabel {
        objectName: "opStackMessage"
        visible: stack.space && stack.space.message.length > 0
        text: stack.space ? stack.space.message : ""
        role: "small"
        Layout.fillWidth: true
    }
}
