// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Layouts
import "../.."

// Taille physique de l'image source. Ratio conservé (les pixels ne changent pas). Tout est
// délégué à ImageSpaceController : une étape d'annulation, refus clair si des objets existent.
ColumnLayout {
    id: sizeSection
    objectName: "imageSizeSection"
    required property var space // ImageSpaceController
    spacing: 6

    function commitWidth() {
        sizeSection.space.setImageWidthMm(widthField.text.replace(",", "."))
        widthField.text = sizeSection.space.imageWidthMm.toFixed(1)
    }
    function commitHeight() {
        sizeSection.space.setImageHeightMm(heightField.text.replace(",", "."))
        heightField.text = sizeSection.space.imageHeightMm.toFixed(1)
    }

    SectionLabel { text: qsTr("TAILLE DE L'IMAGE") }
    GridLayout {
        columns: 3
        columnSpacing: 6
        rowSpacing: 4
        StudioLabel { text: qsTr("Largeur (mm)"); role: "small" }
        StudioTextField {
            id: widthField
            objectName: "imageWidthMm"
            Layout.fillWidth: true
            enabled: sizeSection.space && sizeSection.space.canResize
            text: sizeSection.space ? sizeSection.space.imageWidthMm.toFixed(1) : ""
            onEditingFinished: sizeSection.commitWidth()
        }
        Item { Layout.preferredWidth: 1 }
        StudioLabel { text: qsTr("Hauteur (mm)"); role: "small" }
        StudioTextField {
            id: heightField
            objectName: "imageHeightMm"
            Layout.fillWidth: true
            enabled: sizeSection.space && sizeSection.space.canResize
            text: sizeSection.space ? sizeSection.space.imageHeightMm.toFixed(1) : ""
            onEditingFinished: sizeSection.commitHeight()
        }
        Item { Layout.preferredWidth: 1 }
    }
    RowLayout {
        spacing: 4
        Repeater {
            model: [50, 75, 125, 150, 200]
            delegate: StudioButton {
                required property int modelData
                objectName: "imageScale_" + modelData
                text: modelData + " %"
                enabled: sizeSection.space && sizeSection.space.canResize
                tooltip: qsTr("Multiplier la taille de l'image par %1 %").arg(modelData / 100)
                onClicked: sizeSection.space.scaleImage(modelData)
            }
        }
    }
    StudioLabel {
        visible: sizeSection.space && !sizeSection.space.canResize
        objectName: "imageSizeLocked"
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        role: "small"
        text: qsTr("Des objets existent déjà : redimensionnez-les par la sélection.")
    }
}
