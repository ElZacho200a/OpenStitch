// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Layouts
import "../.."

// Réglages d'affichage SEULS de la couche « regions » : image dessous, opacité, contours, et le
// résumé région(s)/sélection. Aucune action ici (« Segmenter l'image », « Mettre à jour les
// formes ») : elles vivent uniquement dans le panneau de droite (onglets Segmentation/Régions),
// pour qu'une même intention n'ait plus qu'un seul déclencheur à l'écran (audit UX panneaux
// Segmentation). Vue seule : rien ici ne modifie le document.
Rectangle {
    id: controls
    objectName: "regionLayerControls"
    required property var seg
    property alias showSource: sourceSwitch.checked
    color: Theme.panel
    border.color: Theme.divider
    radius: Theme.cardRadius
    implicitWidth: 220
    implicitHeight: column.implicitHeight + 24

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: 12
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
    }
}
