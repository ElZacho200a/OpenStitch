// SPDX-License-Identifier: Apache-2.0
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import OpenStitch.Studio 1.0
import ".."
import "points"

// Espace « Points » : tout ce qui concerne les types de points et leur réglage.
//
//   rail d'outils de points | barre de panneaux
//   (gauche)                | barre d'options contextuelle
//                           | CANEVAS (points, rendu réaliste) + UN seul panneau latéral
//                           |                                    | objets de broderie
//                           |                                    | inspecteur (type, réglages)
//
// Les outils de dessin vectoriel n'y sont plus : ils appartiennent à l'espace Vecteurs. Contrat des
// espaces : docs/desktop-v2/23-espaces-studio.md.
Item {
    id: root
    objectName: "pointsSpace"
    required property var workspace
    property bool active: false
    readonly property var canvas: spaceCanvas
    readonly property var editorController: editor
    readonly property var digitizeController: typeof digitizer !== "undefined" ? digitizer : null
    readonly property var toolController: typeof tools !== "undefined" ? tools : null
    readonly property var spaceController: typeof pointsSpace !== "undefined" ? pointsSpace : null
    readonly property int selectedCount: {
        var count = 0
        const entries = studio.objects
        for (var i = 0; i < entries.length; ++i)
            if (entries[i].selected)
                ++count
        return count
    }
    readonly property var threadController: typeof threads !== "undefined" ? threads : null

    ColorDialog {
        id: threadColorDialog
        objectName: "pointsThreadColorDialog"
        property string entryId
        title: qsTr("Couleur du fil")
        function edit(entry) {
            entryId = entry.id
            selectedColor = entry.color
            open()
        }
        onAccepted: {
            const index = swatches.model.findIndex(entry => entry.id === entryId)
            if (index >= 0 && selectedColor.toString() !== swatches.model[index].color.toString())
                root.threadController.recolorThread(index, selectedColor.toString())
        }
    }

    // Panneau latéral ouvert ("" = aucun) : un seul à la fois, tous hubs confondus.
    readonly property string openPanel: digitizeHub.openPanel !== "" ? digitizeHub.openPanel : viewHub.openPanel

    // Deux hubs, jamais deux tiroirs : dès qu'un hub ouvre un panneau, l'autre se ferme (que
    // l'ouverture vienne d'un bouton, d'un raccourci ou de `--open-panels`).
    Connections {
        target: digitizeHub
        function onOpenPanelChanged() { if (digitizeHub.openPanel !== "") viewHub.openPanel = "" }
    }
    Connections {
        target: viewHub
        function onOpenPanelChanged() { if (viewHub.openPanel !== "") digitizeHub.openPanel = "" }
    }

    // Publication de l'état de l'étape « points ». La publication ne remplace le calcul de base
    // que lorsque l'amont (les formes) est sain : sinon le bandeau de l'amont garde la main.
    property string publishedKey: ""
    function publishStage() {
        if (!root.active) return
        if (!root.spaceController) {
            root.workspace.clearStageStatus("points")
            return
        }
        const upstream = root.workspace.space("vector").state
        const state = root.spaceController.state
        const key = upstream + "|" + state + "|" + root.spaceController.summary
        if (key === publishedKey)
            return
        publishedKey = key
        if (upstream === "stale" || upstream === "blocked")
            root.workspace.clearStageStatus("points")
        else
            root.workspace.setStageStatus("points", state, root.spaceController.summary)
    }
    Connections {
        target: root.spaceController
        function onChanged() { root.publishStage() }
    }
    Connections {
        target: root.workspace
        function onSpacesChanged() { root.publishStage() }
    }
    function activate() {
        root.publishedKey = ""
        root.publishStage()
        // La caméra propre à cet espace prime sur la vue partagée lors d'un retour.
        if (root.spaceController && Object.keys(root.spaceController.camera).length > 0)
            Qt.callLater(function() {
                if (root.active && root.spaceController && root.canvas)
                    root.canvas.restoreViewState(root.spaceController.camera)
            })
    }
    function deactivate() {
        if (root.spaceController && root.canvas)
            root.spaceController.rememberCamera(root.canvas.viewState())
        root.workspace.clearStageStatus("points")
    }
    onActiveChanged: { if (active) activate(); else deactivate() }
    Component.onCompleted: { if (active) activate() }
    Component.onDestruction: deactivate()

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: Theme.controlHeight + 24
            Layout.fillHeight: true
            color: Theme.panel
            PointsToolRail {
                workspaceActive: root.active
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.topMargin: 12
                canvas: spaceCanvas
            }
            Text {
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 16
                anchors.horizontalCenter: parent.horizontalCenter
                text: "mm"
                color: Theme.faint
                font.family: Theme.fontFamily
                font.pixelSize: Theme.captionSize
            }
        }

        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: Theme.divider }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            PointsPanelBar {
                Layout.fillWidth: true
                digitizeHub: digitizeHub
                viewHub: viewHub
            }
            PointsOptionsBar { Layout.fillWidth: true }

            Item {
                id: viewport
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                GpuCanvas {
                    id: spaceCanvas
                    objectName: "studioCanvas"
                    anchors.fill: parent
                    controller: studio
                    editor: root.editorController
                    tools: root.toolController
                    digitize: root.digitizeController
                    focus: true
                    onContextMenuRequested: function(x, y, objectId) { contextMenu.openAt(x, y) }
                }

                HandlesOverlay {
                    objectName: "handlesOverlay"
                    anchors.fill: parent
                    gpu: spaceCanvas
                }

                TransformHandles {
                    objectName: "transformHandles"
                    anchors.fill: parent
                    canvas: spaceCanvas
                }

                CanvasContextMenu { id: contextMenu }

                // Les deux hubs ne dessinent plus de boutons : la barre de panneaux les pilote.
                DigitizeHub {
                    id: digitizeHub
                    anchors.fill: parent
                    toolbarVisible: false
                }

                ViewPanel {


                    active: root.active
                    id: viewHub
                    objectName: "viewHub"
                    anchors.fill: parent
                    canvasItem: spaceCanvas
                    toolbarVisible: false
                }

                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.min(400, parent.width - 64)
                    spacing: 16
                    visible: studio.commandCount === 0 && !studio.busy

                    StudioIcon {
                        name: "needle"
                        implicitWidth: 46
                        implicitHeight: 46
                        color: Theme.accent
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Votre prochain fil commence ici.")
                        color: Theme.foreground
                        horizontalAlignment: Text.AlignHCenter
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.heroSize
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Ouvrez une broderie OpenStitch pour explorer\nsa structure sur le canevas du Studio.")
                        color: Theme.muted
                        horizontalAlignment: Text.AlignHCenter
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.bodySize
                        lineHeight: 1.4
                        wrapMode: Text.WordWrap
                    }
                    StudioButton {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.topMargin: 7
                        emphasized: true
                        iconName: "open"
                        text: qsTr("Ouvrir un projet .osp")
                        tooltip: qsTr("Ouvrir un projet OpenStitch existant")
                        onClicked: actions.runCommand("cmd.file.open")
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 8
                        StudioButton {
                            objectName: "welcomeImportImage"
                            text: qsTr("Importer une image ou un SVG")
                            onClicked: actions.runCommand("cmd.file.import-image")
                        }
                        StudioButton {
                            objectName: "welcomeImportMachine"
                            text: qsTr("Importer une broderie machine")
                            onClicked: actions.runCommand("cmd.file.import-machine")
                        }
                    }
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: loadingRow.implicitWidth + 36
                    height: 54
                    radius: Theme.radius
                    color: Theme.surface
                    border.color: Theme.border
                    visible: studio.busy
                    RowLayout {
                        id: loadingRow
                        anchors.centerIn: parent
                        spacing: 14
                        Controls.BusyIndicator {
                            implicitWidth: 26
                            implicitHeight: 26
                            running: studio.busy
                            palette.dark: Theme.accent
                        }
                        Text {
                            text: qsTr("Chargement du projet…")
                            color: Theme.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.bodySize
                        }
                        StudioButton {
                            text: qsTr("Annuler")
                            tooltip: qsTr("Annuler le chargement")
                            quiet: true
                            onClicked: studio.cancel()
                        }
                    }
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.margins: 18
                    width: viewControls.implicitWidth + 8
                    height: 40
                    radius: Theme.radius
                    color: Theme.panel
                    border.color: Theme.divider
                    RowLayout {
                        id: viewControls
                        anchors.centerIn: parent
                        spacing: 0
                        StudioButton {
                            iconName: "minus"
                            quiet: true
                            implicitHeight: 32
                            tooltip: qsTr("Réduire la vue")
                            onClicked: spaceCanvas.zoomAt(0.8, spaceCanvas.width / 2, spaceCanvas.height / 2)
                        }
                        Text {
                            Layout.minimumWidth: 72
                            horizontalAlignment: Text.AlignHCenter
                            text: spaceCanvas.zoom.toFixed(2) + " px/mm"
                            color: Theme.muted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.smallSize
                        }
                        StudioButton {
                            iconName: "plus"
                            quiet: true
                            implicitHeight: 32
                            tooltip: qsTr("Agrandir la vue")
                            onClicked: spaceCanvas.zoomAt(1.25, spaceCanvas.width / 2, spaceCanvas.height / 2)
                        }
                        Rectangle { implicitWidth: 1; implicitHeight: 16; color: Theme.divider; Layout.leftMargin: 4; Layout.rightMargin: 4 }
                        StudioButton {
                            text: qsTr("Ajuster")
                            quiet: true
                            implicitHeight: 32
                            tooltip: qsTr("Ajuster le motif à la fenêtre")
                            shortcutLabel: "F"
                            onClicked: spaceCanvas.fitDesign()
                        }
                    }
                }
            }

            Rectangle {
                id: threadStrip
                objectName: "pointsThreadStrip"
                visible: swatches.count > 0
                Layout.fillWidth: true
                implicitHeight: 48
                color: Theme.panel
                border.color: Theme.divider

                ListView {
                    id: swatches
                    anchors.fill: parent
                    anchors.margins: 6
                    orientation: ListView.Horizontal
                    boundsBehavior: Flickable.StopAtBounds
                    clip: true
                    spacing: 4
                    model: root.threadController ? root.threadController.entries : []
                    property int dragFrom: -1
                    property int dropIndex: -1
                    property real pointerX: 0
                    readonly property int tileWidth: 32
                    onModelChanged: {
                        if (stripMouse)
                            stripMouse.pressedIndex = -1
                        dragFrom = -1
                        dropIndex = -1
                    }

                    function indexAtX(x, clamp) {
                        if (x < 0 || x >= width)
                            return -1
                        const offset = contentX + x - originX
                        const stride = tileWidth + spacing
                        const index = Math.floor(offset / stride)
                        if (clamp)
                            return Math.min(count - 1, index)
                        return index < count && offset % stride < tileWidth ? index : -1
                    }

                    Timer {
                        interval: 25
                        repeat: true
                        running: swatches.dragFrom >= 0 && swatches.dropIndex >= 0
                                 && swatches.contentWidth > swatches.width
                                 && (swatches.pointerX < 18 || swatches.pointerX > swatches.width - 18)
                        onTriggered: {
                            const delta = swatches.pointerX < 18 ? -8 : 8
                            swatches.contentX = Math.max(swatches.originX,
                                Math.min(swatches.originX + swatches.contentWidth - swatches.width,
                                         swatches.contentX + delta))
                            swatches.dropIndex = swatches.indexAtX(swatches.pointerX, true)
                        }
                    }

                    Controls.ScrollBar.horizontal: Controls.ScrollBar {}
                    delegate: Rectangle {
                        id: swatch
                        required property var modelData
                        objectName: "pointsThreadSwatch" + modelData.index
                        width: swatches.tileWidth
                        height: 28
                        radius: 4
                        color: modelData.color
                        opacity: modelData.visible && swatches.dragFrom !== modelData.index ? 1 : 0.5
                        border.width: modelData.selected || activeFocus || swatches.dropIndex === modelData.index ? 2 : 1
                        border.color: swatches.dropIndex === modelData.index ? Theme.accent
                                    : modelData.selected || activeFocus ? Theme.foreground : Theme.border
                        activeFocusOnTab: true
                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("Fil %1 · %2 objet(s)").arg(modelData.label).arg(modelData.objectCount)
                        Accessible.onPressAction: root.threadController.selectThreadObjects(modelData.index)
                        Keys.onSpacePressed: root.threadController.selectThreadObjects(modelData.index)

                        Keys.onPressed: event => {
                            if (event.key === Qt.Key_F2) {
                                threadColorDialog.edit(modelData)
                                event.accepted = true
                            }
                        }
                    }
                }

                MouseArea {
                    id: stripMouse
                    anchors.fill: swatches
                    anchors.bottomMargin: 8
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    hoverEnabled: true
                    property real pressX
                    property int pressedIndex: -1
                    readonly property int hoveredIndex: containsMouse ? swatches.indexAtX(mouseX, false) : -1
                    readonly property bool hovered: hoveredIndex >= 0 && swatches.dragFrom < 0 && !pressed
                    cursorShape: swatches.dragFrom >= 0 ? Qt.ClosedHandCursor
                                 : hoveredIndex >= 0 ? Qt.PointingHandCursor : Qt.ArrowCursor

                    onPressed: function(event) {
                        pressX = event.x
                        pressedIndex = swatches.indexAtX(event.x, false)
                        event.accepted = pressedIndex >= 0
                    }
                    onPositionChanged: function(event) {
                        if (pressedIndex < 0 || !(pressedButtons & Qt.LeftButton))
                            return
                        if (swatches.dragFrom < 0 && Math.abs(event.x - pressX) < Qt.styleHints.startDragDistance)
                            return
                        swatches.dragFrom = pressedIndex
                        swatches.pointerX = event.x
                        swatches.dropIndex = swatches.indexAtX(event.x, true)
                    }
                    onReleased: function(event) {
                        const from = swatches.dragFrom
                        const to = event.y >= 0 && event.y < height ? swatches.indexAtX(event.x, true) : -1
                        swatches.dragFrom = -1
                        swatches.dropIndex = -1
                        if (from >= 0) {
                            if (to >= 0 && from !== to && !root.threadController.moveThread(from, to))
                                errorTip.show(root.threadController.message, 3000)
                        } else if (pressedIndex >= 0 && pressedIndex === swatches.indexAtX(event.x, false)) {
                            if (event.button === Qt.RightButton)
                                threadColorDialog.edit(swatches.model[pressedIndex])
                            else
                                root.threadController.selectThreadObjects(pressedIndex)
                        }
                        pressedIndex = -1
                    }
                    onCanceled: {
                        pressedIndex = -1
                        swatches.dragFrom = -1
                        swatches.dropIndex = -1
                    }

                    StudioToolTip {
                        text: {
                            const entry = stripMouse.hoveredIndex >= 0
                                        ? swatches.model[stripMouse.hoveredIndex] : null
                            return entry ? qsTr("%1 · %2 objet(s)\nCliquer pour sélectionner · Glisser pour réordonner · Clic droit pour recolorer")
                                           .arg(entry.label).arg(entry.objectCount) : ""
                        }
                    }
                }

                Controls.ToolTip { id: errorTip }
            }
        }

        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: Theme.divider }

        // UN inspecteur contextuel : objets de broderie en haut, type et réglages du point en bas.
        Rectangle {
            Layout.preferredWidth: root.width < 1180 ? 270 : Theme.inspectorWidth + 16
            Layout.fillHeight: true
            color: Theme.panel

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                PointsObjectsPanel {
                    workspace: root.workspace
                    Layout.fillWidth: true
                    Layout.fillHeight: !inspectorArea.active
                    // When EditBar is open, allocate enough room for its own scroll area
                    // and the object tree. The point inspector remains independently scrollable.
                    Layout.minimumHeight: inspectorArea.active ? (root.workspace.editOpen ? 275 : 180) : 0
                    Layout.preferredHeight: inspectorArea.active
                                            ? Math.min(580, root.height * (root.workspace.editOpen ? 0.62 : 0.38))
                                            : -1
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.divider }

                Item {
                    id: inspectorArea
                    objectName: "inspectorArea"
                    readonly property bool active: inspectorPanel.active
                    Layout.fillWidth: true
                    Layout.fillHeight: active
                    Layout.minimumHeight: active ? 150 : 0
                    Layout.preferredHeight: active ? -1 : 0
                    visible: active
                    clip: true
                    Inspector {
                        id: inspectorPanel
                        anchors.fill: parent
                    }
                }

                ColumnLayout {
                    visible: !inspectorArea.active
                    Layout.fillWidth: true
                    Layout.margins: Theme.panelPadding
                    spacing: 6
                    SectionLabel { text: qsTr("TYPE DE POINTS") }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Sélectionnez un objet dans la vue ou dans la liste : son type de points (satin, tatami, fondu, contour…) et tous ses réglages apparaissent ici.")
                        color: Theme.faint
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.smallSize
                        wrapMode: Text.WordWrap
                        lineHeight: 1.35
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Ctrl + clic pour une sélection multiple")
                        color: Theme.faint
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.captionSize
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }
}
