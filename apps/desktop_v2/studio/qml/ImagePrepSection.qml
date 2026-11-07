// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."

// Prétraitement de l'image source, avec AVANT / APRÈS. Tout est délégué à ImageController :
// aucun calcul ici. « Aperçu » calcule l'image sans toucher au document ; « Appliquer » pousse
// UNE opération annulable.
ColumnLayout {
    id: section
    objectName: "imagePrepSection"
    spacing: 8

    property int currentIndex: 0
    readonly property var entry: imageTools.prepCatalog[currentIndex]
    property var values: ({})

    function resetValues() {
        var v = {}
        var fields = entry ? entry.fields : []
        for (var i = 0; i < fields.length; ++i) v[fields[i].key] = fields[i].value
        values = v
        imageTools.clearPrepPreview()
    }
    function setValue(key, value) {
        var v = Object.assign({}, values)
        v[key] = value
        values = v
    }
    Component.onCompleted: resetValues()
    onCurrentIndexChanged: resetValues()

    SectionLabel { text: qsTr("AVANT / APRÈS") }
    RowLayout {
        Layout.fillWidth: true
        Layout.preferredHeight: 120
        spacing: 6
        Repeater {
            model: [
                { key: "before", title: qsTr("Avant") },
                { key: imageTools.prepPreviewActive ? "preview" : "after",
                  title: imageTools.prepPreviewActive ? qsTr("Aperçu") : qsTr("Après") }
            ]
            delegate: Rectangle {
                required property var modelData
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.surface
                border.color: Theme.border
                Image {
                    objectName: "prepImage_" + parent.modelData.key
                    anchors.fill: parent
                    anchors.margins: 2
                    fillMode: Image.PreserveAspectFit
                    asynchronous: false
                    cache: false
                    source: imageTools.hasImage
                            ? "image://prepare/" + parent.modelData.key + "?rev=" + imageTools.imageRevision
                            : ""
                }
                Text {
                    anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 3
                    text: parent.modelData.title
                    color: Theme.muted; font.pixelSize: Theme.captionSize; font.family: Theme.fontFamily
                }
            }
        }
    }
    Text {
        objectName: "prepSizeLabel"
        Layout.fillWidth: true
        text: imageTools.hasImage
              ? qsTr("Image de travail : %1 × %2 px — %3 traitement(s)")
                    .arg(imageTools.imageWidth).arg(imageTools.imageHeight).arg(imageTools.ops.length)
              : qsTr("Aucune image source : ouvrez ou importez une image.")
        color: Theme.muted; font.pixelSize: Theme.smallSize; font.family: Theme.fontFamily
        wrapMode: Text.WordWrap
    }

    SectionLabel { text: qsTr("TRAITEMENT") }
    Controls.ComboBox {
        id: kindBox
        objectName: "prepKind"
        Layout.fillWidth: true
        model: imageTools.prepCatalog
        textRole: "label"
        enabled: imageTools.hasImage
        currentIndex: section.currentIndex
        onActivated: function(index) { section.currentIndex = index }
    }
    Repeater {
        model: section.entry ? section.entry.fields : []
        delegate: RowLayout {
            required property var modelData
            Layout.fillWidth: true
            Text {
                Layout.preferredWidth: 110
                text: parent.modelData.label
                color: Theme.foreground; font.pixelSize: Theme.bodySize; font.family: Theme.fontFamily
            }
            Controls.Slider {
                objectName: "prepField_" + parent.modelData.key
                Layout.fillWidth: true
                from: parent.modelData.min
                to: parent.modelData.max
                stepSize: parent.modelData.step
                value: section.values[parent.modelData.key] !== undefined
                       ? section.values[parent.modelData.key] : parent.modelData.value
                onMoved: section.setValue(parent.modelData.key, value)
            }
            Text {
                Layout.preferredWidth: 38
                text: Number(section.values[parent.modelData.key]).toString()
                horizontalAlignment: Text.AlignRight
                color: Theme.muted; font.pixelSize: Theme.smallSize; font.family: Theme.fontFamily
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        StudioButton {
            objectName: "prepPreviewButton"
            text: qsTr("Aperçu")
            enabled: imageTools.hasImage && !imageTools.busy
            onClicked: imageTools.previewPrep(section.entry.kind, section.values)
        }
        StudioButton {
            objectName: "prepApplyButton"
            text: qsTr("Appliquer")
            emphasized: true
            enabled: imageTools.hasImage && !imageTools.busy
            onClicked: imageTools.applyPrep(section.entry.kind, section.values)
        }
        StudioButton {
            objectName: "prepCancelButton"
            text: qsTr("Annuler l'aperçu")
            quiet: true
            visible: imageTools.prepPreviewActive || imageTools.busy
            onClicked: imageTools.prepPreviewActive ? imageTools.clearPrepPreview() : imageTools.cancelTask()
        }
    }

    SectionLabel { text: qsTr("HISTORIQUE DES TRAITEMENTS") }
    Repeater {
        model: imageTools.ops
        delegate: Text {
            required property string modelData
            required property int index
            Layout.fillWidth: true
            text: (index + 1) + ". " + modelData
            color: Theme.foreground; font.pixelSize: Theme.smallSize; font.family: Theme.fontFamily
            elide: Text.ElideRight
        }
    }
    Text {
        visible: imageTools.ops.length === 0
        text: qsTr("Aucun traitement : l'image d'origine est utilisée telle quelle.")
        color: Theme.faint; font.pixelSize: Theme.smallSize; font.family: Theme.fontFamily
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
    Text {
        objectName: "prepMessage"
        Layout.fillWidth: true
        visible: imageTools.message.length > 0
        text: imageTools.message
        wrapMode: Text.WordWrap
        color: imageTools.messageIsError ? "#ff8a8a" : Theme.success
        font.pixelSize: Theme.smallSize; font.family: Theme.fontFamily
    }
}
