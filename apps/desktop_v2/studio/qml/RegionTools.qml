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

    property string confirmText: ""
    property bool pendingResegment: false
    property bool choosingMergeTarget: false
    function tappedRegion(id, ctrlHeld) {
        if (choosingMergeTarget) {
            if (regionTools.mergeSelectedInto(id) === "")
                choosingMergeTarget = false
        } else {
            regionTools.selectRegion(id, ctrlHeld ? "toggle" : "replace")
        }
    }
    // Sourced from the active SegmentationSection in Studio, not hardcoded defaults.
    property var segmentationOptions: imageTools.segmentationDefaults
    property var pendingOptions: ({})
    function requestRegeneration(resegment) {
        pendingResegment = resegment
        // Freeze the chosen settings while the overwrite confirmation is shown.
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
    property var strokePoints: []
    // A brush gesture is valid only if it begins inside the painted image and never
    // crosses the letterbox. Otherwise a later in-bounds move could paint unexpectedly.
    property bool strokeStartedInside: false

    Connections {
        target: regionTools
        function onConfirmRequested(question) { section.confirmText = question }
    }

    Text {
        objectName: "regionStaleBanner"
        Layout.fillWidth: true
        visible: regionTools.stale
        text: qsTr("Objets obsolètes : %1. Régénérez-les pour appliquer.").arg(regionTools.staleReason)
        wrapMode: Text.WordWrap
        color: Theme.accent; font.pixelSize: Theme.smallSize; font.family: Theme.fontFamily
    }

    SectionLabel { text: qsTr("CARTE DES RÉGIONS") }
    Rectangle {
        id: mapBox
        Layout.fillWidth: true
        Layout.preferredHeight: 170
        color: Theme.surface
        border.color: Theme.border
        Image {
            id: mapImage
            objectName: "regionMapImage"
            anchors.fill: parent
            anchors.margins: 2
            fillMode: Image.PreserveAspectFit
            cache: false
            source: regionTools.hasSegmentation ? "image://prepare/regions?rev=" + regionTools.mapRevision : ""
            // The ScrollView's MouseArea and the image have different origins (2 px margins).
            // Reject letterboxed clicks instead of feeding out-of-image coordinates to the
            // region brush. Convert only the painted image rectangle to source pixels.
            function pixelAt(px, py) {
                const p = mapImage.mapFromItem(mapArea, px, py)
                const w = paintedWidth
                const h = paintedHeight
                const sourceW = regionTools.mapWidth
                const sourceH = regionTools.mapHeight
                if (w <= 0 || h <= 0 || sourceW <= 0 || sourceH <= 0)
                    return null
                const ox = (width - w) / 2
                const oy = (height - h) / 2
                if (p.x < ox || p.y < oy || p.x >= ox + w || p.y >= oy + h)
                    return null
                return {
                    x: Math.min(sourceW - 1, Math.floor((p.x - ox) / w * sourceW)),
                    y: Math.min(sourceH - 1, Math.floor((p.y - oy) / h * sourceH))
                }
            }
        }
        MouseArea {
            id: mapArea
            objectName: "regionMapArea"
            anchors.fill: parent
            enabled: regionTools.hasSegmentation
            onPressed: function(mouse) {
                const p = mapImage.pixelAt(mouse.x, mouse.y)
                if (regionTools.tool === "select") {
                    section.strokeStartedInside = false
                    if (p)
                        regionTools.selectAtPixel(p.x, p.y, (mouse.modifiers & Qt.ControlModifier) !== 0)
                    else if (!(mouse.modifiers & Qt.ControlModifier))
                        regionTools.clearSelection()
                } else {
                    section.strokeStartedInside = p !== null
                    section.strokePoints = p ? [p] : []
                }
            }
            onPositionChanged: function(mouse) {
                if (regionTools.tool !== "select" && pressed && section.strokeStartedInside) {
                    const p = mapImage.pixelAt(mouse.x, mouse.y)
                    if (!p) {
                        // Cancel the entire gesture rather than bridging an empty margin.
                        section.strokeStartedInside = false
                        section.strokePoints = []
                        return
                    }
                    if (p) {
                        const pts = section.strokePoints
                        if (pts.length === 0 || pts[pts.length - 1].x !== p.x ||
                                pts[pts.length - 1].y !== p.y)
                            section.strokePoints = pts.concat([p])
                    }
                }
            }
            onReleased: function(mouse) {
                // A release can arrive outside the painted image without a preceding
                // positionChanged event (e.g. a fast drag). Never commit that stroke.
                const releasePixel = mapImage.pixelAt(mouse.x, mouse.y)
                if (regionTools.tool !== "select" && section.strokeStartedInside &&
                        section.strokePoints.length > 0 && releasePixel !== null) {
                    // A quick release may arrive without a final positionChanged event.
                    // Include the release pixel so the last brush dab is not lost.
                    const pts = section.strokePoints
                    const last = pts[pts.length - 1]
                    if (last.x !== releasePixel.x || last.y !== releasePixel.y)
                        section.strokePoints = pts.concat([releasePixel])
                    regionTools.paintStroke(section.strokePoints, regionTools.tool === "erase")
                }
                section.strokeStartedInside = false
                section.strokePoints = []
            }
            onCanceled: {
                section.strokeStartedInside = false
                section.strokePoints = []
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Repeater {
            model: [ { id: "select", label: qsTr("Sélection") }, { id: "brush", label: qsTr("Pinceau") },
                     { id: "erase", label: qsTr("Gomme") } ]
            delegate: StudioButton {
                required property var modelData
                objectName: "regionTool_" + modelData.id
                text: modelData.label
                checkable: true
                checked: regionTools.tool === modelData.id
                onClicked: regionTools.tool = modelData.id
            }
        }
        StudioSpinBox {
            objectName: "regionBrushRadius"
            from: 1; to: 200; value: regionTools.brushRadius; editable: true
            visible: regionTools.tool !== "select"
            onValueModified: regionTools.brushRadius = value
        }
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
        StudioButton {
            objectName: "brushSmaller"
            text: "−"
            tooltip: qsTr("Pinceau : rayon plus petit")
            visible: regionTools.tool !== "select"
            onClicked: regionTools.stepBrushRadius(-1)
        }
        StudioButton {
            objectName: "brushLarger"
            text: "+"
            tooltip: qsTr("Pinceau : rayon plus grand")
            visible: regionTools.tool !== "select"
            onClicked: regionTools.stepBrushRadius(1)
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
        StudioButton { objectName: "regionInvert"; text: qsTr("Inverser"); enabled: regionTools.hasSegmentation; onClicked: regionTools.invertSelection() }
        StudioButton { objectName: "regionClear"; text: qsTr("Aucune"); quiet: true; onClicked: regionTools.clearSelection() }
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
            tooltip: qsTr("Sélectionnez une région source puis cliquez la région à conserver dans la liste")
            onClicked: section.choosingMergeTarget = !section.choosingMergeTarget
        }
        StudioButton { objectName: "regionDelete"; text: qsTr("Supprimer"); enabled: regionTools.selectedCount > 0; onClicked: regionTools.deleteSelected() }
    }
    Text {
        objectName: "regionMergeTargetHint"
        Layout.fillWidth: true
        visible: section.choosingMergeTarget
        text: qsTr("Cliquez dans la liste sur la région qui doit absorber la sélection ; annulez avec le même bouton.")
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
                    onClicked: {
                        section.confirmText = ""
                        regionTools.regenerate(section.pendingResegment, true, section.pendingOptions)
                    }
                }
                StudioButton {
                    objectName: "regionConfirmNo"
                    text: qsTr("Garder mes retouches")
                    onClicked: section.confirmText = ""
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
