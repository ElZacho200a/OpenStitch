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

    Item {
        id: viewport
        anchors.fill: parent
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

        // Barre d'outils de l'image : avant/après, ajuster, recadrer.
        Rectangle {
            objectName: "imageToolbar"
            x: 12
            y: 12
            width: toolRow.implicitWidth + 16
            height: Theme.controlHeight + 12
            radius: Theme.radius
            color: Theme.panel
            border.color: Theme.border
            visible: root.hasImage
            RowLayout {
                id: toolRow
                anchors.centerIn: parent
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
                StudioButton {
                    objectName: "imageFit"
                    text: qsTr("Ajuster")
                    tooltip: qsTr("Cadrer l'image dans la fenêtre")
                    quiet: true
                    onClicked: root.space.fit()
                }
                StudioButton {
                    objectName: "imageCropTool"
                    text: qsTr("Recadrer")
                    tooltip: qsTr("Dessiner un cadre de recadrage sur l'image")
                    checkable: true
                    checked: root.space && root.space.cropActive
                    onClicked: root.space.cropActive ? root.space.cancelCrop() : root.space.beginCrop()
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
            radius: Theme.radius
            color: Theme.surface
            border.color: Theme.border
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

        // Panneau de l'espace : prétraitements (ImagePrepSection existant) + pile d'opérations.
        Rectangle {
            id: sidePanel
            objectName: "imageSidePanel"
            visible: root.hasImage && root.prepReady
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: 100
            width: 340
            color: Theme.panel
            border.color: Theme.border
            Controls.ScrollView {
                anchors.fill: parent
                anchors.margins: 8
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: sidePanel.width - 28
                    spacing: 10
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
    }

    Shortcut {
        sequence: "Escape"
        enabled: root.active && root.space && root.space.cropActive
        onActivated: root.space.cancelCrop()
    }
}
