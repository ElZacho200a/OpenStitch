// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."

// Segmentation couleur : réglages, aperçu annulable (document intact), application en une étape.
ColumnLayout {
    id: section
    objectName: "segmentationSection"
    spacing: 8

    property int maxColors: imageTools.segmentationDefaults.max_colors
    property int minRegionPx: imageTools.segmentationDefaults.min_region_px
    property int smoothing: imageTools.segmentationDefaults.smoothing_radius_px
    readonly property var options: ({ max_colors: maxColors, min_region_px: minRegionPx,
                                      smoothing_radius_px: smoothing })

    SectionLabel { text: qsTr("APERÇU DE LA SEGMENTATION") }
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 150
        color: Theme.surface
        border.color: Theme.border
        Image {
            objectName: "segmentationImage"
            anchors.fill: parent
            anchors.margins: 2
            fillMode: Image.PreserveAspectFit
            cache: false
            source: imageTools.hasImage
                    ? "image://prepare/" + (imageTools.segPreviewActive ? "segmentation" : "after")
                      + "?rev=" + imageTools.imageRevision
                    : ""
        }
    }
    Text {
        objectName: "segmentationInfo"
        Layout.fillWidth: true
        text: imageTools.segPreviewActive ? imageTools.segPreviewInfo
              : imageTools.hasSegmentation ? qsTr("Segmentation du projet : %1 régions").arg(imageTools.regionCount)
              : qsTr("Pas encore de segmentation.")
        color: Theme.muted; font.pixelSize: Theme.smallSize; font.family: Theme.fontFamily
        wrapMode: Text.WordWrap
    }

    SectionLabel { text: qsTr("RÉGLAGES") }
    GridLayout {
        columns: 2
        Layout.fillWidth: true
        columnSpacing: 8
        Text { text: qsTr("Couleurs maximum"); color: Theme.foreground; font.pixelSize: Theme.bodySize; font.family: Theme.fontFamily; Layout.fillWidth: true }
        StudioSpinBox { objectName: "segMaxColors"; from: 2; to: 64; value: section.maxColors; editable: true
            onValueModified: section.maxColors = value }
        Text { text: qsTr("Taille minimale (px)"); color: Theme.foreground; font.pixelSize: Theme.bodySize; font.family: Theme.fontFamily; Layout.fillWidth: true }
        StudioSpinBox { objectName: "segMinRegion"; from: 1; to: 100000; value: section.minRegionPx; editable: true
            onValueModified: section.minRegionPx = value }
        Text { text: qsTr("Lissage (px)"); color: Theme.foreground; font.pixelSize: Theme.bodySize; font.family: Theme.fontFamily; Layout.fillWidth: true }
        StudioSpinBox { objectName: "segSmoothing"; from: 0; to: 10; value: section.smoothing; editable: true
            onValueModified: section.smoothing = value }
    }
    RowLayout {
        Layout.fillWidth: true
        StudioButton {
            objectName: "segPreviewButton"
            text: qsTr("Aperçu")
            enabled: imageTools.hasImage && !imageTools.busy
            onClicked: imageTools.previewSegmentation(section.options)
        }
        StudioButton {
            objectName: "segApplyButton"
            text: qsTr("Appliquer")
            emphasized: true
            enabled: imageTools.hasImage && !imageTools.busy
            onClicked: imageTools.applySegmentation(section.options)
        }
        StudioButton {
            objectName: "segCancelButton"
            text: imageTools.busy ? qsTr("Annuler le calcul") : qsTr("Annuler l'aperçu")
            quiet: true
            visible: imageTools.segPreviewActive || imageTools.busy
            onClicked: imageTools.busy ? imageTools.cancelTask() : imageTools.cancelSegmentationPreview()
        }
    }
    Text {
        Layout.fillWidth: true
        visible: imageTools.busy
        text: imageTools.busyLabel + "…"
        color: Theme.accent; font.pixelSize: Theme.smallSize; font.family: Theme.fontFamily
    }
    Text {
        objectName: "segMessage"
        Layout.fillWidth: true
        visible: imageTools.message.length > 0
        text: imageTools.message
        wrapMode: Text.WordWrap
        color: imageTools.messageIsError ? Theme.error : Theme.success
        font.pixelSize: Theme.smallSize; font.family: Theme.fontFamily
    }
}
