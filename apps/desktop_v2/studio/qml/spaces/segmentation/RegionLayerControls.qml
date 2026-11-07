// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Layouts
import "../.."

// Réglages d'affichage de la couche « regions » + actions de l'étape : image dessous, opacité,
// contours, « Segmenter l'image », « Mettre à jour les formes » (avec confirmation avant
// d'écraser des retouches de points). Vue seule : rien ici ne modifie le document.
Rectangle {
    id: controls
    objectName: "regionLayerControls"
    required property var seg
    property alias showSource: sourceSwitch.checked
    signal segmentRequested()
    color: Theme.panel
    border.color: Theme.border
    radius: Theme.radius
    implicitWidth: 300
    implicitHeight: column.implicitHeight + 16

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6
        Text {
            objectName: "segmentationSummary"
            Layout.fillWidth: true
            text: controls.seg.hasSegmentation
                  ? qsTr("%1 régions · %2 sélectionnée(s)").arg(controls.seg.regionCount).arg(controls.seg.selectedCount)
                  : qsTr("Pas encore de segmentation")
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.bodySize
            wrapMode: Text.WordWrap
        }
        StudioSwitch {
            id: sourceSwitch
            objectName: "regionLayerSource"
            text: qsTr("Image dessous")
            checked: true
        }
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: qsTr("Opacité")
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
            }
            StudioSlider {
                objectName: "regionLayerOpacity"
                Layout.fillWidth: true
                from: 0.1
                to: 1.0
                value: controls.seg.opacity
                onMoved: controls.seg.opacity = value
            }
        }
        StudioSwitch {
            objectName: "regionLayerContours"
            text: qsTr("Contours")
            checked: controls.seg.contours
            onToggled: controls.seg.contours = checked
        }
        RowLayout {
            Layout.fillWidth: true
            StudioButton {
                objectName: "segmentImageButton"
                text: qsTr("Segmenter l'image")
                enabled: controls.seg.canSegment
                onClicked: controls.segmentRequested()
            }
            StudioButton {
                objectName: "updateShapesButton"
                text: qsTr("Mettre à jour les formes")
                emphasized: controls.seg.stageState === "stale"
                enabled: controls.seg.canUpdateShapes
                onClicked: controls.seg.updateShapes(false)
            }
        }
        Rectangle {
            objectName: "shapesConfirm"
            Layout.fillWidth: true
            visible: controls.seg.confirmText.length > 0
            implicitHeight: confirmColumn.implicitHeight + 16
            color: Theme.accentSoft
            border.color: Theme.accent
            ColumnLayout {
                id: confirmColumn
                anchors.fill: parent
                anchors.margins: 8
                Text {
                    Layout.fillWidth: true
                    text: controls.seg.confirmText
                    wrapMode: Text.WordWrap
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallSize
                }
                RowLayout {
                    StudioButton {
                        objectName: "shapesConfirmYes"
                        text: qsTr("Remplacer les retouches")
                        emphasized: true
                        onClicked: controls.seg.updateShapes(true)
                    }
                    StudioButton {
                        objectName: "shapesConfirmNo"
                        text: qsTr("Garder mes retouches")
                        onClicked: controls.seg.cancelConfirm()
                    }
                }
            }
        }
    }
}
