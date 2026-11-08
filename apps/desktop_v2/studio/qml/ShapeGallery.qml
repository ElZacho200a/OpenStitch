// SPDX-License-Identifier: Apache-2.0
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import "."

// CX04: catalogue de raccourcis vers les outils EXISTANTS. Aucun objet n'est créé ici :
// seul ToolController valide les gestes via DocumentSession (une opération annulable).
ColumnLayout {
    id: gallery
    objectName: "shapeGallery"
    property var toolCtl: typeof tools !== "undefined" ? tools : null
    spacing: 5
    visible: toolCtl !== null

    readonly property var presets: [
        { id: "rectangle", label: qsTr("Rectangle"), tool: "draw.rectangle" },
        { id: "ellipse", label: qsTr("Ellipse"), tool: "draw.ellipse" },
        { id: "polygon", label: qsTr("Polygone libre"), tool: "draw.polygon" },
        { id: "triangle", label: qsTr("Triangle"), tool: "draw.polygon-regular", sides: 3, star: false },
        { id: "pentagon", label: qsTr("Pentagone"), tool: "draw.polygon-regular", sides: 5, star: false },
        { id: "hexagon", label: qsTr("Hexagone"), tool: "draw.polygon-regular", sides: 6, star: false },
        { id: "star", label: qsTr("Étoile"), tool: "draw.polygon-regular", sides: 5, star: true }
    ]

    SectionLabel { text: qsTr("FORMES PRÉDÉFINIES") }

    Flow {
        Layout.fillWidth: true
        Layout.preferredHeight: childrenRect.height
        spacing: 5

        Repeater {
            model: gallery.presets
            delegate: StudioButton {
                required property var modelData
                objectName: "shapePreset_" + modelData.id
                text: modelData.label
                quiet: true
                implicitHeight: 28
                tooltip: qsTr("Choisir cette forme, puis la dessiner sur le canevas")
                active: gallery.toolCtl !== null
                        && gallery.toolCtl.activeTool === modelData.tool
                        && (modelData.sides === undefined
                            || (gallery.toolCtl.sides === modelData.sides
                                && gallery.toolCtl.star === modelData.star))
                onClicked: {
                    if (gallery.toolCtl === null)
                        return
                    if (modelData.sides !== undefined) {
                        gallery.toolCtl.sides = modelData.sides
                        gallery.toolCtl.star = modelData.star
                    }
                    gallery.toolCtl.activeTool = modelData.tool
                }
            }
        }
    }
}
