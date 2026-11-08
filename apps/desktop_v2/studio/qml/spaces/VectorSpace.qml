// SPDX-License-Identifier: Apache-2.0
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import OpenStitch.Studio 1.0
import ".."
import "vector"

// Espace « Vecteurs » : toutes les formes vectorielles générées depuis la segmentation (couche
// `vectors` du canevas), les outils de dessin et d'édition de formes (rail, nœuds, booléennes),
// le lien amont vers la région d'origine, l'état de l'étape et les actions « Mettre à jour depuis
// la segmentation » / « Convertir en broderie ». Contrat : docs/desktop-v2/23-espaces-studio.md.
Item {
    id: root
    objectName: "vectorSpace"
    required property var workspace
    property bool active: false
    readonly property string spaceId: "vector"
    readonly property var canvas: spaceCanvas
    readonly property var editorController: editor
    readonly property var toolController: typeof tools !== "undefined" ? tools : null
    readonly property var stage: typeof vectorStage !== "undefined" ? vectorStage : null

    // Couches dessinées par CE canevas : les formes, jamais les points.
    readonly property var canvasLayers: ({ vectors: true, stitches: false, selectionOverlays: false })

    // --- état publié et action « mettre à jour » du bandeau -----------------------------------
    function publish() {
        if (!root.stage)
            return
        root.workspace.setStageStatus(root.spaceId, root.stage.stageState, root.stage.stageDetail)
        root.workspace.setStageAction(root.spaceId, qsTr("Mettre à jour depuis la segmentation"),
                                      root.stage.stageState !== "blocked")
    }
    function syncDirty() {
        if (root.stage)
            root.stage.setSegmentationDirty(root.workspace.segmentationDirty, root.workspace.dirtyReason)
    }

    Component.onCompleted: {
        if (root.stage) {
            root.stage.attach(spaceCanvas)
            root.syncDirty()
        }
        root.publish()
    }
    Component.onDestruction: {
        if (root.stage)
            root.stage.detach()
        // L'état se déduit à nouveau du document (calcul de base) une fois l'espace quitté.
        root.workspace.clearStageStatus(root.spaceId)
        root.workspace.setStageAction(root.spaceId, "", false)
    }

    Connections {
        target: root.stage
        ignoreUnknownSignals: true
        function onLayerChanged() { root.publish() }
    }
    Connections {
        target: root.workspace
        ignoreUnknownSignals: true
        function onStatesChanged() { root.syncDirty() }
        function onSpacesChanged() { root.syncDirty() }
        // L'espace courant exécute la mise à jour de SON étape (une seule étape d'annulation).
        function onUpdateRequested(id) {
            if (id === root.spaceId && root.stage)
                shapePanel.askUpdate()
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: Theme.railWidth
            Layout.fillHeight: true
            color: Theme.panel
            ColumnLayout {
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.topMargin: 12
                spacing: 6
                StudioButton {
                    objectName: "vectorSelectToolButton"
                    iconName: "select"
                    tooltip: studio.selectionTool.label
                    shortcutLabel: studio.selectionTool.shortcut
                    quiet: true
                    active: !spaceCanvas.panMode && (!root.toolController || root.toolController.activeTool === "select")
                    onClicked: { spaceCanvas.panMode = false; if (root.toolController) root.toolController.activeTool = "select" }
                }
                StudioButton {
                    objectName: "vectorPanToolButton"
                    iconName: "pan"
                    tooltip: studio.panTool.label
                    shortcutLabel: studio.panTool.shortcut
                    quiet: true
                    active: spaceCanvas.panMode
                    onClicked: { spaceCanvas.panMode = true; if (root.toolController) root.toolController.activeTool = "select" }
                }
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 4
                    Layout.bottomMargin: 4
                    implicitWidth: 24
                    implicitHeight: 1
                    color: Theme.divider
                }
                ToolRail { Layout.alignment: Qt.AlignHCenter }
                Rectangle {
                    visible: root.toolController !== null
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 4
                    Layout.bottomMargin: 4
                    implicitWidth: 24
                    implicitHeight: 1
                    color: Theme.divider
                }
                StudioButton {
                    objectName: "vectorFitToolButton"
                    iconName: "fit"
                    tooltip: qsTr("Ajuster le motif à la fenêtre")
                    shortcutLabel: "F"
                    quiet: true
                    onClicked: spaceCanvas.fitDesign()
                }
            }
        }

        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: Theme.divider }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 43
                color: Theme.background
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 20
                    anchors.rightMargin: 20
                    SectionLabel { text: qsTr("FORMES VECTORIELLES") }
                    Item { Layout.fillWidth: true }
                    Text {
                        objectName: "vectorHeaderCount"
                        text: root.stage ? root.stage.stageDetail : ""
                        color: Theme.faint
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.smallSize
                    }
                }
            }

            Item {
                id: viewport
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                GpuCanvas {
                    id: spaceCanvas
                    objectName: "vectorCanvas"
                    anchors.fill: parent
                    controller: studio
                    editor: root.editorController
                    tools: root.toolController
                    layers: root.canvasLayers
                    focus: true
                }

                ToolOverlay {
                    objectName: "toolOverlay"
                    anchors.fill: parent
                    gpu: spaceCanvas
                }

                TransformHandles {
                    objectName: "transformHandles"
                    anchors.fill: parent
                    canvas: spaceCanvas
                }

                ColumnLayout {
                    objectName: "vectorEmpty"
                    anchors.centerIn: parent
                    width: Math.min(420, parent.width - 64)
                    spacing: 8
                    visible: root.stage !== null && root.stage.shapeCount === 0 && !studio.busy
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Aucune forme vectorielle.")
                        color: Theme.foreground
                        horizontalAlignment: Text.AlignHCenter
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.heroSize
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Segmentez l'image puis « Mettre à jour depuis la segmentation », ou dessinez une forme avec les outils.")
                        color: Theme.muted
                        horizontalAlignment: Text.AlignHCenter
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.bodySize
                        wrapMode: Text.WordWrap
                    }
                }

                ViewPanel {
                    objectName: "viewHub"
                    anchors.fill: parent
                    canvasItem: spaceCanvas
                }
            }
        }

        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: Theme.divider }

        Rectangle {
            Layout.preferredWidth: 320
            Layout.fillHeight: true
            color: Theme.panel
            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                VectorShapePanel {
                    id: shapePanel
                    Layout.fillWidth: true
                    Layout.margins: Theme.panelPadding
                    stage: root.stage
                    workspace: root.workspace
                    onGoToRegionRequested: {
                        // Sélection partagée : la région d'origine, puis bascule d'espace.
                        if (root.stage.selectOriginRegion())
                            root.workspace.setCurrent("segmentation")
                    }
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.divider }

                // A single inspector surface: layer management and editing actions no
                // longer compete for vertical space or obscure the object tree.
                Controls.TabBar {
                    id: vectorInspectorTabs
                    objectName: "vectorInspectorTabs"
                    Layout.fillWidth: true
                    currentIndex: 0
                    background: Rectangle { color: Theme.panel }
                    Controls.TabButton {
                        text: qsTr("Calques")
                        Accessible.name: qsTr("Afficher les calques et les objets")
                    }
                    Controls.TabButton {
                        text: qsTr("Édition")
                        Accessible.name: qsTr("Afficher les opérations de sélection et d'édition")
                    }
                }
                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 100
                    currentIndex: vectorInspectorTabs.currentIndex

                    LayerPanel {
                        objectName: "layerPanel"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                    }
                    Controls.ScrollView {
                        id: vectorActionsScroll
                        objectName: "vectorActionsScroll"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        contentWidth: availableWidth
                        Controls.ScrollBar.horizontal.policy: Controls.ScrollBar.AlwaysOff

                        ColumnLayout {
                            width: vectorActionsScroll.availableWidth
                            spacing: 8
                            EditBar {
                                Layout.fillWidth: true
                                Layout.leftMargin: 12
                                Layout.rightMargin: 12
                                Layout.topMargin: 12
                            }
                            ToolOptions {
                                visible: root.toolController !== null
                                Layout.fillWidth: true
                                Layout.leftMargin: 12
                                Layout.rightMargin: 12
                                Layout.bottomMargin: 12
                            }
                        }
                    }
                }
            }
        }
    }
}
