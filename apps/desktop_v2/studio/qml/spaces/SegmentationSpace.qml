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
    // Pendant l'aperçu en direct, l'image prévisualisée remplace les régions du projet.
    readonly property bool previewing: root.hasTools && imageTools.segPreviewActive
    readonly property var canvasLayers: ({ sourceImage: root.showSource, regions: !root.previewing,
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
        // Le bandeau d'étape est un statut, pas un déclencheur : « Segmenter l'image » (état
        // vide) et « Appliquer »/« Aperçu » (onglet Segmentation) restent les seules actions —
        // un seul endroit par action, cf. audit UX panneaux Segmentation.
        root.workspace.setStageAction(root.spaceId, "", false)
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
        // Un aperçu laissé actif réapparaîtrait à la place des régions au retour dans l'espace.
        // Annule aussi un aperçu encore en calcul (il réapparaîtrait au retour dans l'espace).
        if (root.hasTools)
            imageTools.cancelSegmentationPreview()
        if (root.seg) root.seg.detach()
        if (root.workspace) {
            root.workspace.clearStageStatus(root.spaceId)
            root.workspace.setStageAction(root.spaceId, "", false)
        }
    }
    // Appelé par le Shortcut « Escape » de Main.qml : pinceau ou gomme -> sélection.
    function cancelTool() {
        if (!root.active || !root.hasTools || regionTools.tool === "select") return false
        regionTools.tool = "select"
        return true
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

    // Barre latérale commune : outils modaux du canevas (pinceau et gomme de frontière). Les actions
    // ponctuelles (fusionner, nettoyer, recolorer…) restent dans le panneau.
    StudioCard {
        id: segRailCard
        objectName: "segmentationRailCard"
        visible: root.hasTools
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: Theme.gap
        width: Theme.controlHeight + 16
        height: Math.min(segRail.implicitHeight + 16, root.height - 2 * Theme.gap)
        CanvasToolRail {
            id: segRail
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: 8
            canvas: spaceCanvas
            workspaceActive: root.active
            toolPrefix: "regionTool_"
            entries: [
                { id: "brush", icon: "brush", label: qsTr("Pinceau de frontière : peindre la région choisie"), shortcut: "U", group: "regions" },
                { id: "erase", icon: "eraser", label: qsTr("Gomme : retirer de la région choisie"), shortcut: "Maj+U", group: "regions" },
                { id: "bucket", icon: "bucket", label: qsTr("Pot de peinture : remplir une poche de fond fermée"), shortcut: "Maj+B", group: "regions" }
            ]
            activeTool: root.hasTools && regionTools.tool !== "select" ? regionTools.tool : ""
            onToolTriggered: id => { if (root.hasTools) regionTools.tool = id === "" ? "select" : id }
            onFitRequested: spaceCanvas.fitDesign()
        }
    }

    Item {
        id: viewport
        anchors.left: segRailCard.visible ? segRailCard.right : parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: panelLoader.active ? panelLoader.left : parent.right
        anchors.margins: Theme.gap
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

        // Aperçu de segmentation en direct SUR le canevas : l'image prévisualisée (mêmes dimensions
        // que l'image de travail, centrée) est posée à l'échelle de la caméra. Aucun calcul ici.
        Image {
            id: livePreview
            objectName: "segmentationLivePreview"
            property int cameraTick: 0
            readonly property var cam: { cameraTick; return spaceCanvas.viewState() }
            readonly property double wMm: root.hasTools ? imageTools.imageWidth * imageTools.mmPerPx : 0
            readonly property double hMm: root.hasTools ? imageTools.imageHeight * imageTools.mmPerPx : 0
            visible: root.previewing && wMm > 0
            enabled: false
            cache: false
            smooth: true
            fillMode: Image.Stretch
            source: visible ? "image://prepare/segmentation?rev=" + imageTools.imageRevision : ""
            width: wMm * cam.zoom
            height: hMm * cam.zoom
            x: parent.width / 2 + (-wMm / 2 - cam.centerX) * cam.zoom
            y: parent.height / 2 + (-hMm / 2 - cam.centerY) * cam.zoom
            Connections {
                target: spaceCanvas
                function onViewChanged() { livePreview.cameraTick++ }
            }
        }

        // Options de l'outil actif (pinceau, gomme) posées sur le canevas, à côté de la barre :
        // rayon et dernier message de l'outil (un refus reste visible quel que soit l'onglet).
        Rectangle {
            objectName: "regionToolOptions"
            visible: root.hasTools && regionTools.tool !== "select"
            x: 8
            y: 8
            width: Math.min(parent.width - 16, optionsRow.implicitWidth + 20)
            height: Theme.controlHeight + 12
            radius: Theme.cardRadius
            color: Theme.panel
            border.color: Theme.divider
            RowLayout {
                id: optionsRow
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 10
                spacing: 6
                readonly property bool hasRadius: root.hasTools && regionTools.tool !== "bucket"
                StudioLabel {
                    visible: optionsRow.hasRadius
                    text: qsTr("Rayon")
                    tone: "foreground"
                    wrapMode: Text.NoWrap
                }
                StudioButton {
                    objectName: "brushSmaller"
                    visible: optionsRow.hasRadius
                    text: "−"
                    quiet: true
                    tooltip: qsTr("Pinceau : rayon plus petit")
                    onClicked: regionTools.stepBrushRadius(-1)
                }
                StudioSpinBox {
                    objectName: "regionBrushRadius"
                    visible: optionsRow.hasRadius
                    from: 1; to: 200; editable: true
                    value: root.hasTools ? regionTools.brushRadius : 1
                    Accessible.name: qsTr("Rayon du pinceau (px)")
                    onValueModified: regionTools.brushRadius = value
                }
                StudioButton {
                    objectName: "brushLarger"
                    visible: optionsRow.hasRadius
                    text: "+"
                    quiet: true
                    tooltip: qsTr("Pinceau : rayon plus grand")
                    onClicked: regionTools.stepBrushRadius(1)
                }
                StudioLabel {
                    objectName: "regionToolMessage"
                    visible: text.length > 0
                    text: root.hasTools ? regionTools.message : ""
                    wrapMode: Text.NoWrap
                    elide: Text.ElideRight
                    Layout.maximumWidth: 320
                }
            }
        }

        // Reflect actual image / region processing activity, not an estimated percentage.
        Rectangle {
            objectName: "segmentationBusyBadge"
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 16
            visible: root.seg !== null && root.seg.busy
            enabled: false
            width: busyRow.implicitWidth + 22
            height: 32
            color: Theme.surface
            border.color: Theme.info
            radius: Theme.radius
            Row {
                id: busyRow
                anchors.centerIn: parent
                spacing: 8
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: Theme.reducedMotion
                    width: 10
                    height: 10
                    radius: 5
                    color: Theme.info
                }
                Controls.BusyIndicator {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !Theme.reducedMotion
                    running: root.seg !== null && root.seg.busy && !Theme.reducedMotion
                    width: 16
                    height: 16
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Segmentation en cours…")
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallSize
                }
            }
            Accessible.role: Accessible.StaticText
            Accessible.name: qsTr("Segmentation en cours")
        }

        // L'aperçu remplace silencieusement la couche régions par l'image prévisualisée
        // (livePreview ci-dessus) : sans ce bandeau, rien ne distingue à l'écran un aperçu non
        // appliqué de la segmentation réelle du document.
        Rectangle {
            objectName: "segmentationPreviewBadge"
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: 16
            visible: root.previewing
            enabled: false
            width: previewRow.implicitWidth + 24
            height: 32
            color: Theme.accentSoft
            border.color: Theme.accent
            radius: Theme.radius
            Row {
                id: previewRow
                anchors.centerIn: parent
                spacing: 8
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 10
                    height: 10
                    radius: 5
                    color: Theme.accent
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Aperçu de segmentation — document inchangé")
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallSize
                }
            }
            Accessible.role: Accessible.StaticText
            Accessible.name: qsTr("Aperçu de segmentation en cours, document inchangé")
        }

        // Clic : sélection de la région sous le curseur (Ctrl = multi-sélection) ; pinceau / gomme
        // des régions quand l'outil de RegionController l'est. Les autres boutons (déplacement,
        // molette) restent au canevas.
        MouseArea {
            id: pick
            objectName: "segmentationPickArea"
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            enabled: root.seg !== null
            property var stroke: []
            readonly property bool painting: root.hasTools
                && (regionTools.tool === "brush" || regionTools.tool === "erase")
            readonly property bool filling: root.hasTools && regionTools.tool === "bucket"
            function scenePoint(mx, my) {
                const v = spaceCanvas.viewState()
                return Qt.point(v.centerX + (mx - width / 2) / v.zoom,
                                v.centerY + (my - height / 2) / v.zoom)
            }
            onPressed: function(mouse) {
                // Clic droit : sélectionne la région sous le curseur (sauf pinceau/gomme actifs,
                // où il resterait sans effet visible) puis ouvre le menu contextuel pour elle.
                if (mouse.button === Qt.RightButton) {
                    if (pick.painting || pick.filling || !root.hasTools) return
                    const rp = pick.scenePoint(mouse.x, mouse.y)
                    root.seg.pickAt(rp.x, rp.y, false)
                    if (regionTools.selectedCount === 1)
                        regionContextMenu.openFor(regionTools.selected[0], mouse.x, mouse.y)
                    return
                }
                const p = pick.scenePoint(mouse.x, mouse.y)
                if (pick.filling) {
                    // Un seul clic : pas de trait à accumuler, contrairement au pinceau/gomme.
                    const px = root.seg.pixelAt(p.x, p.y)
                    if (px.valid) regionTools.paintBucket(px.x, px.y)
                } else if (pick.painting) {
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

        RegionContextMenu {
            id: regionContextMenu
            region: root.hasTools ? regionTools : null
        }

        RegionQuickBar {
            anchors.top: parent.top
            anchors.topMargin: 8
            anchors.horizontalCenter: parent.horizontalCenter
            z: 5
            region: root.hasTools ? regionTools : null
            spaceActive: root.active
        }

        Loader {
            id: controlsLoader
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            active: root.seg !== null
            sourceComponent: RegionLayerControls {
                seg: root.seg
                showSource: root.showSource
                onShowSourceChanged: root.showSource = showSource
            }
        }

        ViewPanel {


            active: root.active
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
        anchors.margins: Theme.gap
        anchors.leftMargin: 0
        width: Math.min(340, Math.max(180, root.width * 0.38))
        active: root.hasTools
        sourceComponent: SegmentationPanels {}
    }

    readonly property var editorController: editor
    readonly property var toolController: typeof tools !== "undefined" ? tools : null
}
