// SPDX-License-Identifier: Apache-2.0
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import QtQuick.Controls as Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import OpenStitch.Studio 1.0
import "."
import "spaces"

Window {
    id: window
    objectName: "studioWindow"
    width: 1440
    height: 900
    minimumWidth: 1000
    minimumHeight: 640
    visible: true
    color: Theme.background
    // Safety net: a Qt control that is not a Studio* component still inherits a dark, readable
    // palette (never the white Basic defaults). The font counterpart is applied in main.cpp.
    palette.window: Theme.background
    palette.windowText: Theme.foreground
    palette.base: Theme.raised
    palette.alternateBase: Theme.surface
    palette.text: Theme.foreground
    palette.placeholderText: Theme.faint
    palette.button: Theme.raised
    palette.buttonText: Theme.foreground
    palette.light: Theme.hover
    palette.midlight: Theme.hover
    palette.mid: Theme.border
    palette.dark: Theme.divider
    palette.shadow: Theme.background
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.accentInk
    palette.link: Theme.info
    palette.toolTipBase: Theme.raised
    palette.toolTipText: Theme.foreground
    title: (studio.dirty ? "• " : "") + studio.title + " — OpenStitch Studio"

    readonly property var editController: editor
    readonly property var toolController: typeof tools !== "undefined" ? tools : null
    // Les quatre espaces (image, segmentation, vector, points) : un seul est instancié à la fois,
    // par SpaceHost ; le canevas courant est celui de l'espace actif (null le temps d'un changement).
    readonly property var workspaceController: workspaceCtl
    readonly property var canvas: spaceHost.canvas
    WorkspaceController { id: workspaceCtl; controller: studio }
    Binding {
        target: workspaceCtl
        property: "segmentationDirty"
        value: typeof regionTools !== "undefined" && regionTools.stale
        when: typeof regionTools !== "undefined"
    }
    Binding {
        target: workspaceCtl
        property: "dirtyReason"
        value: typeof regionTools !== "undefined" ? regionTools.staleReason : ""
        when: typeof regionTools !== "undefined"
    }
    readonly property var spaceIds: ["image", "segmentation", "vector", "points"]
    property string workspaceNotice: ""
    Timer { id: workspaceNoticeTimer; interval: 1400; onTriggered: window.workspaceNotice = "" }


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

    property bool closeApproved: false
    FilePanel {
        id: filePanel
        // A raster import is an image-only operation: enter the first workspace
        // after the project has been successfully adopted.
        onImageImported: workspaceCtl.setCurrent("image")
        onQuitApproved: {
            window.closeApproved = true
            window.close()
        }
    }
    onClosing: function (close) {
        if (!window.closeApproved && !filePanel.requestQuit()) {
            close.accepted = false
            return
        }
        window.closeApproved = false
        window.persistSession()
    }
    ActionPalette { id: palette }
    CompanionPanel { id: companionPanel }
    PreferencesDialog { id: prefsDialog }
    ShortcutsSheet { id: shortcutsSheet }
    HelpDialog { id: helpDialog }

    // Interface preferences (theme, density, reduced motion) drive the Theme singleton.
    readonly property bool hasPrefs: typeof prefs !== "undefined" && prefs !== null
    Binding { target: Theme; property: "mode"; value: window.hasPrefs ? prefs.effectiveTheme : "dark" }
    Binding { target: Theme; property: "densityScale"; value: window.hasPrefs ? prefs.densityScale : 1.0 }
    Binding { target: Theme; property: "reducedMotion"; value: window.hasPrefs ? prefs.reducedMotion : false }

    // Session restore: workspace and panels now, zoom once a document has been framed; the file
    // itself is reopened by the application at launch (main.cpp).
    function persistSession() {
        if (!hasPrefs)
            return
        const file = studio.hasPath() && typeof files !== "undefined" && files.recent.length > 0 ? files.recent[0].path : ""
        prefs.rememberSession({ file: file, workspace: workspaceCtl.currentIndex, space: workspaceCtl.current,
                                editOpen: workspaceCtl.editOpen, treeMode: workspaceCtl.treeMode,
                                zoom: canvas ? canvas.zoom : 1 })
    }
    Connections {
        target: workspaceCtl
        function onCurrentChanged() {
            window.persistSession()
            const labels = { image: qsTr("Image"), segmentation: qsTr("Segmentation"),
                             vector: qsTr("Vecteurs"), points: qsTr("Points") }
            window.workspaceNotice = qsTr("Espace actif : ") + (labels[workspaceCtl.current] || workspaceCtl.current)
            workspaceNoticeTimer.restart()
        }
        function onPanelsChanged() { window.persistSession() }
    }
    Component.onCompleted: {
        if (!hasPrefs)
            return
        const s = prefs.session
        if (s.space !== undefined && spaceIds.indexOf(String(s.space)) >= 0)
            workspaceCtl.setCurrent(String(s.space))
        workspaceCtl.editOpen = s.editOpen === "1"
        workspaceCtl.treeMode = s.treeMode === "1"
    }

    function deleteSelection() {
        if (toolController !== null && toolController.selectedNodeCount > 0)
            toolController.deleteSelectedNodes()
        else
            editor.deleteSelection()
    }

    // The ONE dispatcher behind the palette (and any menu): shortcuts and buttons call the same
    // functions, so every path has the same effect, enabled state and undo step.
    function dispatch(id) {
        if (id.indexOf("cmd.tool.") === 0) {
            if (toolController)
                toolController.activeTool = id.substring(9)
            return
        }
        if (id.indexOf("cmd.workspace.") === 0) {
            const index = parseInt(id.substring(14))
            if (index >= 0 && index < spaceIds.length)
                workspaceCtl.setCurrent(spaceIds[index])
            return
        }
        switch (id) {
        case "cmd.file.new": filePanel.requestNew(); break
        case "cmd.file.open": openProject(); break
        case "cmd.file.save": saveProject(); break
        case "cmd.file.save-as": saveDialog.open(); break
        case "cmd.file.import-image": filePanel.importImageDialog(); break
        case "cmd.file.import-machine": filePanel.importMachineDialog(); break
        case "cmd.edit.undo": studio.undo(); break
        case "cmd.edit.redo": studio.redo(); break
        case "cmd.edit.duplicate": editor.duplicate(5); break
        case "cmd.edit.delete": deleteSelection(); break
        case "cmd.edit.group": editor.group(""); break
        case "cmd.edit.ungroup": editor.ungroup(); break
        case "cmd.edit.rotate-cw": editor.rotate90(true); break
        case "cmd.edit.rotate-ccw": editor.rotate90(false); break
        case "cmd.edit.mirror-h": editor.mirror(true); break
        case "cmd.edit.mirror-v": editor.mirror(false); break
        case "cmd.edit.eyedropper": editor.eyedropper = !editor.eyedropper; break
        case "cmd.select.all": editor.selectAll(); break
        case "cmd.select.invert": editor.invertSelection(); break
        case "cmd.select.clear": studio.clearSelection(); break
        case "cmd.arrange.forward": editor.stack("forward"); break
        case "cmd.arrange.backward": editor.stack("backward"); break
        case "cmd.arrange.front": editor.stack("front"); break
        case "cmd.arrange.back": editor.stack("back"); break
        case "cmd.view.fit": if (canvas) canvas.fitDesign(); break
        case "cmd.view.zoom-in": if (canvas) canvas.zoomAt(1.25, canvas.width / 2, canvas.height / 2); break
        case "cmd.view.zoom-out": if (canvas) canvas.zoomAt(0.8, canvas.width / 2, canvas.height / 2); break
        case "cmd.view.zoom-selection": if (canvas) canvas.zoomToSelection(); break
        case "cmd.prefs.open": prefsDialog.openDialog(); break
        case "cmd.companion.open": companionPanel.openPanel(); break
        case "cmd.companion.toggle":
            if (typeof companion !== "undefined")
                companion.setBridgeEnabled(!companion.bridgeActive)
            break
        case "cmd.help.shortcuts": shortcutsSheet.openSheet(); break
        case "cmd.help.guide": helpDialog.show("guide"); break
        case "cmd.help.tool": helpDialog.show("tool", typeof help !== "undefined" && toolController
                                              ? help.toolHelp(toolController.activeTool, toolController.hint) : ""); break
        case "cmd.help.about": helpDialog.show("about"); break
        }
        // Keyboard control returns to the canvas, except when a dialog just took it.
        if (id.indexOf("cmd.prefs") !== 0 && id.indexOf("cmd.companion") !== 0 && id.indexOf("cmd.help") !== 0)
            if (canvas)
                canvas.forceActiveFocus()
    }
    // Keyboard invariant: the focus never rests on an item the user cannot see (a button of a
    // collapsed panel or of a closed popup). When it does, the canvas takes it back.
    onActiveFocusItemChanged: {
        const item = window.activeFocusItem
        if (item !== null && item !== canvas && !item.visible)
            Qt.callLater(function () {
                if (window.activeFocusItem && !window.activeFocusItem.visible && canvas)
                    canvas.forceActiveFocus()
            })
    }
    Connections {
        target: actions
        function onCommandRequested(id) { window.dispatch(id) }
    }

    // QA-STUDIO-001 KEY-010/CTL-018/019: an editable field, spin box, slider or combo that has
    // the focus owns the arrow / Delete / Home / End / Escape keys; the global canvas shortcuts
    // (nudge, delete, restack) must not fire behind it.
    readonly property bool keyboardOwnedByControl: {
        const item = window.activeFocusItem
        return !!item && (item.cursorPosition !== undefined || item.stepSize !== undefined ||
                          item.popup !== undefined)
    }

    Shortcut { sequence: "Ctrl+K"; onActivated: palette.openList() }
    Shortcut { sequence: "Ctrl+J"; onActivated: companionPanel.openPanel() }
    Shortcut { sequence: "Ctrl+,"; onActivated: prefsDialog.openDialog() }
    Shortcut { sequence: "F1"; onActivated: shortcutsSheet.openSheet() }
    Shortcut { sequence: "Shift+F1"; onActivated: window.dispatch("cmd.help.tool") }
    Shortcut { sequence: "Ctrl+N"; enabled: !studio.busy; onActivated: filePanel.requestNew() }
    Shortcut { sequence: "Ctrl+S"; enabled: !studio.busy; onActivated: window.saveProject() }
    Shortcut { sequence: "Ctrl+Shift+S"; enabled: !studio.busy; onActivated: saveDialog.open() }
    Shortcut { sequence: "Ctrl+Z"; enabled: studio.canUndo; onActivated: studio.undo() }
    Shortcut { sequence: "Ctrl+Y"; enabled: studio.canRedo; onActivated: studio.redo() }
    Shortcut { sequence: "Ctrl+Shift+Z"; enabled: studio.canRedo; onActivated: studio.redo() }
    // Arrow keys nudge the selection by 1 mm (Shift: 5 mm); Y is up in model space.
    Shortcut { sequence: "Left"; enabled: studio.hasSelection && !window.keyboardOwnedByControl; onActivated: studio.moveSelection(-1, 0) }
    Shortcut { sequence: "Right"; enabled: studio.hasSelection && !window.keyboardOwnedByControl; onActivated: studio.moveSelection(1, 0) }
    Shortcut { sequence: "Up"; enabled: studio.hasSelection && !window.keyboardOwnedByControl; onActivated: studio.moveSelection(0, 1) }
    Shortcut { sequence: "Down"; enabled: studio.hasSelection && !window.keyboardOwnedByControl; onActivated: studio.moveSelection(0, -1) }
    Shortcut { sequence: "Shift+Left"; enabled: studio.hasSelection && !window.keyboardOwnedByControl; onActivated: studio.moveSelection(-5, 0) }
    Shortcut { sequence: "Shift+Right"; enabled: studio.hasSelection && !window.keyboardOwnedByControl; onActivated: studio.moveSelection(5, 0) }
    Shortcut { sequence: "Shift+Up"; enabled: studio.hasSelection && !window.keyboardOwnedByControl; onActivated: studio.moveSelection(0, 5) }
    Shortcut { sequence: "Shift+Down"; enabled: studio.hasSelection && !window.keyboardOwnedByControl; onActivated: studio.moveSelection(0, -5) }
    Shortcut { sequence: "Ctrl+O"; enabled: !studio.busy; onActivated: window.openProject() }
    Shortcut { sequence: "F"; onActivated: { if (canvas) canvas.fitDesign() } }
    Shortcut { sequence: studio.selectionTool.shortcut; onActivated: { if (canvas) canvas.panMode = false } }
    Shortcut { sequence: studio.panTool.shortcut; onActivated: { if (canvas) canvas.panMode = true } }
    // Escape inside a field only leaves the field; it must not silently drop the selection.
    Shortcut {
        sequence: "Escape"
        onActivated: {
            if (window.keyboardOwnedByControl) { window.activeFocusItem.focus = false; return }
            editor.eyedropper = false
            if (!canvas || !canvas.cancelGesture()) studio.clearSelection()
        }
    }
    Shortcut { sequence: "Ctrl+A"; onActivated: editor.selectAll() }
    Shortcut { sequence: "Ctrl+I"; onActivated: editor.invertSelection() }
    // One focus-independent handler: selected path nodes first, otherwise the selected objects.
    Shortcut {
        sequence: "Delete"
        enabled: !window.keyboardOwnedByControl &&
                 (studio.hasSelection || (toolController !== null && toolController.selectedNodeCount > 0))
        onActivated: window.deleteSelection()
    }
    Shortcut { sequence: "Ctrl+D"; enabled: studio.hasSelection; onActivated: editor.duplicate(5) }
    Shortcut { sequence: "Ctrl+G"; enabled: editor.selectedCount > 1; onActivated: editor.group("") }
    Shortcut { sequence: "Ctrl+Shift+G"; enabled: studio.hasSelection; onActivated: editor.ungroup() }
    Shortcut { sequence: "Ctrl+R"; enabled: studio.hasSelection; onActivated: editor.rotate90(true) }
    Shortcut { sequence: "Ctrl+Shift+R"; enabled: studio.hasSelection; onActivated: editor.rotate90(false) }
    Shortcut { sequence: "Ctrl+Shift+H"; enabled: studio.hasSelection; onActivated: editor.mirror(true) }
    Shortcut { sequence: "Ctrl+Shift+V"; enabled: studio.hasSelection; onActivated: editor.mirror(false) }
    Shortcut { sequence: "PgUp"; enabled: studio.hasSelection && !window.keyboardOwnedByControl; onActivated: editor.stack("forward") }
    Shortcut { sequence: "PgDown"; enabled: studio.hasSelection && !window.keyboardOwnedByControl; onActivated: editor.stack("backward") }
    Shortcut { sequence: "End"; enabled: studio.hasSelection && !window.keyboardOwnedByControl; onActivated: editor.stack("front") }
    Shortcut { sequence: "Home"; enabled: studio.hasSelection && !window.keyboardOwnedByControl; onActivated: editor.stack("back") }
    Shortcut { sequence: "I"; enabled: studio.hasSelection; onActivated: editor.eyedropper = !editor.eyedropper }
    // Ctrl+1..4 (changement d'espace) : Shortcut de SpaceHost.

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 66
            color: Theme.panel

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.panelPadding
                anchors.rightMargin: Theme.panelPadding
                spacing: 12

                StudioIcon { name: "needle"; color: Theme.accent; implicitWidth: 28; implicitHeight: 28 }
                ColumnLayout {
                    spacing: 1
                    Text {
                        text: "OpenStitch"
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.brandSize
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: "STUDIO"
                        color: Theme.faint
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.captionSize
                        font.letterSpacing: 1.6
                    }
                }

                Item { Layout.fillWidth: true; Layout.minimumWidth: 10 }

                Item { Layout.fillWidth: true; Layout.minimumWidth: 10 }

                StudioButton {
                    objectName: "undoButton"
                    text: qsTr("Annuler")
                    iconName: "undo"
                    tooltip: studio.canUndo ? qsTr("Annuler : %1").arg(studio.undoLabel) : qsTr("Rien à annuler")
                    shortcutLabel: "Ctrl+Z"
                    showShortcutLabel: window.width >= 1400
                    enabled: studio.canUndo
                    onClicked: studio.undo()
                }

                StudioButton {
                    objectName: "redoButton"
                    text: qsTr("Rétablir")
                    iconName: "redo"
                    tooltip: studio.canRedo ? qsTr("Rétablir : %1").arg(studio.redoLabel) : qsTr("Rien à rétablir")
                    shortcutLabel: "Ctrl+Y"
                    showShortcutLabel: window.width >= 1400
                    enabled: studio.canRedo
                    onClicked: studio.redo()
                }

                StudioButton {
                    objectName: "historyToggle"
                    text: qsTr("Historique")
                    iconName: "undo"
                    visible: historyHub.available
                    active: historyHub.opened
                    tooltip: qsTr("Afficher les étapes du document")
                    onClicked: historyHub.opened = !historyHub.opened
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
                    showShortcutLabel: window.width >= 1400
                    enabled: !studio.busy
                    onClicked: window.saveProject()
                }

                StudioButton {
                    objectName: "openProjectButton"
                    text: qsTr("Ouvrir")
                    iconName: "open"
                    tooltip: qsTr("Ouvrir un projet .osp")
                    shortcutLabel: "Ctrl+O"
                    showShortcutLabel: window.width >= 1400
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
                anchors.leftMargin: Theme.panelPadding
                anchors.rightMargin: Theme.panelPadding
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
                Rectangle { implicitWidth: 1; implicitHeight: 14; color: Theme.border; Layout.leftMargin: 8; Layout.rightMargin: 8 }
                Text {
                    text: studio.dirty ? qsTr("Modifications non enregistrées") : qsTr("Aucune modification en attente")
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallSize
                }
                StudioButton {
                    objectName: "paletteButton"
                    text: qsTr("Commandes")
                    quiet: true
                    implicitHeight: 28
                    tooltip: qsTr("Palette de commandes")
                    shortcutLabel: "Ctrl+K"
                    onClicked: palette.openList()
                }
                StudioButton {
                    objectName: "companionButton"
                    text: qsTr("IA")
                    quiet: true
                    implicitHeight: 28
                    tooltip: qsTr("Compagnon IA : discussion, pont MCP, propositions")
                    shortcutLabel: "Ctrl+J"
                    onClicked: companionPanel.openPanel()
                }
                StudioButton {
                    objectName: "prefsButton"
                    text: qsTr("Préférences")
                    quiet: true
                    implicitHeight: 28
                    tooltip: qsTr("Préférences : thème, densité, animations, compagnon IA")
                    shortcutLabel: "Ctrl+,"
                    onClicked: prefsDialog.openDialog()
                }
                StudioButton {
                    objectName: "helpButton"
                    text: qsTr("Aide")
                    quiet: true
                    implicitHeight: 28
                    tooltip: qsTr("Raccourcis, guide de prise en main, À propos")
                    shortcutLabel: "F1"
                    onClicked: shortcutsSheet.openSheet()
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.divider }

        SpaceHost {
            id: spaceHost
            Layout.fillWidth: true
            Layout.fillHeight: true
            workspace: workspaceCtl

            // L’historique du document survit au changement des quatre espaces.
            HistoryPanel { id: historyHub; anchors.fill: parent; z: 10 }
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
                    text: studio.busy ? qsTr("Opération en cours…  ·  ") + studio.status
                                      : window.workspaceNotice.length > 0 ? window.workspaceNotice : studio.status
                    elide: Text.ElideRight
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallSize
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
                Controls.BusyIndicator {
                    objectName: "studioBusyIndicator"
                    visible: studio.busy
                    running: visible && !Theme.reducedMotion
                    implicitWidth: 18
                    implicitHeight: 18
                    Accessible.name: qsTr("Opération en cours")
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
                    text: canvas ? canvas.backendName : ""
                    color: Theme.info
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallSize
                }
                Text {
                    objectName: "realisticModeStatus"
                    visible: typeof view !== "undefined" && view.realistic
                    text: typeof view !== "undefined" ? view.realisticMode : ""
                    color: Theme.accent
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallSize
                }
                Text {
                    visible: window.width >= 1180
                    text: qsTr("Géométrie · ") + (canvas ? canvas.geometryBuildCount : 0)
                    color: Theme.faint
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallSize
                }
            }
        }
    }
}
