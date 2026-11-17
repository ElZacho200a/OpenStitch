// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "."

// Édition des régions de la segmentation : sélection, recoloration, fusion, suppression,
// nettoyage, palette, pinceau/gomme et régénération des objets. Logique : RegionController.
ColumnLayout {
    id: section
    objectName: "regionTools"
    spacing: 8

    // La question et la demande en attente vivent dans RegionController : la carte du canevas et
    // ce panneau confirment donc la même action, avec les mêmes réglages.
    readonly property string confirmText: regionTools.confirmText
    // Mode « choisir la cible » partagé avec la carte et le canevas (RegionController).
    readonly property bool choosingMergeTarget: regionTools.mergeTargetMode
    function tappedRegion(id, ctrlHeld) {
        if (choosingMergeTarget) {
            regionTools.mergeSelectedInto(id)
        } else {
            regionTools.selectRegion(id, ctrlHeld ? "toggle" : "replace")
        }
    }
    // Sourced from the active SegmentationSection in Studio, not hardcoded defaults.
    property var segmentationOptions: imageTools.segmentationDefaults
    // Copie des réglages envoyés avec la dernière demande (diagnostic et tests).
    property var pendingOptions: ({})
    function requestRegeneration(resegment) {
        // RegionController freezes these settings while the overwrite confirmation is shown.
        pendingOptions = Object.assign({}, segmentationOptions)
        regionTools.regenerate(resegment, false, pendingOptions)
    }
    property int islandPx: 12
    property int holePx: 12
    function validHexColour(value) {
        return /^#[0-9a-fA-F]{6}$/.test(String(value).trim())
    }
    property int paletteColors: 8
    property int paletteDistance: 60
    property bool paletteMergeAdjacent: true

    // Palette du projet : regroupe les régions par couleur EXACTE réellement stockée. Depuis que
    // segmentImageSource() (apps/desktop_v2/core/segmentation_preview.cpp) recolore automatiquement
    // au plus « Couleurs maximum » teintes après application (plan_reduce_colors +
    // apply_color_merge, le même mécanisme que « Réduire » ci-dessous), ce nombre de lignes est
    // censé correspondre au réglage de segmentation — s'il grimpe bien au-delà, c'est un vrai
    // bruit de retouches manuelles (pinceau, îlots…), pas un artefact d'affichage : inutile de le
    // masquer par un regroupement perceptuel cosmétique, « Réduire » plus bas le corrige pour de
    // vrai sur le document. Triée par surface décroissante : les teintes dominantes d'abord.
    function buildColorClasses(regions) {
        const byColor = {}
        const order = []
        for (let i = 0; i < regions.length; ++i) {
            const r = regions[i]
            if (!(r.color in byColor)) {
                byColor[r.color] = { color: r.color, ids: [], pixels: 0 }
                order.push(r.color)
            }
            byColor[r.color].ids.push(r.id)
            byColor[r.color].pixels += r.pixels
        }
        const list = order.map(c => byColor[c])
        list.sort((a, b) => b.pixels - a.pixels)
        return list
    }
    readonly property var colorClasses: section.buildColorClasses(regionTools.regions)
    // "" = pas de fusion de teinte en cours ; sinon la teinte source en attente d'une cible.
    property string colorMergeSource: ""
    function recolorClass(cls, hex) {
        if (!cls || cls.ids.length === 0)
            return
        regionTools.selectRegion(cls.ids[0], "replace")
        for (let i = 1; i < cls.ids.length; ++i)
            regionTools.selectRegion(cls.ids[i], "add")
        regionTools.recolorSelected(hex)
    }
    // « Fusionner » deux teintes du panneau = les ramener à une seule couleur (celle de la
    // cible) : c'est la fusion de palette visible ici, par opposition à la fusion géométrique de
    // régions voisines (boutons de la section RETOUCHES) ou à la réduction automatique en masse
    // (« Réduire » plus bas, qui vise un NOMBRE de couleurs plutôt qu'une paire choisie à la main).
    function mergeClassInto(sourceColor, targetColor) {
        const classes = section.colorClasses
        let source = null
        let target = null
        for (let i = 0; i < classes.length; ++i) {
            if (classes[i].color === sourceColor) source = classes[i]
            if (classes[i].color === targetColor) target = classes[i]
        }
        if (source && target)
            section.recolorClass(source, target.color)
        section.colorMergeSource = ""
    }

    Connections {
        target: regionTools
        // Le champ de couleur part de la couleur de la région choisie : « Recolorer » retouche une
        // teinte au lieu de proposer une couleur sans rapport.
        function onSelectionChanged() {
            if (regionTools.selectedCount !== 1)
                return
            const id = regionTools.selected[0]
            const all = regionTools.regions
            for (let i = 0; i < all.length; ++i) {
                if (all[i].id === id) {
                    colorField.text = all[i].color
                    return
                }
            }
        }
    }

    Text {
        objectName: "regionStaleBanner"
        Layout.fillWidth: true
        visible: regionTools.stale
        text: qsTr("Objets obsolètes : %1. Régénérez-les pour appliquer.").arg(regionTools.staleReason)
        wrapMode: Text.WordWrap
        color: Theme.accent; font.pixelSize: Theme.smallSize; font.family: Theme.fontFamily
    }

    // Sélection, pinceau et gomme se font sur le canevas principal (SegmentationSpace.qml) :
    // une seule surface de dessin, pas de miniature redondante avec sa propre conversion de
    // coordonnées (ancienne « CARTE DES RÉGIONS », supprimée — doublon source de confusion).
    // Pinceau et gomme : barre latérale de l'espace (CanvasToolRail) ; rayon au canevas.
    StudioLabel {
        objectName: "regionToolHint"
        visible: regionTools.tool !== "select"
        Layout.fillWidth: true
        text: regionTools.tool === "brush"
              ? qsTr("Pinceau actif : peignez sur le canevas pour agrandir la région choisie.")
              : qsTr("Gomme active : peignez sur le canevas pour retirer de la région choisie.")
    }

    SectionLabel { text: qsTr("RÉGIONS (%1)").arg(regionTools.regions.length) }
    RowLayout {
        Layout.fillWidth: true
        StudioButton {
            objectName: "regionPrevious"
            text: qsTr("◀ Précédente")
            tooltip: qsTr("Sélectionner la région précédente de la liste")
            enabled: regionTools.regions.length > 0
            onClicked: regionTools.selectPreviousRegion()
        }
        StudioButton {
            objectName: "regionNext"
            text: qsTr("Suivante ▶")
            tooltip: qsTr("Sélectionner la région suivante de la liste")
            enabled: regionTools.regions.length > 0
            onClicked: regionTools.selectNextRegion()
        }
    }
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 110
        color: Theme.surface
        border.color: Theme.border
        ListView {
            objectName: "regionList"
            anchors.fill: parent
            anchors.margins: 2
            clip: true
            model: regionTools.regions
            delegate: Rectangle {
                required property var modelData
                objectName: "regionRow_" + modelData.id
                readonly property bool isSelected: regionTools.selected.indexOf(modelData.id) >= 0
                width: ListView.view.width
                height: 24
                color: isSelected ? Theme.accentSoft : "transparent"
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6
                    leftPadding: 4
                    Rectangle { width: 14; height: 14; color: modelData.color; border.color: Theme.border }
                    Text {
                        text: qsTr("Région %1 — %2 px").arg(modelData.id).arg(modelData.pixels)
                        color: Theme.foreground; font.pixelSize: Theme.smallSize; font.family: Theme.fontFamily
                    }
                }
                TapHandler {
                    onTapped: section.tappedRegion(modelData.id,
                              (point.modifiers & Qt.ControlModifier) !== 0)
                }
            }
        }
    }
    Flow {
        Layout.fillWidth: true
        spacing: 6
        StudioButton { objectName: "regionSameColor"; text: qsTr("Même couleur"); enabled: regionTools.selectedCount > 0; onClicked: regionTools.selectSameColor() }
        StudioButton { objectName: "regionNeighbours"; text: qsTr("Voisines"); enabled: regionTools.selectedCount > 0; onClicked: regionTools.selectNeighbours() }
        StudioButton { objectName: "regionSelectAll"; text: qsTr("Toutes"); enabled: regionTools.hasSegmentation; onClicked: regionTools.selectAll() }
        StudioButton { objectName: "regionInvert"; text: qsTr("Inverser"); enabled: regionTools.hasSegmentation; onClicked: regionTools.invertSelection() }
        StudioButton { objectName: "regionClear"; text: qsTr("Aucune"); quiet: true; enabled: regionTools.selectedCount > 0; onClicked: regionTools.clearSelection() }
    }

    SectionLabel { text: qsTr("PALETTE DU PROJET (%1 teinte(s))").arg(section.colorClasses.length) }
    Text {
        objectName: "paletteExactHint"
        Layout.fillWidth: true
        visible: regionTools.hasSegmentation
        text: qsTr("Couleurs réellement stockées (une ligne par teinte exacte). Normalement proche du réglage « Couleurs maximum » de la segmentation ; si ce nombre dérive après des retouches, utilisez « Réduire » plus bas pour le ramener à un nombre choisi.")
        wrapMode: Text.WordWrap
        color: Theme.muted
        font.pixelSize: Theme.smallSize
        font.family: Theme.fontFamily
    }
    ColorDialog {
        id: paletteColourPicker
        objectName: "paletteColorDialog"
        title: qsTr("Nouvelle couleur de cette teinte")
        property string recolorTarget: ""
        onAccepted: section.recolorClass(
            section.colorClasses.find(c => c.color === recolorTarget), selectedColor.toString())
    }
    Text {
        objectName: "paletteMergeHint"
        Layout.fillWidth: true
        visible: section.colorMergeSource.length > 0
        text: qsTr("Cliquez « Fusionner ici » sur la teinte qui doit absorber %1 ; cliquez « Annuler » pour renoncer.").arg(section.colorMergeSource)
        wrapMode: Text.WordWrap
        color: Theme.accent
        font.pixelSize: Theme.smallSize
        font.family: Theme.fontFamily
    }
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 150
        color: Theme.surface
        border.color: Theme.border
        ListView {
            objectName: "paletteList"
            anchors.fill: parent
            anchors.margins: 2
            clip: true
            model: section.colorClasses
            delegate: Rectangle {
                required property var modelData
                objectName: "paletteRow_" + modelData.color
                width: ListView.view.width
                height: 48
                color: Theme.surface
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 4
                    spacing: 6
                    Rectangle { width: 16; height: 16; color: modelData.color; border.color: Theme.border }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            Layout.fillWidth: true
                            text: modelData.color
                            color: Theme.foreground; font.pixelSize: Theme.smallSize; font.family: Theme.fontFamily
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("%1 région(s) · %2 px").arg(modelData.ids.length).arg(modelData.pixels)
                            color: Theme.muted; font.pixelSize: Theme.smallSize; font.family: Theme.fontFamily
                            elide: Text.ElideRight
                        }
                    }
                    StudioButton {
                        objectName: "paletteRecolor_" + modelData.color
                        text: qsTr("Recolorer")
                        onClicked: {
                            paletteColourPicker.recolorTarget = modelData.color
                            paletteColourPicker.selectedColor = modelData.color
                            paletteColourPicker.open()
                        }
                    }
                    StudioButton {
                        objectName: "paletteMerge_" + modelData.color
                        text: section.colorMergeSource === modelData.color ? qsTr("Annuler")
                              : section.colorMergeSource.length > 0 ? qsTr("Fusionner ici")
                              : qsTr("Fusionner dans…")
                        checkable: section.colorMergeSource === modelData.color
                        checked: section.colorMergeSource === modelData.color
                        enabled: section.colorClasses.length > 1
                        onClicked: {
                            if (section.colorMergeSource === modelData.color)
                                section.colorMergeSource = ""
                            else if (section.colorMergeSource.length > 0)
                                section.mergeClassInto(section.colorMergeSource, modelData.color)
                            else
                                section.colorMergeSource = modelData.color
                        }
                    }
                }
            }
        }
    }

    SectionLabel { text: qsTr("RETOUCHES") }
    ColorDialog {
        id: regionColourPicker
        objectName: "regionColorDialog"
        title: qsTr("Couleur de la région")
        onAccepted: colorField.text = selectedColor.toString()
    }
    RowLayout {
        Layout.fillWidth: true
        StudioTextField {
            id: colorField
            objectName: "regionColorField"
            Layout.fillWidth: true
            placeholderText: "#rrggbb"
            text: "#cc3344" // lint-ok: default thread colour (document data)
            invalid: !section.validHexColour(text)
            Accessible.name: qsTr("Couleur hexadécimale de la région")
        }
        StudioButton {
            objectName: "regionPickColor"
            text: qsTr("Palette…")
            tooltip: qsTr("Choisir visuellement la couleur d'une région")
            onClicked: {
                if (section.validHexColour(colorField.text))
                    regionColourPicker.selectedColor = colorField.text
                regionColourPicker.open()
            }
        }
        StudioButton {
            objectName: "regionRecolor"
            text: qsTr("Recolorer")
            enabled: regionTools.selectedCount > 0 && section.validHexColour(colorField.text)
            onClicked: regionTools.recolorSelected(colorField.text.trim())
        }
    }
    Flow {
        Layout.fillWidth: true
        spacing: 6
        StudioButton { objectName: "regionMerge"; text: qsTr("Fusionner la sélection"); enabled: regionTools.selectedCount > 1; onClicked: regionTools.mergeSelected() }
        StudioButton { objectName: "regionMergeNeighbour"; text: qsTr("Fusionner dans la voisine"); enabled: regionTools.selectedCount === 1
            onClicked: regionTools.mergeWithNeighbour(regionTools.selected[0]) }
        StudioButton {
            objectName: "regionChooseMergeTarget"
            text: section.choosingMergeTarget ? qsTr("Annuler cible") : qsTr("Choisir la cible…")
            checkable: true
            checked: section.choosingMergeTarget
            enabled: regionTools.selectedCount === 1 || section.choosingMergeTarget
            tooltip: qsTr("Sélectionnez une région source puis cliquez la région à conserver (liste ou canevas)")
            onClicked: regionTools.mergeTargetMode = !regionTools.mergeTargetMode
        }
        StudioButton { objectName: "regionDelete"; text: qsTr("Supprimer"); enabled: regionTools.selectedCount > 0; onClicked: regionTools.deleteSelected() }
    }
    Text {
        objectName: "regionMergeTargetHint"
        Layout.fillWidth: true
        visible: section.choosingMergeTarget
        text: qsTr("Cliquez la région qui doit absorber la sélection (liste ou canevas) ; annulez avec le même bouton.")
        wrapMode: Text.WordWrap
        color: Theme.accent
        font.pixelSize: Theme.smallSize
        font.family: Theme.fontFamily
    }
    RowLayout {
        Layout.fillWidth: true
        Text { text: qsTr("Îlots ≤"); color: Theme.foreground; font.pixelSize: Theme.bodySize; font.family: Theme.fontFamily }
        StudioSpinBox { objectName: "regionIslandPx"; from: 1; to: 100000; value: section.islandPx; editable: true; onValueModified: section.islandPx = value }
        StudioButton { objectName: "regionCleanIslands"; text: qsTr("Nettoyer"); enabled: regionTools.hasSegmentation; onClicked: regionTools.cleanIslands(section.islandPx) }
    }
    RowLayout {
        Layout.fillWidth: true
        Text { text: qsTr("Trous ≤"); color: Theme.foreground; font.pixelSize: Theme.bodySize; font.family: Theme.fontFamily }
        StudioSpinBox { objectName: "regionHolePx"; from: 1; to: 100000; value: section.holePx; editable: true; onValueModified: section.holePx = value }
        StudioButton { objectName: "regionFillHoles"; text: qsTr("Reboucher"); enabled: regionTools.hasSegmentation; onClicked: regionTools.fillHoles(section.holePx) }
    }
    // Réduction AUTOMATIQUE par distance de couleur : regroupe les teintes proches de la palette
    // ci-dessus en une seule passe, au lieu d'éditer chaque teinte une à une.
    RowLayout {
        Layout.fillWidth: true
        Text { text: qsTr("Palette ≤"); color: Theme.foreground; font.pixelSize: Theme.bodySize; font.family: Theme.fontFamily }
        StudioSpinBox { objectName: "regionPaletteColors"; from: 1; to: 64; value: section.paletteColors; editable: true; onValueModified: section.paletteColors = value }
        StudioButton { objectName: "regionReducePalette"; text: qsTr("Réduire"); enabled: regionTools.hasSegmentation && !regionTools.busy
            onClicked: regionTools.reducePalette(section.paletteColors, section.paletteDistance, section.paletteMergeAdjacent) }
    }
    RowLayout {
        Layout.fillWidth: true
        Text { text: qsTr("Distance couleurs"); color: Theme.foreground; font.pixelSize: Theme.smallSize; font.family: Theme.fontFamily }
        StudioSpinBox {
            id: paletteDistanceControl
            objectName: "regionPaletteDistance"
            from: 0; to: 255; stepSize: 5
            value: section.paletteDistance
            editable: true
            onValueModified: section.paletteDistance = value
            Accessible.name: qsTr("Distance maximale de fusion des couleurs")
        }
        StudioCheckBox {
            objectName: "regionMergeAdjacent"
            text: qsTr("Voisines")
            checked: section.paletteMergeAdjacent
            onToggled: section.paletteMergeAdjacent = checked
            Accessible.name: qsTr("Fusionner les régions de couleur voisines")
        }
    }
    Text {
        objectName: "regionPalettePreview"
        Layout.fillWidth: true
        readonly property var p: (regionTools.regions, regionTools.hasSegmentation) ? regionTools.previewReduction(section.paletteColors, section.paletteDistance, section.paletteMergeAdjacent) : ({ before: 0, after: 0 })
        text: qsTr("Palette : %1 → %2 couleurs").arg(p.before).arg(p.after)
        color: Theme.muted; font.pixelSize: Theme.smallSize; font.family: Theme.fontFamily
    }

    SectionLabel { text: qsTr("OBJETS") }
    RowLayout {
        Layout.fillWidth: true
        StudioButton {
            objectName: "regionRegenerate"
            text: qsTr("Régénérer les objets")
            emphasized: regionTools.stale
            enabled: regionTools.hasSegmentation && !regionTools.busy
            onClicked: section.requestRegeneration(false)
        }
        StudioButton {
            objectName: "regionResegment"
            text: qsTr("Re-segmenter")
            enabled: imageTools.hasImage && !regionTools.busy
            onClicked: section.requestRegeneration(true)
        }
        StudioButton {
            objectName: "regionCancelTask"
            text: qsTr("Annuler")
            quiet: true
            visible: regionTools.busy
            onClicked: regionTools.cancelTask()
        }
    }
    Rectangle {
        objectName: "regionConfirm"
        Layout.fillWidth: true
        visible: section.confirmText.length > 0
        implicitHeight: confirmColumn.implicitHeight + 16
        color: Theme.accentSoft
        border.color: Theme.accent
        ColumnLayout {
            id: confirmColumn
            anchors.fill: parent
            anchors.margins: 8
            Text {
                Layout.fillWidth: true
                text: section.confirmText
                wrapMode: Text.WordWrap
                color: Theme.foreground; font.pixelSize: Theme.smallSize; font.family: Theme.fontFamily
            }
            RowLayout {
                StudioButton {
                    objectName: "regionConfirmYes"
                    text: qsTr("Remplacer les retouches")
                    emphasized: true
                    onClicked: regionTools.confirmRegeneration()
                }
                StudioButton {
                    objectName: "regionConfirmNo"
                    text: qsTr("Garder mes retouches")
                    onClicked: regionTools.cancelConfirmation()
                }
            }
        }
    }
    Text {
        objectName: "regionMessage"
        Layout.fillWidth: true
        visible: regionTools.message.length > 0
        text: regionTools.message
        wrapMode: Text.WordWrap
        color: regionTools.messageIsError ? Theme.error : Theme.success
        font.pixelSize: Theme.smallSize; font.family: Theme.fontFamily
    }
}
