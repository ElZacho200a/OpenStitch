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
    // Expérimental : zones de fondu (peintures, ciels, eau), chaque zone cousue en fondu de fils
    // mélangés au lieu d'aplats quantifiés.
    property bool blendZones: imageTools.segmentationDefaults.blend_zones
    property int blendThreads: imageTools.segmentationDefaults.blend_threads
    readonly property var options: ({ max_colors: maxColors, min_region_px: minRegionPx,
                                      smoothing_radius_px: smoothing, blend_zones: blendZones,
                                      blend_threads: blendThreads })
    // The preview describes a specific parameter tuple. Invalidate/cancel it
    // immediately when colours, island size or smoothing changes.
    // Aperçu en direct (comme en V1) : un changement de réglage relance l'aperçu après 350 ms, sans
    // toucher au document. Désactivable pour les grosses images.
    property bool liveEnabled: true
    function invalidatePreview() {
        if (imageTools.segPreviewActive || imageTools.busy)
            imageTools.cancelSegmentationPreview()
        if (section.liveEnabled)
            livePreview.restart()
    }
    Timer {
        id: livePreview
        objectName: "segLiveTimer"
        interval: 350
        onTriggered: {
            // Hors de son onglet (section masquée), l'aperçu ne part pas : il recouvrirait les
            // régions que l'on retouche.
            if (!section.liveEnabled || !imageTools.hasImage || !section.visible)
                return
            if (imageTools.busy)
                livePreview.restart() // attend la fin du calcul en cours
            else
                imageTools.previewSegmentation(section.options)
        }
    }
    onMaxColorsChanged: invalidatePreview()
    onMinRegionPxChanged: invalidatePreview()
    onSmoothingChanged: invalidatePreview()
    onBlendZonesChanged: invalidatePreview()
    onBlendThreadsChanged: invalidatePreview()

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
              : imageTools.hasSegmentation ? qsTr("Segmentation du projet : %1 régions, %2 couleurs").arg(imageTools.regionCount).arg(imageTools.colorCount)
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
            enabled: !section.blendZones
            onValueModified: section.maxColors = value }
        Text { text: qsTr("Taille minimale (px)"); color: Theme.foreground; font.pixelSize: Theme.bodySize; font.family: Theme.fontFamily; Layout.fillWidth: true }
        StudioSpinBox { objectName: "segMinRegion"; from: 1; to: 100000; value: section.minRegionPx; editable: true
            onValueModified: section.minRegionPx = value }
        Text { text: qsTr("Lissage (px)"); color: Theme.foreground; font.pixelSize: Theme.bodySize; font.family: Theme.fontFamily; Layout.fillWidth: true }
        StudioSpinBox { objectName: "segSmoothing"; from: 0; to: 10; value: section.smoothing; editable: true
            onValueModified: section.smoothing = value }
    }
    StudioCheckBox {
        objectName: "segBlendZones"
        text: qsTr("Zones de fondu (expérimental)")
        checked: section.blendZones
        onToggled: section.blendZones = checked
        palette.windowText: Theme.foreground
    }
    GridLayout {
        columns: 2
        Layout.fillWidth: true
        columnSpacing: 8
        visible: section.blendZones
        Text { text: qsTr("Fils de la palette"); color: Theme.foreground; font.pixelSize: Theme.bodySize; font.family: Theme.fontFamily; Layout.fillWidth: true }
        StudioSpinBox { objectName: "segBlendThreads"; from: 2; to: 16; value: section.blendThreads; editable: true
            onValueModified: section.blendThreads = value }
    }
    Text {
        objectName: "segBlendHint"
        Layout.fillWidth: true
        visible: section.blendZones
        text: qsTr("Découpe l'image aux seuls bords nets ; « Mettre à jour les formes » coud ensuite chaque zone en fondu de 1 à 3 fils lus dans l'image. Expérimental : selon l'image, plus de coupes ou des couleurs moins fidèles qu'une segmentation classique.")
        wrapMode: Text.WordWrap
        color: Theme.muted; font.pixelSize: Theme.smallSize; font.family: Theme.fontFamily
    }
    StudioCheckBox {
        objectName: "segLiveBox"
        text: qsTr("Aperçu en direct")
        checked: section.liveEnabled
        onToggled: { section.liveEnabled = checked; if (checked) livePreview.restart() }
        palette.windowText: Theme.foreground
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
    StudioButton {
        objectName: "segmentAllLayersButton"
        Layout.fillWidth: true
        visible: imageTools.imageLayers.length > 1
        enabled: !imageTools.busy && !studio.busy
        text: qsTr("Segmenter tous les calques")
        tooltip: qsTr("Recalcule les régions de chaque image avec ces réglages. L’ensemble est annulable.")
        onClicked: {
            livePreview.stop()
            imageTools.applySegmentationToAllLayers(section.options)
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
