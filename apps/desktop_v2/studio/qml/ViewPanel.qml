// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."

// Hub of the view / simulation / thread / analysis surfaces, laid over the canvas viewport.
// It is inert (loads nothing) when the host does not provide the adapters, like the other
// optional panels. All state lives in ViewController / ThreadController / AnalysisController.
Item {
    id: hub
    objectName: "viewHub"
    property var canvasItem
    // "", "view", "threads", "analysis"
    property string openPanel: ""
    readonly property bool ready: loader.status === Loader.Ready

    function toggle(name) { openPanel = openPanel === name ? "" : name }

    Loader {
        id: loader
        anchors.fill: parent
        active: typeof view !== "undefined" && typeof threads !== "undefined"
                && typeof analysis !== "undefined"
        sourceComponent: Item {
            id: inner
            property var ticksH: []
            property var ticksV: []
            function refreshTicks() {
                if (!view.rulersVisible || !hub.canvasItem) { ticksH = []; ticksV = []; return }
                ticksH = hub.canvasItem.rulerTicks(true)
                ticksV = hub.canvasItem.rulerTicks(false)
            }
            Component.onCompleted: {
                view.attachCanvas(hub.canvasItem)
                refreshTicks()
            }
            Connections { target: hub.canvasItem; function onViewChanged() { inner.refreshTicks() } }
            Connections { target: view; function onDisplayChanged() { inner.refreshTicks() } }

            Shortcut { sequence: "G"; onActivated: view.gridVisible = !view.gridVisible }
            Shortcut { sequence: "Z"; onActivated: view.zoomRectangle() }
            Shortcut { sequence: "1"; onActivated: view.zoomActualSize() }
            Shortcut { sequence: "Shift+F"; onActivated: view.zoomSelection() }
            Shortcut { sequence: "Alt+Left"; onActivated: view.zoomPreviousView() }
            Shortcut { sequence: "Ctrl+L"; onActivated: view.simActive ? view.stopSimulation() : view.startSimulation() }
            Shortcut { sequence: "Ctrl+Shift+T"; onActivated: view.toggleRealistic() }

            // Rulers (model mm, Y up). At most ~60 labels, rebuilt on view change only.
            Rectangle { visible: view.rulersVisible; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 18; color: Theme.panel; opacity: 0.9
                Repeater { model: inner.ticksH
                    delegate: Text { required property var modelData; x: modelData.pos + 2; y: 2; text: modelData.label; color: Theme.faint; font.pixelSize: Theme.captionSize; font.family: Theme.fontFamily } } }
            Rectangle { visible: view.rulersVisible; anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 34; color: Theme.panel; opacity: 0.9
                Repeater { model: inner.ticksV
                    delegate: Text { required property var modelData; y: modelData.pos + 2; x: 2; text: modelData.label; color: Theme.faint; font.pixelSize: Theme.captionSize; font.family: Theme.fontFamily } } }

            // Zoom rectangle rubber band (state owned by the canvas gesture).
            Rectangle {
                objectName: "rubberBand"
                visible: hub.canvasItem && hub.canvasItem.rubberBand.width > 0
                x: hub.canvasItem ? hub.canvasItem.rubberBand.x : 0
                y: hub.canvasItem ? hub.canvasItem.rubberBand.y : 0
                width: hub.canvasItem ? hub.canvasItem.rubberBand.width : 0
                height: hub.canvasItem ? hub.canvasItem.rubberBand.height : 0
                color: Qt.alpha(Theme.accent, 0.13)
                border.color: Theme.accent
            }

            // Compact toolbar: the canvas keeps the space, panels open on demand.
            Row {
                objectName: "viewToolbar"
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 10
                spacing: 4
                StudioButton { objectName: "realisticButton2"; text: qsTr("Réaliste"); shortcutLabel: "Ctrl+Shift+T"; tooltip: qsTr("Rendu réaliste (fils en relief, ombre, tissu)"); checkable: true; checked: view.realistic; onClicked: view.toggleRealistic() }
                StudioButton { objectName: "viewPanelButton"; text: qsTr("Vue"); checkable: true; checked: hub.openPanel === "view"; onClicked: hub.toggle("view") }
                StudioButton { objectName: "threadsPanelButton"; text: qsTr("Fils"); checkable: true; checked: hub.openPanel === "threads"; onClicked: hub.toggle("threads") }
                StudioButton { objectName: "analysisPanelButton"; text: qsTr("Analyse"); checkable: true; checked: hub.openPanel === "analysis"; onClicked: hub.toggle("analysis") }
                StudioButton { objectName: "simulationButton"; text: qsTr("Simulation"); shortcutLabel: "Ctrl+L"; checkable: true; checked: view.simActive; enabled: view.simReady; onClicked: view.simActive ? view.stopSimulation() : view.startSimulation() }
            }

            Rectangle {
                id: drawer
                objectName: "viewDrawer"
                visible: hub.openPanel !== ""
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.topMargin: 54
                anchors.rightMargin: 10
                anchors.bottomMargin: 70
                anchors.bottom: parent.bottom
                width: Math.min(340, parent.width - 40)
                radius: Theme.radius
                color: Theme.panel
                border.color: Theme.divider

                ThreadPanel { anchors.fill: parent; visible: hub.openPanel === "threads"; controller: threads }
                AnalysisPanel { anchors.fill: parent; visible: hub.openPanel === "analysis"; controller: analysis }

                Flickable {
                    objectName: "viewSettings"
                    anchors.fill: parent
                    anchors.margins: 10
                    visible: hub.openPanel === "view"
                    contentHeight: viewColumn.implicitHeight
                    clip: true
                    ColumnLayout {
                        id: viewColumn
                        width: parent.width - 12
                        spacing: 8
                        SectionLabel { text: qsTr("FILTRES D'AFFICHAGE") }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 4
                            StudioButton {
                                objectName: "showAllButton"
                                text: qsTr("Tout afficher") + (view.hiddenCount > 0 ? " (" + view.hiddenCount + ")" : "")
                                enabled: view.hiddenCount > 0
                                onClicked: view.showAll()
                            }
                            StudioButton {
                                objectName: "onlySelectedColourButton"
                                text: qsTr("Uniquement cette couleur")
                                enabled: studio.hasSelection
                                tooltip: qsTr("N'afficher que les objets de la couleur du premier objet sélectionné")
                                onClicked: {
                                    var o = studio.objects
                                    for (var i = 0; i < o.length; ++i)
                                        if (o[i].selected) { view.showOnlyColour(o[i].color.toString()); return }
                                }
                            }
                            StudioButton {
                                objectName: "onlySelectedTypeButton"
                                text: qsTr("Uniquement ce type de points")
                                enabled: studio.hasSelection
                                onClicked: {
                                    var o = studio.objects
                                    for (var i = 0; i < o.length; ++i)
                                        if (o[i].selected) { view.showOnlyType(o[i].type); return }
                                }
                            }
                        }
                        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.divider }
                        SectionLabel { text: qsTr("RENDU RÉALISTE") }
                        RealisticToggle { objectName: "realisticPanelToggle"; Layout.fillWidth: true }
                        SectionLabel { text: qsTr("CALQUES D'AFFICHAGE") }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 2
                            StudioCheckBox { objectName: "layerStitchesBox"; text: qsTr("Points"); checked: view.layerStitches; onToggled: view.layerStitches = checked; palette.windowText: Theme.foreground }
                            StudioCheckBox { objectName: "layerJumpsBox"; text: qsTr("Sauts"); checked: view.layerJumps; onToggled: view.layerJumps = checked; palette.windowText: Theme.foreground }
                            StudioCheckBox { objectName: "layerHoopBox"; text: qsTr("Cadre"); checked: view.layerHoop; onToggled: view.layerHoop = checked; palette.windowText: Theme.foreground }
                            StudioCheckBox { objectName: "layerDensityBox"; text: qsTr("Densité"); checked: view.layerDensity; onToggled: view.layerDensity = checked; palette.windowText: Theme.foreground }
                            StudioCheckBox { objectName: "layerRiskBox"; text: qsTr("Risque tissu"); checked: view.layerRisk; onToggled: view.layerRisk = checked; palette.windowText: Theme.foreground }
                        }
                        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.divider }
                        SectionLabel { text: qsTr("GRILLE, GUIDES, RÈGLES") }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 2
                            StudioCheckBox { objectName: "gridBox"; text: qsTr("Grille"); checked: view.gridVisible; onToggled: view.gridVisible = checked; palette.windowText: Theme.foreground }
                            StudioCheckBox { objectName: "guidesBox"; text: qsTr("Guides"); checked: view.guidesVisible; onToggled: view.guidesVisible = checked; palette.windowText: Theme.foreground }
                            StudioCheckBox { objectName: "rulersBox"; text: qsTr("Règles"); checked: view.rulersVisible; onToggled: view.rulersVisible = checked; palette.windowText: Theme.foreground }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: qsTr("Pas (mm)"); color: Theme.muted; font.family: Theme.fontFamily; font.pixelSize: Theme.smallSize }
                            StudioTextField { id: stepField; objectName: "gridStepField"; implicitWidth: 70; text: view.gridStepMm; onEditingFinished: view.gridStepMm = parseFloat(text) }
                            Text { text: qsTr("Guide (mm)"); color: Theme.muted; font.family: Theme.fontFamily; font.pixelSize: Theme.smallSize }
                            StudioTextField { id: guideField; objectName: "guideField"; implicitWidth: 70; text: "0" }
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 4
                            StudioButton { objectName: "addVGuideButton"; text: qsTr("Guide vertical"); onClicked: view.addGuide(true, parseFloat(guideField.text)) }
                            StudioButton { objectName: "addHGuideButton"; text: qsTr("Guide horizontal"); onClicked: view.addGuide(false, parseFloat(guideField.text)) }
                            StudioButton { objectName: "clearGuidesButton"; text: qsTr("Effacer les guides"); enabled: view.guides.length > 0; onClicked: view.clearGuides() }
                        }
                        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.divider }
                        SectionLabel { text: qsTr("ZOOM") }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 4
                            StudioButton { objectName: "zoomRectButton"; text: qsTr("Rectangle"); shortcutLabel: "Z"; onClicked: view.zoomRectangle() }
                            StudioButton { objectName: "zoomSelectionButton"; text: qsTr("Sélection"); shortcutLabel: "⇧F"; enabled: studio.hasSelection; onClicked: view.zoomSelection() }
                            StudioButton { objectName: "zoom100Button"; text: qsTr("100 %"); shortcutLabel: "1"; onClicked: view.zoomActualSize() }
                            StudioButton { objectName: "zoomPreviousButton"; text: qsTr("Précédent"); enabled: hub.canvasItem && hub.canvasItem.canZoomPrevious; onClicked: view.zoomPreviousView() }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            StudioTextField { id: calibField; objectName: "calibrateField"; implicitWidth: 70; placeholderText: qsTr("mm réels") }
                            StudioButton { objectName: "calibrateButton"; text: qsTr("Étalonner le 100 % sur 100 mm"); onClicked: view.calibrateZoom100(parseFloat(calibField.text), 100) }
                        }
                        Text {
                            objectName: "viewMessage"
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: view.message
                            color: Theme.muted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.smallSize
                        }
                    }
                }
            }

            SimulationBar {
                visible: view.simActive
                anchors.left: parent.left
                anchors.leftMargin: 18
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 66
                width: Math.min(760, parent.width - 36)
                controller: view
            }
        }
    }
}
