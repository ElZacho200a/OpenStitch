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
        if (!root.active || !root.stage)
            return
        root.workspace.setStageStatus(root.spaceId, root.stage.stageState, root.stage.stageDetail)
        root.workspace.setStageAction(root.spaceId, qsTr("Mettre à jour depuis la segmentation"),
                                      root.stage.stageState !== "blocked")
    }
    function syncDirty() {
        if (root.stage)
            root.stage.setSegmentationDirty(root.workspace.segmentationDirty, root.workspace.dirtyReason)
    }

    function activate() {
        if (root.stage) {
            root.stage.attach(spaceCanvas)
            root.syncDirty()
        }
        root.publish()
    }
    function deactivate() {
        if (root.stage) root.stage.detach()
        root.workspace.clearStageStatus(root.spaceId)
        root.workspace.setStageAction(root.spaceId, "", false)
    }
    onActiveChanged: { if (active) activate(); else deactivate() }
    Component.onCompleted: { if (active) activate() }
    Component.onDestruction: deactivate()

    Connections {
        target: root.stage
        ignoreUnknownSignals: true
        function onLayerChanged() { root.publish() }
    }
    // L'outil nœuds a ses réglages dans « Édition » : y basculer au lieu de les laisser cachés.
    Connections {
        target: root.toolController
        ignoreUnknownSignals: true
        function onActiveToolChanged() {
            if (root.active && root.toolController.activeTool === "node.edit")
                vectorInspectorTabs.currentIndex = 1
        }
    }
    Connections {
        target: root.workspace
        ignoreUnknownSignals: true
        function onStatesChanged() { root.syncDirty() }
        function onSpacesChanged() { root.syncDirty() }
        // L'espace courant exécute la mise à jour de SON étape (une seule étape d'annulation).
        function onUpdateRequested(id) {
            if (root.active && id === root.spaceId && root.stage)
                shapePanel.askUpdate()
        }
    }

    // Même habillage que l'espace Points : cartes flottantes sur le plan de travail.
    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.gap
        spacing: Theme.gap

        StudioCard {
            objectName: "vectorRailCard"
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: Theme.controlHeight + 16
            Layout.preferredHeight: Math.min(vectorRail.implicitHeight + 16, root.height - 2 * Theme.gap)
            ToolRail {
                id: vectorRail
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.topMargin: 8
                canvas: spaceCanvas
                workspaceActive: root.active
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 34
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
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
                    onContextMenuRequested: function(x, y, objectId) { contextMenu.openAt(x, y) }
                }

                // Clic droit : mêmes actions de sélection que dans l'espace Points.
                CanvasContextMenu { id: contextMenu }

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

                VectorQuickBar {
                    anchors.top: parent.top
                    anchors.topMargin: 8
                    anchors.horizontalCenter: parent.horizontalCenter
                    spaceActive: root.active
                    z: 5
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


                    active: root.active
                    objectName: "viewHub"
                    anchors.fill: parent
                    canvasItem: spaceCanvas
                }
            }
        }

        StudioCard {
            objectName: "vectorSideCard"
            Layout.preferredWidth: 320
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 2
                spacing: 0
                StudioComboBox {
                    objectName: "vectorImageLayerSelector"
                    enabled: !studio.busy
                    Layout.fillWidth: true
                    Layout.margins: Theme.panelPadding
                    readonly property bool ready: typeof imageTools !== "undefined"
                    visible: ready && imageTools.imageLayers.length > 1
                    model: ready ? imageTools.imageLayers : []
                    currentIndex: ready ? imageTools.imageLayerIndex : -1
                    Accessible.name: qsTr("Calque à vectoriser")
                    onActivated: index => imageTools.selectImageLayer(index)
                }
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
                Rectangle {
                    id: vectorInspectorTabs
                    objectName: "vectorInspectorTabs"
                    property int currentIndex: 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.controlHeight + 12
                    color: "transparent"
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 6
                        StudioButton {
                            objectName: "vectorLayersTab"
                            Layout.fillWidth: true
                            text: qsTr("Calques")
                            tooltip: qsTr("Afficher les calques et les objets")
                            checkable: true
                            checked: vectorInspectorTabs.currentIndex === 0
                            onClicked: vectorInspectorTabs.currentIndex = 0
                        }
                        StudioButton {
                            objectName: "vectorEditTab"
                            Layout.fillWidth: true
                            text: qsTr("Édition")
                            tooltip: qsTr("Afficher les opérations de sélection et d'édition")
                            checkable: true
                            checked: vectorInspectorTabs.currentIndex === 1
                            onClicked: vectorInspectorTabs.currentIndex = 1
                        }
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

                        // Options de l'outil et opérations sur les formes d'abord : ce sont
                        // les actions propres à cet espace ; l'EditBar générique suit.
                        ColumnLayout {
                            width: vectorActionsScroll.availableWidth
                            spacing: 8
                            ToolOptions {
                                visible: root.toolController !== null
                                Layout.fillWidth: true
                                Layout.leftMargin: 12
                                Layout.rightMargin: 12
                                Layout.topMargin: 12
                            }
                            EditBar {
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
