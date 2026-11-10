// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
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
    property int paletteColors: 8
    property int paletteDistance: 60
    property bool paletteMergeAdjacent: true
    property var strokePoints: []

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
            // Pixel de l'image de travail sous un point de la zone.
            function pixelAt(px, py) {
                // La miniature est bornée à 512 px : les clics restent exprimés
                // dans les coordonnées de la vraie segmentation.
                var ox = (width - paintedWidth) / 2
                var oy = (height - paintedHeight) / 2
                return {
                    x: Math.floor((px - ox) / Math.max(1, paintedWidth) * regionTools.mapWidth),
                    y: Math.floor((py - oy) / Math.max(1, paintedHeight) * regionTools.mapHeight)
                }
            }
        }
        MouseArea {
            objectName: "regionMapArea"
            anchors.fill: parent
            enabled: regionTools.hasSegmentation
            onPressed: function(mouse) {
                var p = mapImage.pixelAt(mouse.x, mouse.y)
                if (regionTools.tool === "select") {
                    regionTools.selectAtPixel(p.x, p.y, (mouse.modifiers & Qt.ControlModifier) !== 0)
                } else {
                    section.strokePoints = [p]
                }
            }
            onPositionChanged: function(mouse) {
                if (regionTools.tool !== "select" && pressed) {
                    var pts = section.strokePoints
                    pts.push(mapImage.pixelAt(mouse.x, mouse.y))
                    section.strokePoints = pts
                }
            }
            onReleased: {
                if (regionTools.tool !== "select" && section.strokePoints.length > 0) {
                    regionTools.paintStroke(section.strokePoints, regionTools.tool === "erase")
                    section.strokePoints = []
                }
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
    RowLayout {
        Layout.fillWidth: true
        StudioTextField {
            id: colorField
            objectName: "regionColorField"
            Layout.fillWidth: true
            placeholderText: "#rrggbb"
            text: "#cc3344" // lint-ok: default thread colour (document data)
        }
        StudioButton {
            objectName: "regionRecolor"
            text: qsTr("Recolorer")
            enabled: regionTools.selectedCount > 0
            onClicked: regionTools.recolorSelected(colorField.text)
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
