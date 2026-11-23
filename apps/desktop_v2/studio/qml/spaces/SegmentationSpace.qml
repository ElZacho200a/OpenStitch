// SPDX-License-Identifier: Apache-2.0
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import QtQuick.Shapes
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
    // Zones de détail : outil modal (peindre / gommer) à côté du pinceau de frontière.
    readonly property bool hasDetail: root.hasTools && typeof detailZones !== "undefined"
    // Zones de traits fins : même pinceau, second masque (détection des traits renforcée).
    readonly property bool hasLinework: root.hasDetail && typeof lineworkZones !== "undefined"
    readonly property bool lineworkPainting: root.hasLinework && lineworkZones.tool !== ""
    readonly property bool detailPainting: root.hasDetail && (detailZones.tool !== "" || root.lineworkPainting)
    // Contrôleur du pinceau de zone actif (détail par défaut).
    readonly property var zone: root.lineworkPainting ? lineworkZones : (root.hasDetail ? detailZones : null)
    readonly property bool zoneErasing: root.zone !== null && root.zone.tool === "erase"
    property bool showSource: true
    // Couleurs des deux cartes (incrustation, pastille du pinceau, légende) : orange = détail,
    // cyan = traits fins. Identiques à celles de `DetailZoneController` (buildOverlay).
    readonly property color detailColor: Theme.zoneDetail
    readonly property color lineworkColor: Theme.zoneLinework
    readonly property color zoneColor: root.lineworkPainting ? root.lineworkColor : root.detailColor
    // La carte qu'on ne peint pas est atténuée pour ne pas se confondre avec celle qu'on peint.
    readonly property real dimmedOpacity: 0.3
    readonly property bool updateShapesAvailable: root.seg !== null && root.seg.canUpdateShapes

    // Un seul outil modal à la fois : id de la barre -> état des deux contrôleurs.
    function activateTool(id) {
        if (!root.hasTools) return
        const zoneTool = id === "detail" || id === "detailErase" || id === "linework" || id === "lineworkErase"
        regionTools.tool = (id === "" || zoneTool) ? "select" : id
        if (root.hasDetail)
            detailZones.tool = id === "detail" ? "paint" : (id === "detailErase" ? "erase" : "")
        if (root.hasLinework)
            lineworkZones.tool = id === "linework" ? "paint" : (id === "lineworkErase" ? "erase" : "")
    }
    readonly property string activeToolId: {
        if (!root.hasTools) return ""
        if (root.lineworkPainting) return lineworkZones.tool === "erase" ? "lineworkErase" : "linework"
        if (root.detailPainting) return detailZones.tool === "erase" ? "detailErase" : "detail"
        return regionTools.tool !== "select" ? regionTools.tool : ""
    }

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
        root.activateTool("")
        if (root.seg) root.seg.detach()
        if (root.workspace) {
            root.workspace.clearStageStatus(root.spaceId)
            root.workspace.setStageAction(root.spaceId, "", false)
        }
    }
    // Appelé par le Shortcut « Escape » de Main.qml : pinceau ou gomme -> sélection.
    function cancelTool() {
        if (!root.active || !root.hasTools || root.activeToolId === "") return false
        root.activateTool("")
        return true
    }
    onActiveChanged: { if (active) activate(); else deactivate() }
    Component.onCompleted: { if (active) activate() }
    Component.onDestruction: deactivate()
    Connections {
        target: root.seg
        function onStageChanged() { root.publish() }
    }
    // « Mettre à jour les formes » (bouton de la barre, du panneau ou du bandeau) applique la
    // carte de traits fins : le bouton d'application disparaît.
    Connections {
        target: root.hasTools ? regionTools : null
        function onRegenerated() { if (root.hasLinework) lineworkZones.markShapesUpdated() }
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
    // Parcours séquentiel des régions, région suivante / précédente (audit multicalque
    // segmentation, P2) : `RegionController::selectNextRegion`/`selectPreviousRegion` portent déjà
    // la logique, actif seulement quand l'espace l'est (comme les autres raccourcis de l'appli).
    Shortcut {
        sequence: "Tab"
        enabled: root.active && root.hasTools
        onActivated: regionTools.selectNextRegion()
    }
    Shortcut {
        sequence: "Shift+Tab"
        enabled: root.active && root.hasTools
        onActivated: regionTools.selectPreviousRegion()
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
                { id: "lasso", icon: "lasso", label: qsTr("Lasso : entourer les régions à sélectionner (Ctrl ajoute, Alt prend aussi les régions touchées)"), shortcut: "Maj+V", group: "selection" },
                { id: "brush", icon: "brush", label: qsTr("Pinceau de frontière : peindre la région choisie"), shortcut: "U", group: "regions" },
                { id: "erase", icon: "eraser", label: qsTr("Gomme : retirer de la région choisie"), shortcut: "Maj+U", group: "regions" },
                { id: "detail", icon: "detail", label: qsTr("Zones de détail : peindre où garder les petites formes"), shortcut: "Q", group: "detail", enabled: root.hasDetail },
                { id: "detailErase", icon: "detail-erase", label: qsTr("Gomme de détail : retirer une zone de détail"), shortcut: "Maj+Q", group: "detail", enabled: root.hasDetail },
                { id: "linework", icon: "linework", label: qsTr("Zones de traits fins : peindre où détecter plus finement les traits"), shortcut: "W", group: "linework", enabled: root.hasLinework },
                { id: "lineworkErase", icon: "linework-erase", label: qsTr("Gomme de traits fins : retirer une zone de traits fins"), shortcut: "Maj+W", group: "linework", enabled: root.hasLinework }
            ]
            activeTool: root.activeToolId
            onToolTriggered: id => root.activateTool(id)
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

        // Zones de détail peintes : incrustation orange posée à l'échelle de la caméra, au pas de
        // l'image de travail (comme l'aperçu ci-dessus). Visible tant qu'il y a un masque.
        Image {
            id: detailOverlay
            objectName: "detailZonesOverlay"
            readonly property double wMm: root.hasDetail ? imageTools.imageWidth * imageTools.mmPerPx : 0
            readonly property double hMm: root.hasDetail ? imageTools.imageHeight * imageTools.mmPerPx : 0
            visible: root.hasDetail && detailZones.hasDetail && detailZones.overlayVisible && wMm > 0
            opacity: root.lineworkPainting ? root.dimmedOpacity : 1.0
            enabled: false
            cache: false
            smooth: false
            fillMode: Image.Stretch
            source: visible ? "image://prepare/detail?rev=" + detailZones.overlayRevision : ""
            width: wMm * livePreview.cam.zoom
            height: hMm * livePreview.cam.zoom
            x: parent.width / 2 + (-wMm / 2 - livePreview.cam.centerX) * livePreview.cam.zoom
            y: parent.height / 2 + (-hMm / 2 - livePreview.cam.centerY) * livePreview.cam.zoom
        }

        // Zones de traits fins peintes : incrustation cyan, même pose que les zones de détail.
        Image {
            objectName: "lineworkZonesOverlay"
            readonly property double wMm: root.hasLinework ? imageTools.imageWidth * imageTools.mmPerPx : 0
            readonly property double hMm: root.hasLinework ? imageTools.imageHeight * imageTools.mmPerPx : 0
            visible: root.hasLinework && lineworkZones.hasDetail && lineworkZones.overlayVisible && wMm > 0
            opacity: root.detailPainting && !root.lineworkPainting ? root.dimmedOpacity : 1.0
            enabled: false
            cache: false
            smooth: false
            fillMode: Image.Stretch
            source: visible ? "image://prepare/linework?rev=" + lineworkZones.overlayRevision : ""
            width: wMm * livePreview.cam.zoom
            height: hMm * livePreview.cam.zoom
            x: parent.width / 2 + (-wMm / 2 - livePreview.cam.centerX) * livePreview.cam.zoom
            y: parent.height / 2 + (-hMm / 2 - livePreview.cam.centerY) * livePreview.cam.zoom
        }

        // Pastille du pinceau de détail : le disque exact qui sera peint (rayon en mm).
        Rectangle {
            id: detailCursor
            objectName: "detailZonesCursor"
            readonly property double diameter: root.zone !== null ? 2 * root.zone.radiusMm * livePreview.cam.zoom : 0
            visible: root.detailPainting && pick.containsMouse
            enabled: false
            width: diameter
            height: diameter
            radius: diameter / 2
            x: pick.mouseX - diameter / 2
            y: pick.mouseY - diameter / 2
            color: "transparent"
            border.width: 2
            border.color: root.zoneErasing ? Theme.foreground : root.zoneColor
        }

        // Options du pinceau de détail : rayon en mm, part de l'image marquée, effacement total et
        // application (relance la segmentation avec les réglages du panneau).
        Rectangle {
            objectName: "detailToolOptions"
            visible: root.detailPainting
            x: 8
            y: 8
            width: Math.min(parent.width - 16, detailRow.implicitWidth + 20)
            height: Theme.controlHeight + 12
            radius: Theme.cardRadius
            color: Theme.panel
            border.color: Theme.divider
            RowLayout {
                id: detailRow
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 10
                spacing: 6
                Rectangle {
                    objectName: "zoneColorDot"
                    Layout.preferredWidth: 10
                    Layout.preferredHeight: 10
                    radius: 5
                    color: root.zoneColor
                }
                StudioLabel {
                    objectName: "zoneToolLabel"
                    text: root.lineworkPainting ? (root.zoneErasing ? qsTr("Gomme de traits fins") : qsTr("Traits fins"))
                                                : (root.zoneErasing ? qsTr("Gomme de détail") : qsTr("Détail"))
                    tone: "foreground"
                    wrapMode: Text.NoWrap
                }
                StudioButton {
                    objectName: "detailSmaller"
                    text: "−"
                    quiet: true
                    tooltip: qsTr("Pinceau de détail : rayon plus petit")
                    onClicked: root.zone.stepRadius(-1)
                }
                StudioLabel {
                    objectName: "detailRadiusLabel"
                    text: qsTr("%1 mm").arg(root.zone !== null ? root.zone.radiusMm.toFixed(1) : "")
                    tone: "foreground"
                    wrapMode: Text.NoWrap
                    Accessible.name: qsTr("Rayon du pinceau de détail")
                }
                StudioButton {
                    objectName: "detailLarger"
                    text: "+"
                    quiet: true
                    tooltip: qsTr("Pinceau de détail : rayon plus grand")
                    onClicked: root.zone.stepRadius(1)
                }
                StudioLabel {
                    objectName: "detailCoverage"
                    visible: root.zone !== null && root.zone.hasDetail
                    text: root.zone !== null ? qsTr("%1 % de l'image").arg(root.zone.coveragePercent.toFixed(1)) : ""
                    wrapMode: Text.NoWrap
                }
                StudioButton {
                    objectName: "zoneOverlayToggle"
                    iconName: root.zone !== null && root.zone.overlayVisible ? "eye" : "eye-off"
                    quiet: true
                    tooltip: root.zone !== null && root.zone.overlayVisible ? qsTr("Masquer cette carte sur le canevas")
                                                                              : qsTr("Afficher cette carte sur le canevas")
                    onClicked: root.zone.overlayVisible = !root.zone.overlayVisible
                }
                StudioButton {
                    objectName: "detailClear"
                    visible: root.zone !== null && root.zone.hasDetail
                    text: qsTr("Tout effacer")
                    quiet: true
                    tooltip: root.lineworkPainting ? qsTr("Retire toutes les zones de traits fins (annulable)")
                                                   : qsTr("Retire toutes les zones de détail (annulable)")
                    onClicked: root.zone.clearAll()
                }
                StudioButton {
                    objectName: "detailApply"
                    visible: root.zone !== null && root.zone.needsSegmentation
                    text: qsTr("Segmenter")
                    tooltip: qsTr("Relance la segmentation : le détail peint est pris en compte")
                    onClicked: root.segmentNow()
                }
                StudioButton {
                    objectName: "lineworkApply"
                    visible: root.lineworkPainting && lineworkZones.needsShapesUpdate
                    enabled: root.updateShapesAvailable
                    text: qsTr("Mettre à jour les formes")
                    emphasized: true
                    tooltip: root.updateShapesAvailable
                             ? qsTr("Applique les traits fins peints : la détection est renforcée dans la zone à la mise à jour des formes")
                             : qsTr("Segmentez d'abord l'image : les formes se mettent à jour depuis la segmentation")
                    onClicked: root.seg.updateShapes(false)
                }
                StudioLabel {
                    objectName: "detailMessage"
                    visible: text.length > 0
                    text: root.zone !== null ? root.zone.message : ""
                    wrapMode: Text.NoWrap
                    elide: Text.ElideRight
                    Layout.maximumWidth: 300
                }
            }
        }

        // Légende des deux cartes : couleur, part de l'image, œil pour afficher / masquer chacune.
<<<<<<< HEAD
=======
        Rectangle {
            id: legend
            objectName: "zonesLegend"
            // Repli : l'espace s'ouvre aussi sans les contrôleurs de zones (aucune référence indéfinie).
            readonly property var fallback: ({ hasDetail: false, coveragePercent: 0, overlayVisible: false })
            readonly property var dz: root.hasLinework ? detailZones : legend.fallback
            readonly property var lz: root.hasLinework ? lineworkZones : legend.fallback
            visible: root.hasLinework && (root.detailPainting || legend.dz.hasDetail || legend.lz.hasDetail)
            x: 8
            y: 8 + Theme.controlHeight + 12 + 6
            width: Math.min(parent.width - 16, legendRow.implicitWidth + 20)
            height: Theme.controlHeight + 4
            radius: Theme.cardRadius
            color: Theme.panel
            border.color: Theme.divider
            RowLayout {
                id: legendRow
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 10
                spacing: 4
                Rectangle { Layout.preferredWidth: 10; Layout.preferredHeight: 10; radius: 5; color: root.detailColor }
                StudioButton {
                    objectName: "legendDetail"
                    text: legend.dz.hasDetail ? qsTr("Détail %1 %").arg(legend.dz.coveragePercent.toFixed(1)) : qsTr("Détail")
                    iconName: legend.dz.overlayVisible ? "eye" : "eye-off"
                    quiet: true
                    tooltip: legend.dz.overlayVisible ? qsTr("Masquer la carte des zones de détail (orange)")
                                                        : qsTr("Afficher la carte des zones de détail (orange)")
                    onClicked: legend.dz.overlayVisible = !legend.dz.overlayVisible
                }
                Rectangle { Layout.preferredWidth: 10; Layout.preferredHeight: 10; radius: 5; color: root.lineworkColor }
                StudioButton {
                    objectName: "legendLinework"
                    text: legend.lz.hasDetail ? qsTr("Traits fins %1 %").arg(legend.lz.coveragePercent.toFixed(1)) : qsTr("Traits fins")
                    iconName: legend.lz.overlayVisible ? "eye" : "eye-off"
                    quiet: true
                    tooltip: legend.lz.overlayVisible ? qsTr("Masquer la carte des zones de traits fins (cyan)")
                                                          : qsTr("Afficher la carte des zones de traits fins (cyan)")
                    onClicked: legend.lz.overlayVisible = !legend.lz.overlayVisible
                }
            }
        }

        // Options de l'outil actif (pinceau, gomme) posées sur le canevas, à côté de la barre :
        // rayon et dernier message de l'outil (un refus reste visible quel que soit l'onglet).
>>>>>>> refs/pr/449
        Rectangle {
            id: legend
            objectName: "zonesLegend"
            // Repli : l'espace s'ouvre aussi sans les contrôleurs de zones (aucune référence indéfinie).
            readonly property var fallback: ({ hasDetail: false, coveragePercent: 0, overlayVisible: false })
            readonly property var dz: root.hasLinework ? detailZones : legend.fallback
            readonly property var lz: root.hasLinework ? lineworkZones : legend.fallback
            visible: root.hasLinework && (root.detailPainting || legend.dz.hasDetail || legend.lz.hasDetail)
            x: 8
            y: 8 + Theme.controlHeight + 12 + 6
            width: Math.min(parent.width - 16, legendRow.implicitWidth + 20)
            height: Theme.controlHeight + 4
            radius: Theme.cardRadius
            color: Theme.panel
            border.color: Theme.divider
            RowLayout {
                id: legendRow
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 10
                spacing: 4
                Rectangle { Layout.preferredWidth: 10; Layout.preferredHeight: 10; radius: 5; color: root.detailColor }
                StudioButton {
                    objectName: "legendDetail"
                    text: legend.dz.hasDetail ? qsTr("Détail %1 %").arg(legend.dz.coveragePercent.toFixed(1)) : qsTr("Détail")
                    iconName: legend.dz.overlayVisible ? "eye" : "eye-off"
                    quiet: true
                    tooltip: legend.dz.overlayVisible ? qsTr("Masquer la carte des zones de détail (orange)")
                                                        : qsTr("Afficher la carte des zones de détail (orange)")
                    onClicked: legend.dz.overlayVisible = !legend.dz.overlayVisible
                }
                Rectangle { Layout.preferredWidth: 10; Layout.preferredHeight: 10; radius: 5; color: root.lineworkColor }
                StudioButton {
                    objectName: "legendLinework"
                    text: legend.lz.hasDetail ? qsTr("Traits fins %1 %").arg(legend.lz.coveragePercent.toFixed(1)) : qsTr("Traits fins")
                    iconName: legend.lz.overlayVisible ? "eye" : "eye-off"
                    quiet: true
                    tooltip: legend.lz.overlayVisible ? qsTr("Masquer la carte des zones de traits fins (cyan)")
                                                          : qsTr("Afficher la carte des zones de traits fins (cyan)")
                    onClicked: legend.lz.overlayVisible = !legend.lz.overlayVisible
                }
            }
        }

        // Options de l'outil actif (pinceau, gomme, lasso) posées sur le canevas, à côté de la
        // barre : rayon (pinceau et gomme seulement) et dernier message de l'outil (un refus ou le
        // résultat du lasso reste visible quel que soit l'onglet).
        Rectangle {
            id: regionToolOptions
            objectName: "regionToolOptions"
            readonly property bool brushing: root.hasTools && (regionTools.tool === "brush" || regionTools.tool === "erase")
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
                StudioLabel {
                    objectName: "regionLassoLabel"
                    visible: !regionToolOptions.brushing
                    text: qsTr("Lasso")
                    tone: "foreground"
                    wrapMode: Text.NoWrap
                }
                StudioLabel {
                    visible: regionToolOptions.brushing
                    text: qsTr("Rayon")
                    tone: "foreground"
                    wrapMode: Text.NoWrap
                }
                StudioButton {
                    objectName: "brushSmaller"
                    visible: regionToolOptions.brushing
                    text: "−"
                    quiet: true
                    tooltip: qsTr("Pinceau : rayon plus petit")
                    onClicked: regionTools.stepBrushRadius(-1)
                }
                StudioSpinBox {
                    objectName: "regionBrushRadius"
                    visible: regionToolOptions.brushing
                    from: 1; to: 200; editable: true
                    value: root.hasTools ? regionTools.brushRadius : 1
                    Accessible.name: qsTr("Rayon du pinceau (px)")
                    onValueModified: regionTools.brushRadius = value
                }
                StudioButton {
                    objectName: "brushLarger"
                    visible: regionToolOptions.brushing
                    text: "+"
                    quiet: true
                    tooltip: qsTr("Pinceau : rayon plus grand")
                    onClicked: regionTools.stepBrushRadius(1)
                }
                StudioLabel {
                    objectName: "regionToolMessage"
                    visible: text.length > 0
                    // Lasso sans message : le mode d'emploi, visible quel que soit l'onglet ouvert.
                    text: !root.hasTools ? ""
                          : regionTools.message.length > 0 ? regionTools.message
                          : regionToolOptions.brushing ? ""
                          : qsTr("Entourez les régions · Ctrl ajoute · Alt prend aussi les régions touchées")
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

        // Clic : sélection de la région sous le curseur (Ctrl = multi-sélection). Glisser en
        // sélection trace un CADRE, l'outil lasso un contour libre : les régions entièrement
        // dedans sont sélectionnées (Ctrl/Maj ajoute, Alt prend aussi les régions touchées).
        // Pinceau / gomme des régions quand l'outil de RegionController l'est. Les autres boutons
        // (déplacement, molette) restent au canevas.
        MouseArea {
            id: pick
            objectName: "segmentationPickArea"
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            enabled: root.seg !== null
            hoverEnabled: root.detailPainting
            property var stroke: []
            readonly property bool painting: root.hasTools && regionTools.tool !== "select"
                                             && regionTools.tool !== "lasso"
            readonly property bool lassoTool: root.hasTools && regionTools.tool === "lasso"
            // Zone en cours, en pixels de l'écran : "" | "pending" (bouton enfoncé, pas encore
            // glissé) | "rect" | "lasso".
            property string zoneKind: ""
            property point zoneStart: Qt.point(0, 0)
            property point zoneEnd: Qt.point(0, 0)
            property var zonePoints: []
            property int zoneModifiers: 0
            readonly property int dragThreshold: 4
            function scenePoint(mx, my) {
                const v = spaceCanvas.viewState()
                return Qt.point(v.centerX + (mx - width / 2) / v.zoom,
                                v.centerY + (my - height / 2) / v.zoom)
            }
            function resetZone() {
                pick.zoneKind = ""
                pick.zonePoints = []
            }
            // Clic sans glisser : la région sous le point de départ, comme avant.
            function clickPick() {
                const p = pick.scenePoint(pick.zoneStart.x, pick.zoneStart.y)
                root.seg.pickAt(p.x, p.y, (pick.zoneModifiers & Qt.ControlModifier) !== 0)
            }
            function commitZone() {
                const screen = pick.zoneKind === "rect"
                    ? [pick.zoneStart, Qt.point(pick.zoneEnd.x, pick.zoneStart.y), pick.zoneEnd,
                       Qt.point(pick.zoneStart.x, pick.zoneEnd.y)]
                    : pick.zonePoints
                const scene = []
                for (let i = 0; i < screen.length; ++i) {
                    const sp = pick.scenePoint(screen[i].x, screen[i].y)
                    scene.push({ x: sp.x, y: sp.y })
                }
                const additive = (pick.zoneModifiers & (Qt.ControlModifier | Qt.ShiftModifier)) !== 0
                const touching = (pick.zoneModifiers & Qt.AltModifier) !== 0
                root.seg.selectInZone(scene, additive, touching)
            }
            onPressed: function(mouse) {
                // Clic droit : sélectionne la région sous le curseur (sauf pinceau/gomme actifs,
                // où il resterait sans effet visible) puis ouvre le menu contextuel pour elle.
                if (mouse.button === Qt.RightButton) {
                    if (pick.painting || root.detailPainting || !root.hasTools) return
                    pick.resetZone()
                    const rp = pick.scenePoint(mouse.x, mouse.y)
                    root.seg.pickAt(rp.x, rp.y, false)
                    if (regionTools.selectedCount === 1)
                        regionContextMenu.openFor(regionTools.selected[0], mouse.x, mouse.y)
                    return
                }
                const p = pick.scenePoint(mouse.x, mouse.y)
                if (root.detailPainting) {
                    // Zones de détail : points en mm scène, le contrôleur les convertit en pixels
                    // (le masque se peint même sans segmentation).
                    pick.stroke = [{ x: p.x, y: p.y }]
                } else if (pick.painting) {
                    const px = root.seg.pixelAt(p.x, p.y)
                    pick.stroke = px.valid ? [{ x: px.x, y: px.y }] : []
                } else if (root.hasTools && regionTools.mergeTargetMode) {
                    // Choix de la cible de fusion : un clic, jamais une zone.
                    root.seg.pickAt(p.x, p.y, false)
                } else {
                    // Sélection : décidée au relâchement (clic, cadre ou lasso).
                    pick.zoneStart = Qt.point(mouse.x, mouse.y)
                    pick.zoneEnd = pick.zoneStart
                    pick.zonePoints = [Qt.point(mouse.x, mouse.y)]
                    pick.zoneModifiers = mouse.modifiers
                    pick.zoneKind = "pending"
                }
            }
            onPositionChanged: function(mouse) {
                if (pick.zoneKind !== "" && pick.pressed) {
                    if (pick.zoneKind === "pending"
                            && Math.abs(mouse.x - pick.zoneStart.x) < pick.dragThreshold
                            && Math.abs(mouse.y - pick.zoneStart.y) < pick.dragThreshold)
                        return
                    if (pick.zoneKind === "pending")
                        pick.zoneKind = pick.lassoTool ? "lasso" : "rect"
                    pick.zoneEnd = Qt.point(mouse.x, mouse.y)
                    // Alt ou Ctrl enfoncé en cours de tracé compte aussi.
                    pick.zoneModifiers |= mouse.modifiers
                    if (pick.zoneKind === "lasso") {
                        const zpts = pick.zonePoints
                        zpts.push(Qt.point(mouse.x, mouse.y)) // copie : pas une référence à zoneEnd
                        pick.zonePoints = zpts
                    }
                } else if (root.detailPainting && pick.pressed) {
                    const dp = pick.scenePoint(mouse.x, mouse.y)
                    const dpts = pick.stroke
                    dpts.push({ x: dp.x, y: dp.y })
                    pick.stroke = dpts
                } else if (pick.painting && pick.pressed) {
                    const p = pick.scenePoint(mouse.x, mouse.y)
                    const px = root.seg.pixelAt(p.x, p.y)
                    if (px.valid) {
                        const pts = pick.stroke
                        pts.push({ x: px.x, y: px.y })
                        pick.stroke = pts
                    }
                }
            }
<<<<<<< HEAD
            onReleased: function(mouse) {
                if (pick.zoneKind !== "")
                    pick.zoneModifiers |= mouse.modifiers
                if (pick.zoneKind === "pending") {
                    pick.clickPick()
                } else if (pick.zoneKind === "rect"
                           || (pick.zoneKind === "lasso" && pick.zonePoints.length >= 3)) {
                    pick.commitZone()
                } else if (root.detailPainting && pick.stroke.length > 0) {
=======
            onReleased: {
                if (root.detailPainting && pick.stroke.length > 0) {
>>>>>>> refs/pr/449
                    root.zone.paintStroke(pick.stroke)
                } else if (pick.painting && pick.stroke.length > 0) {
                    regionTools.paintStroke(pick.stroke, regionTools.tool === "erase")
                }
                pick.resetZone()
                pick.stroke = []
            }
            onCanceled: { pick.resetZone(); pick.stroke = [] }
        }

        // Tracé de la zone en cours (cadre ou lasso) : trait pointillé, aucun calcul.
        Shape {
            id: zoneOutline
            objectName: "segmentationZoneOutline"
            anchors.fill: parent
            enabled: false
            visible: pick.zoneKind === "rect" || pick.zoneKind === "lasso"
            ShapePath {
                strokeColor: Theme.accent
                strokeWidth: 1.5
                strokeStyle: ShapePath.DashLine
                dashPattern: [4, 3]
                fillColor: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.12)
                PathPolyline {
                    path: {
                        if (pick.zoneKind === "rect") {
                            const a = pick.zoneStart
                            const b = pick.zoneEnd
                            return [a, Qt.point(b.x, a.y), b, Qt.point(a.x, b.y), a]
                        }
                        const pts = pick.zonePoints.slice()
                        if (pts.length > 0) pts.push(pts[0])
                        return pts
                    }
                }
            }
        }

        RegionContextMenu {
            id: regionContextMenu
            region: root.hasTools ? regionTools : null
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
