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
    // Expérimental : un trait fin (cordage, nervure, hauban) lu sur l'image survit au lissage et au
    // nettoyage des petites régions au lieu d'être effacé ou fragmenté.
    property bool fineStructures: imageTools.segmentationDefaults.fine_structures
    readonly property var options: ({ max_colors: maxColors, min_region_px: minRegionPx,
                                      smoothing_radius_px: smoothing, blend_zones: blendZones,
                                      blend_threads: blendThreads,
                                      fine_structures: fineStructures })
    // The preview describes a specific parameter tuple. Invalidate/cancel it
    // immediately when colours, island size or smoothing changes.
    // Satin de la génération des objets : état partagé `studio.satinOptions` (onglet Régions et
    // espace Vecteurs lisent le même) ; une clé absente = valeur du moteur.
    readonly property bool satinBands: studio.satinOptions.satin_bands === true
    readonly property bool satinCompare: satinBands && studio.satinOptions.satin_compare === true
    // Sans effet en zones de fondu (chaque zone devient un fondu).
    readonly property bool satinAvailable: !blendZones
    property bool satinAdvancedOpen: false
    function setSatinOption(key, value) {
        const next = Object.assign({}, studio.satinOptions)
        if (value === undefined || value === null || value === "")
            delete next[key]
        else
            next[key] = value
        studio.satinOptions = next
    }
    // Champ avancé : "" si vide (défaut du moteur), null si invalide, sinon le nombre (0 à max).
    function satinNumber(text, max) {
        const t = String(text).trim().replace(",", ".")
        if (t.length === 0)
            return ""
        const n = Number(t)
        return isFinite(n) && n >= 0 && n <= max ? n : null
    }
    function commitSatinField(key, text, max) {
        const n = section.satinNumber(text, max)
        section.setSatinOption(key, n === null ? "" : n)
    }
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
    onFineStructuresChanged: invalidatePreview()

    // Pas de vignette d'aperçu ici : le canevas principal (plein format) montre déjà l'image
    // segmentée ou l'aperçu en direct, avec le badge « Aperçu de segmentation — document
    // inchangé » (SegmentationSpace.qml) quand c'est un aperçu. Une seconde miniature ici n'aurait
    // fait que dupliquer la même information en plus petit (audit UX panneaux Segmentation).
    Text {
        objectName: "segmentationInfo"
        Layout.fillWidth: true
        text: imageTools.segPreviewActive ? qsTr("Aperçu (non appliqué) : %1").arg(imageTools.segPreviewInfo)
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
        text: qsTr("Découpe l'image aux seuls bords nets ; À l'étape Vecteurs, « Recalculer les formes » coud ensuite chaque zone avec un fondu de 1 à 3 fils pris dans la palette. Expérimental : selon l'image, plus de coupes ou des couleurs moins fidèles qu'une segmentation classique.")
        wrapMode: Text.WordWrap
        color: Theme.muted; font.pixelSize: Theme.smallSize; font.family: Theme.fontFamily
    }
    // Deux réglages distincts, regroupés et nommés par l'étape où ils agissent (revue UX : deux
    // libellés quasi identiques étaient confondus) : celui-ci, à la segmentation ; le suivant, à
    // l'étape Vecteurs.
    SectionLabel { text: qsTr("TRAITS FINS (CORDAGES, NERVURES)") }
    StudioCheckBox {
        objectName: "segFineStructures"
        text: qsTr("Les garder dans les régions (expérimental)")
        checked: section.fineStructures
        enabled: !section.blendZones
        onToggled: section.fineStructures = checked
        palette.windowText: Theme.foreground
    }
    // Un seul texte, trois états : pourquoi cocher, ce que ça change, pourquoi c'est grisé.
    Text {
        objectName: "segFineStructuresHint"
        Layout.fillWidth: true
        text: section.blendZones ? qsTr("Indisponible avec les zones de fondu.")
              : section.fineStructures ? qsTr("Les fils, même d'un pixel ou peu contrastés, restent des régions. Une texture ou un bruit marqué peut ajouter de petites régions : vérifiez l'aperçu.")
              : qsTr("Des fils très fins disparaissent de l'aperçu ? Cochez cette case.")
        wrapMode: Text.WordWrap
        color: Theme.muted; font.pixelSize: Theme.smallSize; font.family: Theme.fontFamily
    }
    StudioCheckBox {
        objectName: "segPreserveLinework"
        text: qsTr("Coudre les traits sombres en lignes (étape Vecteurs)")
        checked: imageTools.preserveLinework
        enabled: imageTools.hasImage
        onToggled: imageTools.preserveLinework = checked
        palette.windowText: Theme.foreground
    }
    Text {
        objectName: "segLineworkHint"
        Layout.fillWidth: true
        visible: imageTools.preserveLinework
        text: qsTr("À l'étape Vecteurs, « Recalculer les formes » coud les aplats en continu sous les traits sombres fins (cordages, nervures) et crée chaque trait comme une ligne ouverte, cousue par-dessus.")
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
    // Satin des objets de broderie, juste sous « Appliquer » : UN seul état (studio.satinOptions)
    // pour « Régénérer les objets » (onglet Régions) et « Convertir en broderie » (Vecteurs).
    SectionLabel { text: qsTr("BRODERIE") }
    StudioCheckBox {
        objectName: "regionSatinBands"
        Layout.fillWidth: true
        text: qsTr("Satin sur les bandes")
        checked: section.satinBands
        enabled: section.satinAvailable
        onToggled: {
            section.setSatinOption("satin_bands", checked)
            checked = Qt.binding(() => section.satinBands) // reste lié à l'état partagé
        }
        palette.windowText: Theme.foreground
        Accessible.name: qsTr("Coudre les bandes étroites en satin")
    }
    StudioCheckBox {
        objectName: "regionSatinCompare"
        Layout.fillWidth: true
        text: qsTr("Choisir le meilleur satin (expérimental)")
        checked: section.satinCompare
        enabled: section.satinAvailable && section.satinBands
        onToggled: {
            section.setSatinOption("satin_compare", checked)
            checked = Qt.binding(() => section.satinCompare)
        }
        palette.windowText: Theme.foreground
        Accessible.name: qsTr("Choisir pour chaque bande le meilleur des satins mesurés")
    }
    // Un seul texte, trois états : ce que fait le satin, ce que change « meilleur satin », pourquoi
    // c'est grisé.
    Text {
        objectName: "regionSatinHint"
        Layout.fillWidth: true
        text: !section.satinAvailable ? qsTr("Satin indisponible avec les zones de fondu.")
              : !section.satinBands ? qsTr("Cochez « Satin sur les bandes » pour coudre les formes étroites (bordures, tiges, lettres) en satin plutôt qu'en remplissage. « Meilleur satin » demande cette case.")
              : section.satinCompare ? qsTr("Pour chaque bande, trois satins sont essayés et mesurés ; le meilleur est gardé. Génération plus lente : vérifiez le résultat dans l'espace Points.")
              : qsTr("Les formes étroites seront cousues en satin à la génération des objets. « Meilleur satin » compare en plus trois façons de les coudre.")
        wrapMode: Text.WordWrap
        color: Theme.muted; font.pixelSize: Theme.smallSize; font.family: Theme.fontFamily
    }
    // Réglages avancés repliés par défaut : un débutant n'a rien à y toucher.
    StudioButton {
        objectName: "regionSatinAdvanced"
        quiet: true
        visible: section.satinBands && section.satinAvailable
        text: section.satinAdvancedOpen ? qsTr("▾ Réglages avancés du satin") : qsTr("▸ Réglages avancés du satin")
        Accessible.role: Accessible.Button
        Accessible.checkable: true
        Accessible.checked: section.satinAdvancedOpen
        onClicked: section.satinAdvancedOpen = !section.satinAdvancedOpen
    }
    GridLayout {
        objectName: "regionSatinAdvancedSection"
        columns: 2
        Layout.fillWidth: true
        columnSpacing: 8
        visible: section.satinAdvancedOpen && section.satinBands && section.satinAvailable
        Text { text: qsTr("Couverture minimale (%)"); color: Theme.foreground; font.pixelSize: Theme.smallSize; font.family: Theme.fontFamily; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        StudioTextField {
            id: coverageField
            objectName: "regionSatinMinCoverage"
            Layout.preferredWidth: 80
            placeholderText: qsTr("auto")
            // Pas de liaison : la saisie n'est jamais réécrite pendant la frappe. Affichée en %,
            // transmise en fraction (0 à 1) au moteur.
            Component.onCompleted: text = studio.satinOptions.satin_min_coverage !== undefined ? String(Math.round(studio.satinOptions.satin_min_coverage * 1000) / 10) : ""
            invalid: section.satinNumber(text, 100) === null
            Accessible.name: qsTr("Part minimale de la forme couverte par le satin, en pour cent ; vide pour la valeur par défaut")
            Controls.ToolTip.visible: hovered
            Controls.ToolTip.delay: 600
            Controls.ToolTip.text: qsTr("Un satin n'est retenu que s'il couvre au moins cette part de la forme ; sinon elle reste en remplissage.")
            onTextChanged: {
                const n = section.satinNumber(text, 100)
                section.setSatinOption("satin_min_coverage", n === null || n === "" ? "" : n / 100)
            }
        }
        Text { text: qsTr("Largeur max. d'une forme compacte (mm)"); color: Theme.foreground; font.pixelSize: Theme.smallSize; font.family: Theme.fontFamily; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        StudioTextField {
            id: compactField
            objectName: "regionSatinCompactMax"
            Layout.preferredWidth: 80
            placeholderText: qsTr("auto")
            Component.onCompleted: text = studio.satinOptions.satin_compact_max_mm !== undefined ? String(studio.satinOptions.satin_compact_max_mm) : ""
            invalid: section.satinNumber(text, 1e6) === null
            Accessible.name: qsTr("Largeur maximale en millimètres d'une forme compacte cousue en satin ; vide pour la valeur par défaut")
            Controls.ToolTip.visible: hovered
            Controls.ToolTip.delay: 600
            Controls.ToolTip.text: qsTr("Disque ou forme ramassée : au-delà de cette largeur, elle reste en remplissage.")
            onTextChanged: section.commitSatinField("satin_compact_max_mm", text, 1e6)
        }
        Text { text: qsTr("Point satin le plus long (mm)"); color: section.satinCompare ? Theme.foreground : Theme.faint; font.pixelSize: Theme.smallSize; font.family: Theme.fontFamily; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        StudioTextField {
            id: stitchField
            objectName: "regionSatinMaxStitch"
            Layout.preferredWidth: 80
            enabled: section.satinCompare
            placeholderText: qsTr("auto")
            Component.onCompleted: text = studio.satinOptions.satin_compare_max_stitch_mm !== undefined ? String(studio.satinOptions.satin_compare_max_stitch_mm) : ""
            invalid: section.satinNumber(text, 1e6) === null
            Accessible.name: qsTr("Longueur maximale en millimètres d'un point satin pour le meilleur satin ; vide pour la valeur par défaut")
            Controls.ToolTip.visible: hovered
            Controls.ToolTip.delay: 600
            Controls.ToolTip.text: qsTr("« Meilleur satin » écarte un satin dont un point serait plus long : il accrocherait.")
            onTextChanged: section.commitSatinField("satin_compare_max_stitch_mm", text, 1e6)
        }
        Text {
            objectName: "regionSatinAdvancedHint"
            Layout.columnSpan: 2
            Layout.fillWidth: true
            readonly property bool anyInvalid: coverageField.invalid || compactField.invalid || stitchField.invalid
            text: anyInvalid ? qsTr("Valeur ignorée : le défaut est utilisé. Saisissez un nombre positif (couverture : 0 à 100 %).")
                             : qsTr("Vide : valeur par défaut. Le point le plus long ne s'applique qu'avec « meilleur satin ».")
            wrapMode: Text.WordWrap
            color: anyInvalid ? Theme.warning : Theme.muted; font.pixelSize: Theme.smallSize; font.family: Theme.fontFamily
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
