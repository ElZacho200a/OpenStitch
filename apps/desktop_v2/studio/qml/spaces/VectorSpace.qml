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
            // Fenêtre basse : le rail défile dans sa carte au lieu de déborder sur la barre d'état.
            Flickable {
                objectName: "vectorRailScroll"
                anchors.fill: parent
                anchors.topMargin: 8
                anchors.bottomMargin: 8
                clip: true
                contentWidth: width
                contentHeight: vectorRail.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                interactive: contentHeight > height
                Controls.ScrollBar.vertical: Controls.ScrollBar {
                    policy: vectorRailScroll.interactive ? Controls.ScrollBar.AlwaysOn : Controls.ScrollBar.AlwaysOff
                }
                id: vectorRailScroll
                ToolRail {
                    id: vectorRail
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: root.height < 620 ? 2 : 6
                    canvas: spaceCanvas
                    workspaceActive: root.active
                }
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
                CanvasContextMenu { id: contextMenu; shapeScope: true }

                ToolOverlay {
                    objectName: "toolOverlay"
                    anchors.fill: parent
                    gpu: spaceCanvas
                }

                // Frontières des fondus de la zone principale (le fondu édité mis en évidence).
                BoundaryBlendFrontiers {
                    objectName: "boundaryBlendFrontiers"
                    anchors.fill: parent
                    gpu: spaceCanvas
                    controller: typeof boundaryBlend !== "undefined" ? boundaryBlend : null
                }

                NeedleOriginMarker {
                    objectName: "needleOriginMarker"
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

        DockPanel {
            id: vectorSideCard
            objectName: "vectorSideCard"
            title: qsTr("Vecteurs")
            spaceId: root.spaceId
            floatLayer: root
            // LayerPanel/ObjectTree et vectorActionsScroll gèrent déjà leur propre défilement avec
            // Layout.fillHeight : il leur faut la vraie hauteur du panneau, pas un second scroll.
            scrollBody: false
            Layout.preferredWidth: 320
            Layout.fillHeight: true
            // Replié, le panneau garde sa place en haut de la colonne au lieu de se centrer.
            Layout.alignment: Qt.AlignTop
            applyHomeAnchors: function () {
                vectorSideCard.Layout.preferredWidth = 320
                vectorSideCard.Layout.fillHeight = true
            }
            ColumnLayout {
                id: sideColumn
                Layout.fillWidth: true
                Layout.fillHeight: true
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
                // Le panneau de forme défile dans au plus 55 % de la carte : sans cela il repoussait
                // l'onglet Calques et la liste hors de la fenêtre (rognés, sur la barre d'état).
                Controls.ScrollView {
                    id: shapeScroll
                    objectName: "vectorShapeScroll"
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(shapePanel.implicitHeight + 2 * Theme.panelPadding,
                                                     sideColumn.height * (shapePanel.veryCompact ? 0.5 : 0.55))
                    clip: true
                    contentWidth: availableWidth
                    Controls.ScrollBar.horizontal.policy: Controls.ScrollBar.AlwaysOff
                    // Barre visible dès que le panneau dépasse : une coupure se lit comme du défilement.
                    Controls.ScrollBar.vertical.policy: shapePanel.implicitHeight + 2 * Theme.panelPadding > shapeScroll.height
                                                        ? Controls.ScrollBar.AlwaysOn : Controls.ScrollBar.AlwaysOff
                    VectorShapePanel {
                        id: shapePanel
                        x: Theme.panelPadding
                        y: Theme.panelPadding
                        width: shapeScroll.availableWidth - 2 * Theme.panelPadding
                        compact: root.height < 860
                        veryCompact: root.height < 620
                        showSelection: !veryCompact
                        stage: root.stage
                        workspace: root.workspace
                        onGoToRegionRequested: {
                            // Sélection partagée : la région d'origine, puis bascule d'espace.
                            if (root.stage.selectOriginRegion())
                                root.workspace.setCurrent("segmentation")
                        }
                    }
                }
                // Très petite fenêtre : forme sélectionnée en une ligne fixe, jamais coupée.
                RowLayout {
                    objectName: "vectorCompactSelection"
                    visible: shapePanel.veryCompact
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.panelPadding
                    Layout.rightMargin: Theme.panelPadding
                    Layout.bottomMargin: 4
                    spacing: 8
                    readonly property var shape: root.stage ? root.stage.selectedShape : ({})
                    Text {
                        Layout.fillWidth: true
                        text: parent.shape.id === undefined
                              ? qsTr("Cliquez une forme pour voir son origine.")
                              : (parent.shape.name.length > 0 ? parent.shape.name : qsTr("Forme"))
                                + qsTr(" · %1 nœud(s)").arg(parent.shape.nodes)
                        elide: Text.ElideRight
                        color: Theme.muted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.smallSize
                    }
                    StudioButton {
                        objectName: "vectorCompactOriginButton"
                        text: qsTr("Origine")
                        implicitHeight: 28
                        tooltip: qsTr("Ouvre l'espace Segmentation sur la région d'origine de la forme")
                        enabled: parent.shape.id !== undefined && parent.shape.hasSource === true
                                 && parent.shape.regionExists === true
                        onClicked: if (root.stage.selectOriginRegion()) root.workspace.setCurrent("segmentation")
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
                    // Fenêtre basse : rembourrage réduit, la liste garde sa place malgré l'en-tête
                    // du panneau déplaçable.
                    Layout.preferredHeight: Theme.controlHeight + (shapePanel.compact ? 4 : 12)
                    color: "transparent"
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 6
                        StudioButton {
                            objectName: "vectorLayersTab"
                            Layout.fillWidth: true
                            text: qsTr("Calques et objets")
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
                    clip: true // la liste des calques ne déborde plus de la carte
                    currentIndex: vectorInspectorTabs.currentIndex

                    LayerPanel {
                        objectName: "layerPanel"
                        compactActions: root.height < 620
                        shapeScope: true
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
                                shapeScope: true
                                canvas: spaceCanvas
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
