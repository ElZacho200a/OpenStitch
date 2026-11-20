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

    // Sélection EXPLICITE des calques à segmenter (remplace le bouton binaire « tous les
    // calques ») : map id(string) -> cochée ; absent = cochée par défaut (audit multicalque
    // segmentation, P1). `imageTools.applySegmentationToLayers` fait le travail, même
    // CompositeCommand (une étape d'annulation) que l'ancien « tous les calques ».
    property var layerSelection: ({})
    function isLayerChecked(id) {
        const key = String(id)
        return section.layerSelection[key] !== undefined ? section.layerSelection[key] : true
    }
    function setLayerChecked(id, value) {
        const key = String(id)
        const next = Object.assign({}, section.layerSelection)
        next[key] = value
        section.layerSelection = next
    }
    readonly property var selectedLayerIds: {
        const ids = imageTools.imageLayerIds
        const out = []
        for (let i = 0; i < ids.length; i++) {
            if (section.isLayerChecked(ids[i]))
                out.push(ids[i])
        }
        return out
    }
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
    ColumnLayout {
        id: layerPicker
        objectName: "segmentLayersPicker"
        Layout.fillWidth: true
        spacing: 2
        visible: imageTools.imageLayers.length > 1
        SectionLabel { text: qsTr("CALQUES À SEGMENTER") }
        Repeater {
            model: imageTools.imageLayers
            delegate: StudioCheckBox {
                required property string modelData
                required property int index
                objectName: "segLayerCheck_" + index
                Layout.fillWidth: true
                text: modelData
                checked: section.isLayerChecked(imageTools.imageLayerIds[index])
                onToggled: section.setLayerChecked(imageTools.imageLayerIds[index], checked)
                palette.windowText: Theme.foreground
            }
        }
    }
    StudioButton {
        objectName: "segmentSelectedLayersButton"
        Layout.fillWidth: true
        visible: imageTools.imageLayers.length > 1
        enabled: !imageTools.busy && !studio.busy && section.selectedLayerIds.length > 0
        text: qsTr("Segmenter la sélection (%1/%2)").arg(section.selectedLayerIds.length).arg(imageTools.imageLayers.length)
        tooltip: qsTr("Recalcule les régions des calques cochés avec ces réglages. L'ensemble est annulable.")
        onClicked: {
            livePreview.stop()
            imageTools.applySegmentationToLayers(section.options, section.selectedLayerIds)
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
