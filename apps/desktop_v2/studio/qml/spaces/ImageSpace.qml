// SPDX-License-Identifier: Apache-2.0
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import OpenStitch.Studio 1.0
import ".."
import "image"

// Espace « Image » : traitement de l'image UNIQUEMENT (prétraitements non destructifs, avant /
// après, pile d'opérations, recadrage). La segmentation, les régions et l'IA vivent dans l'espace
// « Segmentation ». Le canevas montre l'image de travail (couche `sourceImage`) ; le document
// garde l'original intact + la pile `ops` rejouée. Contrat : docs/desktop-v2/23-espaces-studio.md.
Item {
    id: root
    objectName: "imageSpace"
    required property var workspace
    property bool active: false
    readonly property string spaceId: "image"
    readonly property var canvas: spaceCanvas

    // Contrôleurs de contexte (absents dans certains bancs de test : l'espace reste inerte).
    readonly property var space: typeof imageSpace !== "undefined" ? imageSpace : null
    readonly property bool prepReady: typeof imageTools !== "undefined"
    readonly property bool hasImage: space ? space.hasImage : false
    readonly property var editorController: typeof editor !== "undefined" ? editor : null
    readonly property var toolController: typeof tools !== "undefined" ? tools : null
    property int viewTick: 0

    // Couches dessinées par CE canevas : l'image, rien d'autre.
    readonly property var canvasLayers: ({ sourceImage: true, stitches: false, selectionOverlays: false })

    // --- état publié ---------------------------------------------------------------------------
    // « stale » quand un traitement a invalidé la segmentation (le bandeau de l'espace Segmentation
    // l'affiche avec son action) ; sinon l'espace publie le détail de son étape.
    function publish() {
        if (!active || !space || !workspace) return
        workspace.setStageStatus(spaceId, space.stageState, space.stageDetail)
    }
    Connections {
        target: root.space
        function onChanged() { root.publish() }
    }
    function activate() {
        if (space) space.attachCanvas(spaceCanvas)
        publish()
    }
    function deactivate() {
        if (space) {
            space.detachCanvas()
            // Le statut « stale » doit survivre à une navigation ; les autres sont recalculés.
            if (workspace && !space.imageChanged) workspace.clearStageStatus(spaceId)
        }
    }
    onActiveChanged: { if (active) activate(); else deactivate() }
    Component.onCompleted: { if (active) activate() }
    Component.onDestruction: deactivate()

    // Barre latérale commune : recadrer, fond transparent (pipette), règle d'échelle. Dans un
    // document multi-calques (`.ora`), recadrage et échelle gardent les calques alignés ; la
    // pipette agit sur le calque actif.
    StudioCard {
        id: imageRailCard
        objectName: "imageRailCard"
        visible: root.hasImage && root.space !== null
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: Theme.gap
        width: Theme.controlHeight + 16
        height: Math.min(imageRail.implicitHeight + 16, root.height - 2 * Theme.gap)
        CanvasToolRail {
            id: imageRail
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: 8
            canvas: spaceCanvas
            workspaceActive: root.active
            toolPrefix: "imageTool_"
            fitTooltip: qsTr("Cadrer l'image dans la fenêtre")
            entries: [
                { id: "crop", icon: "crop", label: qsTr("Recadrer : dessiner un cadre sur l'image"), shortcut: "C", group: "geometrie" },
                { id: "measure", icon: "measure", label: qsTr("Règle : donner l'échelle d'après une longueur connue"), shortcut: "M", group: "geometrie" },
                { id: "pick", icon: "transparent", label: qsTr("Fond transparent : cliquer la couleur du fond"), shortcut: "Maj+I", group: "couleur" }
            ]
            activeTool: root.space ? root.space.tool : ""
            onToolTriggered: id => { if (root.space) root.space.tool = id }
            onFitRequested: if (root.space) root.space.fit()
        }
    }

    Item {
        id: viewport
        // Le canevas s'arrête au bord du panneau : l'image n'est jamais cachée dessous.
        anchors.left: imageRailCard.visible ? imageRailCard.right : parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: sidePanel.visible ? sidePanel.left : parent.right
        anchors.margins: Theme.gap
        clip: true

        GpuCanvas {
            id: spaceCanvas
            objectName: "imageCanvas"
            anchors.fill: parent
            controller: studio
            layers: root.canvasLayers
            focus: true
            onViewChanged: root.viewTick++
        }

        CompareCurtain {
            anchors.fill: parent
            space: root.space
            viewTick: root.viewTick
        }
        CropOverlay {
            anchors.fill: parent
            space: root.space
            viewTick: root.viewTick
        }
        ImageToolOverlay {
            anchors.fill: parent
            space: root.space
            viewTick: root.viewTick
        }

        // Barre d'affichage de l'image : avant / après / rideau (les outils sont dans la barre latérale).
        Rectangle {
            objectName: "imageToolbar"
            x: 0
            y: 0
            width: Math.min(parent.width, toolRow.implicitWidth + 16)
            clip: true
            height: Theme.controlHeight + 12
            radius: Theme.cardRadius
            color: Theme.panel
            border.color: Theme.divider
            visible: root.hasImage
            // Never clip actions silently: the toolbar remains scrollable on narrow canvases.
            Flickable {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                clip: true
                contentWidth: toolRow.implicitWidth
                contentHeight: height
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds

                RowLayout {
                    id: toolRow
                    height: parent.height
                    spacing: 4
                Repeater {
                    model: [
                        { mode: 0, label: qsTr("Après"), tip: qsTr("Image traitée") },
                        { mode: 1, label: qsTr("Avant"), tip: qsTr("Image d'origine") },
                        { mode: 2, label: qsTr("Rideau"), tip: qsTr("Rideau glissant avant / après") }
                    ]
                    delegate: StudioButton {
                        required property var modelData
                        objectName: "compareMode_" + modelData.mode
                        text: modelData.label
                        tooltip: modelData.tip
                        checkable: true
                        checked: root.space && root.space.compareMode === modelData.mode
                        onClicked: root.space.compareMode = modelData.mode
                    }
                }
                }
            }
        }

        // Aucune image : invitation à importer (le menu Fichier et la palette restent disponibles).
        Rectangle {
            objectName: "imageEmptyState"
            visible: !root.hasImage
            anchors.centerIn: parent
            width: Math.min(420, parent.width - 64)
            height: emptyColumn.implicitHeight + 36
            radius: Theme.cardRadius
            color: Theme.panel
            border.color: Theme.divider
            ColumnLayout {
                id: emptyColumn
                anchors.centerIn: parent
                width: parent.width - 36
                spacing: 10
                StudioLabel {
                    text: qsTr("Aucune image source.\nImportez une image : ses traitements (luminosité, netteté, recadrage…) se règlent ici.")
                    horizontalAlignment: Text.AlignHCenter
                    Layout.fillWidth: true
                }
                StudioButton {
                    objectName: "imageImportButton"
                    text: qsTr("Importer une image…")
                    emphasized: true
                    Layout.alignment: Qt.AlignHCenter
                    onClicked: if (typeof actions !== "undefined") actions.runCommand("cmd.file.import-image")
                }
            }
        }

        ViewPanel {


            active: root.active
            objectName: "viewHub"
            anchors.fill: parent
            canvasItem: spaceCanvas
        }

    }

    // Panneau de l'espace : prétraitements (ImagePrepSection existant) + pile d'opérations.
    StudioCard {
        id: sidePanel
        objectName: "imageSidePanel"
        visible: root.hasImage && root.prepReady
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: Theme.gap
        width: Math.min(340, Math.max(180, root.width * 0.38))
        Controls.ScrollView {
            anchors.fill: parent
            anchors.margins: 8
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: Math.max(0, sidePanel.width - 28)
                StudioComboBox {
                    objectName: "imageLayerSelector"
                    enabled: !studio.busy
                    Layout.fillWidth: true
                    // Un seul calque : rien à choisir (même règle que les autres espaces).
                    visible: root.prepReady && imageTools.imageLayers.length > 1
                    model: root.prepReady ? imageTools.imageLayers : []
                    currentIndex: root.prepReady ? imageTools.imageLayerIndex : -1
                    Accessible.name: qsTr("Calque image actif")
                    onActivated: index => imageTools.selectImageLayer(index)
                }
                spacing: 10
                ImageSizeSection {
                    Layout.fillWidth: true
                    space: root.space
                }
                Loader {
                    Layout.fillWidth: true
                    active: root.prepReady
                    sourceComponent: ImagePrepSection {
                        showThumbnails: false
                        showHistory: false
                    }
                }
                ImageOpStack {
                    Layout.fillWidth: true
                    space: root.space
                }
            }
        }
    }

    // Appelé par le Shortcut « Escape » de Main.qml : vrai si un outil modal a été quitté.
    function cancelTool() {
        if (!root.active || !root.space || root.space.tool === "") return false
        root.space.tool = ""
        return true
    }
}
