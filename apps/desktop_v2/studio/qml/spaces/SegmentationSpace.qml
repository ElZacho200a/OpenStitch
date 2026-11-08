// SPDX-License-Identifier: Apache-2.0
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import OpenStitch.Studio 1.0
import ".."
import "segmentation"

// Espace « Segmentation » (docs/desktop-v2/23-espaces-studio.md) : les régions de la segmentation
// colorées sur l'image (couche « regions » du canevas), sélection au clic synchronisée avec la
// liste, panneaux existants (segmentation, régions, IA), état d'étape publié à l'hôte.
// Aucune logique métier ici : segmentationSpace (adaptateur), regionTools, imageTools.
Item {
    id: root
    objectName: "segmentationSpace"
    required property var workspace
    property bool active: false
    readonly property string spaceId: "segmentation"
    readonly property var canvas: spaceCanvas
    readonly property var seg: typeof segmentationSpace !== "undefined" ? segmentationSpace : null
    readonly property bool hasTools: typeof imageTools !== "undefined" && typeof regionTools !== "undefined"
    property bool showSource: true

    // Couches dessinées par CE canevas : image dessous (réalisée par l'espace Image), régions.
    readonly property var canvasLayers: ({ sourceImage: root.showSource, regions: true,
                                           stitches: false, selectionOverlays: false })

    // --- état publié et action « mettre à jour » du bandeau -----------------------------------
    function publish() {
        if (!root.active || !root.workspace || !root.seg) {
            return
        }
        if (root.seg.stageState.length > 0) {
            root.workspace.setStageStatus(root.spaceId, root.seg.stageState, root.seg.stageDetail)
        } else {
            root.workspace.clearStageStatus(root.spaceId)
        }
        root.workspace.setStageAction(root.spaceId, qsTr("Segmenter l'image"), root.seg.canSegment)
    }
    function segmentNow() {
        if (root.seg) {
            root.seg.segmentImage(panelLoader.item ? panelLoader.item.segmentationOptions : ({}))
        }
    }
    function activate() {
        if (root.seg) root.seg.attach(spaceCanvas)
        root.publish()
    }
    function deactivate() {
        if (root.seg) root.seg.detach()
        if (root.workspace) {
            root.workspace.clearStageStatus(root.spaceId)
            root.workspace.setStageAction(root.spaceId, "", false)
        }
    }
    onActiveChanged: { if (active) activate(); else deactivate() }
    Component.onCompleted: { if (active) activate() }
    Component.onDestruction: deactivate()
    Connections {
        target: root.seg
        function onStageChanged() { root.publish() }
    }
    Connections {
        target: root.workspace
        // L'espace courant exécute la mise à jour de SON étape (une seule étape d'annulation).
        function onUpdateRequested(id) {
            if (root.active && id === root.spaceId) {
                root.segmentNow()
            }
        }
    }

    Item {
        id: viewport
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: panelLoader.active ? panelLoader.left : parent.right
        clip: true

        GpuCanvas {
            id: spaceCanvas
            objectName: "segmentationCanvas"
            anchors.fill: parent
            controller: studio
            editor: root.editorController
            tools: root.toolController
            layers: root.canvasLayers
            focus: true
        }

        // Clic : sélection de la région sous le curseur (Ctrl = multi-sélection) ; pinceau / gomme
        // des régions quand l'outil de RegionController l'est. Les autres boutons (déplacement,
        // molette) restent au canevas.
        MouseArea {
            id: pick
            objectName: "segmentationPickArea"
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            enabled: root.seg !== null
            property var stroke: []
            readonly property bool painting: root.hasTools && regionTools.tool !== "select"
            function scenePoint(mx, my) {
                const v = spaceCanvas.viewState()
                return Qt.point(v.centerX + (mx - width / 2) / v.zoom,
                                v.centerY + (my - height / 2) / v.zoom)
            }
            onPressed: function(mouse) {
                const p = pick.scenePoint(mouse.x, mouse.y)
                if (pick.painting) {
                    const px = root.seg.pixelAt(p.x, p.y)
                    pick.stroke = px.valid ? [{ x: px.x, y: px.y }] : []
                } else {
                    root.seg.pickAt(p.x, p.y, (mouse.modifiers & Qt.ControlModifier) !== 0)
                }
            }
            onPositionChanged: function(mouse) {
                if (pick.painting && pick.pressed) {
                    const p = pick.scenePoint(mouse.x, mouse.y)
                    const px = root.seg.pixelAt(p.x, p.y)
                    if (px.valid) {
                        const pts = pick.stroke
                        pts.push({ x: px.x, y: px.y })
                        pick.stroke = pts
                    }
                }
            }
            onReleased: {
                if (pick.painting && pick.stroke.length > 0) {
                    regionTools.paintStroke(pick.stroke, regionTools.tool === "erase")
                }
                pick.stroke = []
            }
        }

        Rectangle {
            objectName: "segmentationEmpty"
            anchors.centerIn: parent
            visible: root.seg !== null && !root.seg.hasSegmentation
            width: Math.min(380, parent.width - 64)
            height: emptyColumn.implicitHeight + 36
            radius: Theme.radius
            color: Theme.surface
            border.color: Theme.border
            ColumnLayout {
                id: emptyColumn
                anchors.centerIn: parent
                width: parent.width - 36
                spacing: 10
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Pas encore de segmentation.\nSegmentez l'image pour découper ses couleurs en régions.")
                    color: Theme.muted
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    lineHeight: 1.35
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.bodySize
                }
                StudioButton {
                    objectName: "segmentEmptyButton"
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Segmenter l'image")
                    emphasized: true
                    enabled: root.seg !== null && root.seg.canSegment
                    onClicked: root.segmentNow()
                }
            }
        }

        Loader {
            id: controlsLoader
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: 12
            active: root.seg !== null
            sourceComponent: RegionLayerControls {
                seg: root.seg
                showSource: root.showSource
                onShowSourceChanged: root.showSource = showSource
                onSegmentRequested: root.segmentNow()
            }
        }

        ViewPanel {
            objectName: "viewHub"
            anchors.fill: parent
            canvasItem: spaceCanvas
        }
    }

    Loader {
        id: panelLoader
        objectName: "segmentationPanelsLoader"
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 340
        active: root.hasTools
        sourceComponent: SegmentationPanels {}
    }

    readonly property var editorController: editor
    readonly property var toolController: typeof tools !== "undefined" ? tools : null
}
