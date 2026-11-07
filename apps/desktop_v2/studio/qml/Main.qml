// SPDX-License-Identifier: Apache-2.0
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import QtQuick.Controls as Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import OpenStitch.Studio 1.0
import "."

Window {
    id: window
    objectName: "studioWindow"
    width: 1440
    height: 900
    minimumWidth: 1000
    minimumHeight: 640
    visible: true
    color: Theme.background
    title: (studio.dirty ? "• " : "") + studio.title + " — OpenStitch Studio"

    readonly property var editController: editor
    readonly property var toolController: typeof tools !== "undefined" ? tools : null
    property int workspaceIndex: 1
    readonly property var workspaces: [
        { name: qsTr("Préparer"), label: qsTr("COMPOSITION"),
          title: qsTr("Repérer les éléments du document"),
          detail: qsTr("Ouvrez un projet .osp et parcourez ses objets. Les couleurs affichées sont celles des fils du projet.") },
        { name: qsTr("Numériser"), label: qsTr("STRUCTURE DES POINTS"),
          title: qsTr("Observer la broderie, fil par fil"),
          detail: qsTr("Les segments proviennent du moteur OpenStitch. Sélectionnez un objet dans la vue ou dans la liste.") },
        { name: qsTr("Contrôler"), label: qsTr("INSPECTION VISUELLE"),
          title: qsTr("Examiner les segments et la sélection"),
          detail: qsTr("Le contour framboise indique la sélection. Zoomez pour observer la structure, sans modifier le document.") },
        { name: qsTr("Produire"), label: qsTr("DOCUMENT SOURCE"),
          title: qsTr("Relire le motif avant production"),
          detail: qsTr("Ce prototype consulte les projets existants. Pour exporter ou simuler la couture, utilisez encore Desktop V2.") }
    ]
    readonly property var workspace: workspaces[workspaceIndex]
    readonly property var objectEntries: studio.objects
    readonly property int selectedCount: {
        var count = 0
        for (var i = 0; i < objectEntries.length; ++i)
            if (objectEntries[i].selected)
                ++count
        return count
    }

    function openProject() {
        if (!studio.busy)
            filePanel.requestOpen()
    }

    FileDialog {
        id: projectDialog
        title: qsTr("Ouvrir un projet OpenStitch")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Projet OpenStitch (*.osp)")]
        onAccepted: filePanel.openUrl(selectedFile)
    }

    function saveProject() {
        if (studio.hasPath())
            studio.saveProject()
        else
            saveDialog.open()
    }

    FileDialog {
        id: saveDialog
        title: qsTr("Enregistrer le projet")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "osp"
        nameFilters: [qsTr("Projet OpenStitch (*.osp)")]
        onAccepted: studio.saveProjectAs(selectedFile)
    }

    FilePanel { id: filePanel }
    onClosing: function (close) { if (!filePanel.requestQuit()) close.accepted = false }
    ActionPalette { id: palette }
    Shortcut { sequence: "Ctrl+K"; onActivated: palette.openList() }
    Shortcut { sequence: "Ctrl+N"; enabled: !studio.busy; onActivated: filePanel.requestNew() }
    Shortcut { sequence: "Ctrl+S"; enabled: !studio.busy; onActivated: window.saveProject() }
    Shortcut { sequence: "Ctrl+Shift+S"; enabled: !studio.busy; onActivated: saveDialog.open() }
    Shortcut { sequence: "Ctrl+Z"; enabled: studio.canUndo; onActivated: studio.undo() }
    Shortcut { sequence: "Ctrl+Y"; enabled: studio.canRedo; onActivated: studio.redo() }
    Shortcut { sequence: "Ctrl+Shift+Z"; enabled: studio.canRedo; onActivated: studio.redo() }
    // Arrow keys nudge the selection by 1 mm (Shift: 5 mm); Y is up in model space.
    Shortcut { sequence: "Left"; enabled: studio.hasSelection; onActivated: studio.moveSelection(-1, 0) }
    Shortcut { sequence: "Right"; enabled: studio.hasSelection; onActivated: studio.moveSelection(1, 0) }
    Shortcut { sequence: "Up"; enabled: studio.hasSelection; onActivated: studio.moveSelection(0, 1) }
    Shortcut { sequence: "Down"; enabled: studio.hasSelection; onActivated: studio.moveSelection(0, -1) }
    Shortcut { sequence: "Shift+Left"; enabled: studio.hasSelection; onActivated: studio.moveSelection(-5, 0) }
    Shortcut { sequence: "Shift+Right"; enabled: studio.hasSelection; onActivated: studio.moveSelection(5, 0) }
    Shortcut { sequence: "Shift+Up"; enabled: studio.hasSelection; onActivated: studio.moveSelection(0, 5) }
    Shortcut { sequence: "Shift+Down"; enabled: studio.hasSelection; onActivated: studio.moveSelection(0, -5) }
    Shortcut { sequence: "Ctrl+O"; enabled: !studio.busy; onActivated: window.openProject() }
    Shortcut { sequence: "F"; onActivated: canvas.fitDesign() }
    Shortcut { sequence: studio.selectionTool.shortcut; onActivated: canvas.panMode = false }
    Shortcut { sequence: studio.panTool.shortcut; onActivated: canvas.panMode = true }
    Shortcut { sequence: "Escape"; onActivated: { editor.eyedropper = false; if (!canvas.cancelGesture()) studio.clearSelection() } }
    Shortcut { sequence: "Ctrl+A"; onActivated: editor.selectAll() }
    Shortcut { sequence: "Ctrl+I"; onActivated: editor.invertSelection() }
    // One focus-independent handler: selected path nodes first, otherwise the selected objects.
    Shortcut {
        sequence: "Delete"
        enabled: studio.hasSelection || (toolController !== null && toolController.selectedNodeCount > 0)
        onActivated: {
            if (toolController !== null && toolController.selectedNodeCount > 0)
                toolController.deleteSelectedNodes()
            else
                editor.deleteSelection()
        }
    }
    Shortcut { sequence: "Ctrl+D"; enabled: studio.hasSelection; onActivated: editor.duplicate(5) }
    Shortcut { sequence: "Ctrl+G"; enabled: editor.selectedCount > 1; onActivated: editor.group("") }
    Shortcut { sequence: "Ctrl+Shift+G"; enabled: studio.hasSelection; onActivated: editor.ungroup() }
    Shortcut { sequence: "Ctrl+R"; enabled: studio.hasSelection; onActivated: editor.rotate90(true) }
    Shortcut { sequence: "Ctrl+Shift+R"; enabled: studio.hasSelection; onActivated: editor.rotate90(false) }
    Shortcut { sequence: "Ctrl+Shift+H"; enabled: studio.hasSelection; onActivated: editor.mirror(true) }
    Shortcut { sequence: "Ctrl+Shift+V"; enabled: studio.hasSelection; onActivated: editor.mirror(false) }
    Shortcut { sequence: "PgUp"; enabled: studio.hasSelection; onActivated: editor.stack("forward") }
    Shortcut { sequence: "PgDown"; enabled: studio.hasSelection; onActivated: editor.stack("backward") }
    Shortcut { sequence: "End"; enabled: studio.hasSelection; onActivated: editor.stack("front") }
    Shortcut { sequence: "Home"; enabled: studio.hasSelection; onActivated: editor.stack("back") }
    Shortcut { sequence: "I"; enabled: studio.hasSelection; onActivated: editor.eyedropper = !editor.eyedropper }
    property bool treeMode: false
    property bool editOpen: false
    Shortcut { sequence: "Ctrl+1"; onActivated: window.workspaceIndex = 0 }
    Shortcut { sequence: "Ctrl+2"; onActivated: window.workspaceIndex = 1 }
    Shortcut { sequence: "Ctrl+3"; onActivated: window.workspaceIndex = 2 }
    Shortcut { sequence: "Ctrl+4"; onActivated: window.workspaceIndex = 3 }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 66
            color: Theme.panel

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                spacing: 12

                StudioIcon { name: "needle"; color: Theme.accent; implicitWidth: 28; implicitHeight: 28 }
                ColumnLayout {
                    spacing: 1
                    Text {
                        text: "OpenStitch"
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: "STUDIO"
                        color: Theme.faint
                        font.family: Theme.fontFamily
                        font.pixelSize: 9
                        font.letterSpacing: 2.4
                    }
                }

                Item { Layout.fillWidth: true; Layout.minimumWidth: 10 }

                RowLayout {
                    spacing: 3
                    Repeater {
                        model: window.workspaces
                        delegate: Controls.AbstractButton {
                            id: workspaceButton
                            required property int index
                            required property var modelData
                            readonly property bool active: window.workspaceIndex === index
                            Layout.preferredHeight: 38
                            Layout.preferredWidth: window.width < 1180 ? 96 : 113
                            hoverEnabled: true
                            focusPolicy: Qt.StrongFocus
                            Accessible.name: modelData.name
                            Accessible.description: qsTr("Contexte du même document. Raccourci Ctrl+") + (index + 1)
                            onClicked: window.workspaceIndex = index
                            background: Rectangle {
                                color: workspaceButton.active ? Theme.accentSoft
                                       : workspaceButton.hovered ? Theme.raised : "transparent"
                                radius: Theme.radius
                                border.width: workspaceButton.activeFocus ? 1 : 0
                                border.color: Theme.accent
                            }
                            contentItem: Text {
                                text: workspaceButton.modelData.name
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                color: workspaceButton.active ? Theme.accent : Theme.muted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.bodySize
                                font.weight: workspaceButton.active ? Font.DemiBold : Font.Normal
                            }
                            Controls.ToolTip.visible: hovered
                            Controls.ToolTip.text: modelData.name + "  ·  Ctrl+" + (index + 1)
                            Controls.ToolTip.delay: 600
                        }
                    }
                }

                Item { Layout.fillWidth: true; Layout.minimumWidth: 10 }

                StudioButton {
                    objectName: "undoButton"
                    text: qsTr("Annuler")
                    iconName: "undo"
                    tooltip: studio.canUndo ? qsTr("Annuler : %1").arg(studio.undoLabel) : qsTr("Rien à annuler")
                    shortcutLabel: "Ctrl+Z"
                    enabled: studio.canUndo
                    onClicked: studio.undo()
                }

                StudioButton {
                    objectName: "redoButton"
                    text: qsTr("Rétablir")
                    iconName: "redo"
                    tooltip: studio.canRedo ? qsTr("Rétablir : %1").arg(studio.redoLabel) : qsTr("Rien à rétablir")
                    shortcutLabel: "Ctrl+Y"
                    enabled: studio.canRedo
                    onClicked: studio.redo()
                }

                StudioButton {
                    objectName: "fileMenuButton"
                    text: qsTr("Fichier")
                    iconName: "open"
                    tooltip: qsTr("Récents, imports et exports")
                    enabled: !studio.busy
                    onClicked: filePanel.openMenu()
                }

                StudioButton {
                    objectName: "saveProjectButton"
                    text: qsTr("Enregistrer")
                    iconName: "save"
                    tooltip: qsTr("Enregistrer le projet (Ctrl+Maj+S : sous…)")
                    shortcutLabel: "Ctrl+S"
                    enabled: !studio.busy
                    onClicked: window.saveProject()
                }

                StudioButton {
                    objectName: "openProjectButton"
                    text: qsTr("Ouvrir")
                    iconName: "open"
                    tooltip: qsTr("Ouvrir un projet .osp")
                    shortcutLabel: "Ctrl+O"
                    enabled: !studio.busy
                    onClicked: window.openProject()
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.divider }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 42
            color: Theme.surface
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                spacing: 10
                StudioIcon { name: "layers"; implicitWidth: 16; implicitHeight: 16; color: Theme.faint }
                Text {
                    Layout.fillWidth: true
                    text: studio.title
                    elide: Text.ElideMiddle
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.bodySize
                }
                Text {
                    text: qsTr("UN DOCUMENT · QUATRE CONTEXTES")
                    visible: window.width >= 1160
                    color: Theme.faint
                    font.family: Theme.fontFamily
                    font.pixelSize: 9
                    font.letterSpacing: 1.1
                }
                Rectangle { implicitWidth: 1; implicitHeight: 14; color: Theme.border; Layout.leftMargin: 8; Layout.rightMargin: 8 }
                Text {
                    text: studio.dirty ? qsTr("Modifié") : qsTr("Enregistré")
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallSize
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.divider }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
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
                        objectName: "selectToolButton"
                        iconName: "select"
                        tooltip: studio.selectionTool.label
                        shortcutLabel: studio.selectionTool.shortcut
                        quiet: true
                        active: !canvas.panMode
                        onClicked: { canvas.panMode = false; if (window.toolController) window.toolController.activeTool = "select" }
                    }
                    StudioButton {
                        objectName: "panToolButton"
                        iconName: "pan"
                        tooltip: studio.panTool.label
                        shortcutLabel: studio.panTool.shortcut
                        quiet: true
                        active: canvas.panMode
                        onClicked: { canvas.panMode = true; if (window.toolController) window.toolController.activeTool = "select" }
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
                        visible: window.toolController !== null
                        Layout.alignment: Qt.AlignHCenter
                        Layout.topMargin: 4
                        Layout.bottomMargin: 4
                        implicitWidth: 24
                        implicitHeight: 1
                        color: Theme.divider
                    }
                    StudioButton {
                        objectName: "fitToolButton"
                        iconName: "fit"
                        tooltip: qsTr("Ajuster le motif à la fenêtre")
                        shortcutLabel: "F"
                        quiet: true
                        onClicked: canvas.fitDesign()
                    }
                }
                Text {
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 16
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "mm"
                    color: Theme.faint
                    font.family: Theme.fontFamily
                    font.pixelSize: 10
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
                        SectionLabel { text: window.workspace.label }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: qsTr("Segments de broderie")
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
                        id: canvas
                        objectName: "studioCanvas"
                        anchors.fill: parent
                        controller: studio
                        editor: window.editController
                        tools: window.toolController
                        focus: true
                        onContextMenuRequested: function(x, y, objectId) { contextMenu.openAt(x, y) }
                    }

                    ToolOverlay {
                        objectName: "toolOverlay"
                        anchors.fill: parent
                        gpu: canvas
                    }

                    DetailTools { anchors.fill: parent; canvasItem: canvas }

                    TransformHandles {
                        objectName: "transformHandles"
                        anchors.fill: parent
                        canvas: canvas
                    }

                    CanvasContextMenu { id: contextMenu }

                    DigitizeHub {
                        anchors.fill: parent
                    }

                    ViewPanel {
                        objectName: "viewHub"
                        anchors.fill: parent
                        canvasItem: canvas
                    }

                    PreparePanel {
                        objectName: "preparePanel"
                        anchors.fill: parent
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
                            font.pixelSize: 23
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Ouvrez une broderie OpenStitch pour explorer\nsa structure sur le canevas du Studio.")
                            color: Theme.muted
                            horizontalAlignment: Text.AlignHCenter
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
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
                            onClicked: window.openProject()
                        }
                        RowLayout {
                            Layout.alignment: Qt.AlignHCenter
                            spacing: 8
                            StudioButton {
                                objectName: "welcomeImportImage"
                                text: qsTr("Importer une image ou un SVG")
                                onClicked: filePanel.importImageDialog()
                            }
                            StudioButton {
                                objectName: "welcomeImportMachine"
                                text: qsTr("Importer une broderie machine")
                                onClicked: filePanel.importMachineDialog()
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
                                onClicked: canvas.zoomAt(0.8, canvas.width / 2, canvas.height / 2)
                            }
                            Text {
                                Layout.minimumWidth: 72
                                horizontalAlignment: Text.AlignHCenter
                                text: canvas.zoom.toFixed(2) + " px/mm"
                                color: Theme.muted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.smallSize
                            }
                            StudioButton {
                                iconName: "plus"
                                quiet: true
                                implicitHeight: 32
                                tooltip: qsTr("Agrandir la vue")
                                onClicked: canvas.zoomAt(1.25, canvas.width / 2, canvas.height / 2)
                            }
                            Rectangle { implicitWidth: 1; implicitHeight: 16; color: Theme.divider; Layout.leftMargin: 4; Layout.rightMargin: 4 }
                            StudioButton {
                                text: qsTr("Ajuster")
                                quiet: true
                                implicitHeight: 32
                                tooltip: qsTr("Ajuster le motif à la fenêtre")
                                shortcutLabel: "F"
                                onClicked: canvas.fitDesign()
                            }
                        }
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.rightMargin: 20
                        anchors.bottomMargin: 29
                        visible: viewport.width >= 670
                        text: canvas.panMode ? qsTr("Glisser pour déplacer · Molette pour zoomer")
                                             : qsTr("Clic pour sélectionner · Molette pour zoomer")
                        color: Theme.faint
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.smallSize
                    }
                }
            }

            Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: Theme.divider }

            Rectangle {
                Layout.preferredWidth: window.width < 1180 ? 250 : Theme.inspectorWidth
                Layout.fillHeight: true
                color: Theme.panel

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 48
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 18
                            anchors.rightMargin: 18
                            Text {
                                text: qsTr("Objets")
                                color: Theme.foreground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.headingSize
                                font.weight: Font.DemiBold
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: window.objectEntries.length
                                color: Theme.faint
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.bodySize
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.divider }

                    Row {
                        Layout.fillWidth: true
                        Layout.margins: 6
                        spacing: 4
                        StudioButton {
                            objectName: "listModeButton"
                            text: qsTr("Liste")
                            implicitHeight: 26
                            active: !window.treeMode
                            onClicked: window.treeMode = false
                        }
                        StudioButton {
                            objectName: "treeModeButton"
                            text: qsTr("Arbre et calques")
                            implicitHeight: 26
                            active: window.treeMode
                            onClicked: window.treeMode = true
                        }
                        StudioButton {
                            objectName: "editOpenButton"
                            text: qsTr("Édition")
                            implicitHeight: 26
                            active: window.editOpen
                            tooltip: qsTr("Afficher les actions d'édition de la sélection")
                            onClicked: window.editOpen = !window.editOpen
                        }
                    }

                    LayerPanel {
                        objectName: "layerPanel"
                        visible: window.treeMode
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 100
                    }

                    ListView {
                        id: objectList
                        objectName: "objectList"
                        visible: !window.treeMode
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 100
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        model: window.objectEntries
                        spacing: 1
                        topMargin: 7
                        bottomMargin: 7
                        Controls.ScrollBar.vertical: Controls.ScrollBar { }

                        delegate: Controls.ItemDelegate {
                            id: objectRow
                            required property var modelData
                            required property int index
                            width: objectList.width
                            height: Theme.rowHeight
                            leftPadding: 18
                            rightPadding: 18
                            hoverEnabled: true
                            focusPolicy: Qt.StrongFocus
                            Accessible.name: modelData.label
                            Accessible.description: modelData.type
                            Accessible.selected: modelData.selected
                            onClicked: studio.selectObject(modelData.id, false)

                            background: Rectangle {
                                color: objectRow.modelData.selected ? Theme.accentSoft
                                       : objectRow.hovered ? Theme.raised : "transparent"
                                Rectangle {
                                    width: 2
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    color: Theme.accent
                                    visible: objectRow.modelData.selected
                                }
                                border.width: objectRow.activeFocus ? 1 : 0
                                border.color: Theme.accent
                            }

                            contentItem: RowLayout {
                                spacing: 10
                                Rectangle {
                                    implicitWidth: 12
                                    implicitHeight: 12
                                    radius: 3
                                    color: objectRow.modelData.color
                                    border.width: 1
                                    border.color: "#66858d9e"
                                    opacity: objectRow.modelData.visible ? 1 : 0.4
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text {
                                        Layout.fillWidth: true
                                        text: objectRow.modelData.label
                                        color: objectRow.modelData.selected ? Theme.foreground : Theme.muted
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.bodySize
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: objectRow.modelData.type
                                              + (objectRow.modelData.visible ? "" : qsTr(" · masqué"))
                                        color: Theme.faint
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 9
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton
                                onClicked: function(mouse) {
                                    objectRow.forceActiveFocus()
                                    studio.selectObject(objectRow.modelData.id,
                                                        Boolean(mouse.modifiers & Qt.ControlModifier))
                                }
                            }
                            Controls.ToolTip.visible: hovered
                            Controls.ToolTip.delay: 700
                            Controls.ToolTip.text: modelData.label + "\n" + modelData.id
                        }

                        Text {
                            anchors.centerIn: parent
                            width: parent.width - 36
                            visible: objectList.count === 0
                            text: studio.commandCount > 0 ? qsTr("Aucun objet source\ndans ce projet.")
                                                          : qsTr("Les objets du projet\napparaîtront ici.")
                            color: Theme.faint
                            horizontalAlignment: Text.AlignHCenter
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.bodySize
                            lineHeight: 1.4
                        }
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.divider }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.margins: 18
                        spacing: 11
                        RowLayout {
                            Layout.fillWidth: true
                            SectionLabel { text: qsTr("SÉLECTION") }
                            Item { Layout.fillWidth: true }
                            StudioButton {
                                visible: window.selectedCount > 0
                                iconName: "close"
                                tooltip: qsTr("Effacer la sélection")
                                shortcutLabel: qsTr("Échap")
                                quiet: true
                                implicitWidth: 24
                                implicitHeight: 24
                                onClicked: studio.clearSelection()
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: window.selectedCount > 0 ? studio.selectionInfo
                                                           : qsTr("Cliquez sur un fil ou choisissez un objet dans la liste.")
                            color: window.selectedCount > 0 ? Theme.foreground : Theme.faint
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.bodySize
                            wrapMode: Text.Wrap
                            maximumLineCount: window.height < 760 ? 8 : 12
                            elide: Text.ElideRight
                            lineHeight: 1.35
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Ctrl + clic pour une sélection multiple")
                            color: Theme.faint
                            font.family: Theme.fontFamily
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                        }
                    }

                    Inspector {
                        Layout.fillWidth: true
                        Layout.preferredHeight: active ? Math.min(implicitHeight + 24, window.height * 0.5) : 0
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.divider }

                    EditBar {
                        visible: window.editOpen
                        Layout.fillWidth: true
                        Layout.margins: 12
                    }

                    ToolOptions {
                        visible: window.toolController !== null && window.editOpen
                        Layout.fillWidth: true
                        Layout.margins: 12
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.divider }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.margins: 18
                        spacing: 9
                        SectionLabel { text: window.workspace.name.toUpperCase() }
                        Text {
                            Layout.fillWidth: true
                            text: window.workspace.title
                            color: Theme.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.bodySize
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: window.height >= 760
                            text: window.workspace.detail
                            color: Theme.faint
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.smallSize
                            wrapMode: Text.WordWrap
                            lineHeight: 1.4
                        }
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.divider }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            color: Theme.surface
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 12
                Text {
                    Layout.fillWidth: true
                    text: studio.status
                    elide: Text.ElideRight
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallSize
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
                Text {
                    text: studio.commandCount.toLocaleString(Qt.locale(), "f", 0) + qsTr(" commandes")
                    color: Theme.faint
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallSize
                }
                Rectangle { implicitWidth: 1; implicitHeight: 12; color: Theme.border }
                Text {
                    objectName: "renderBackendLabel"
                    text: canvas.backendName
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallSize
                }
                Text {
                    visible: window.width >= 1180
                    text: qsTr("Géométrie · ") + canvas.geometryBuildCount
                    color: Theme.faint
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallSize
                }
            }
        }
    }
}
