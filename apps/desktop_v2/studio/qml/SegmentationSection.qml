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

    // Pas de vignette d'aperçu ici : le canevas principal (plein format) montre déjà l'image
    // segmentée ou l'aperçu en direct, avec le badge « Aperçu de segmentation — document
    // inchangé » (SegmentationSpace.qml) quand c'est un aperçu. Une seconde miniature ici n'aurait
    // fait que dupliquer la même information en plus petit (audit UX panneaux Segmentation).
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
            onValueModified: section.maxColors = value }
        Text { text: qsTr("Taille minimale (px)"); color: Theme.foreground; font.pixelSize: Theme.bodySize; font.family: Theme.fontFamily; Layout.fillWidth: true }
        StudioSpinBox { objectName: "segMinRegion"; from: 1; to: 100000; value: section.minRegionPx; editable: true
            onValueModified: section.minRegionPx = value }
        Text { text: qsTr("Lissage (px)"); color: Theme.foreground; font.pixelSize: Theme.bodySize; font.family: Theme.fontFamily; Layout.fillWidth: true }
        StudioSpinBox { objectName: "segSmoothing"; from: 0; to: 10; value: section.smoothing; editable: true
            onValueModified: section.smoothing = value }
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
            // Même garde que SegmentationSpaceController::canSegment() : un pinceau/une fusion de
            // régions en cours ne doit pas être écrasé par un calcul de segmentation parallèle.
            enabled: imageTools.hasImage && !imageTools.busy && !regionTools.busy
            onClicked: imageTools.previewSegmentation(section.options)
        }
        StudioButton {
            objectName: "segApplyButton"
            text: qsTr("Appliquer")
            emphasized: true
            enabled: imageTools.hasImage && !imageTools.busy && !regionTools.busy
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
