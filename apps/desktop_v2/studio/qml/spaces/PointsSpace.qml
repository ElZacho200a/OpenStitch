// SPDX-License-Identifier: Apache-2.0
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import OpenStitch.Studio 1.0
import ".."

// Espace « Points » : le contenu central historique du Studio (canevas des points, rail d'outils,
// inspecteur, panneaux Satin / Fondu / Points, Vue / Fils / Analyse / Simulation), extrait tel
// quel de Main.qml. Contrat des espaces : docs/desktop-v2/23-espaces-studio.md.
Item {
    id: root
    objectName: "pointsSpace"
    required property var workspace
    property bool active: false
    readonly property var canvas: spaceCanvas
    readonly property var editorController: editor
    readonly property var digitizeController: typeof digitizer !== "undefined" ? digitizer : null
    readonly property var toolController: typeof tools !== "undefined" ? tools : null
    readonly property int selectedCount: {
        var count = 0
        const entries = studio.objects
        for (var i = 0; i < entries.length; ++i)
            if (entries[i].selected)
                ++count
        return count
    }

    // L'état des points se déduit du document (objets et formes) : aucune publication propre.
    Component.onCompleted: root.workspace.clearStageStatus("points")

    RowLayout {
        anchors.fill: parent
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
                    active: !spaceCanvas.panMode
                    onClicked: { spaceCanvas.panMode = false; if (root.toolController) root.toolController.activeTool = "select" }
                }
                StudioButton {
                    objectName: "panToolButton"
                    iconName: "pan"
                    tooltip: studio.panTool.label
                    shortcutLabel: studio.panTool.shortcut
                    quiet: true
                    active: spaceCanvas.panMode
                    onClicked: { spaceCanvas.panMode = true; if (root.toolController) root.toolController.activeTool = "select" }
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
                    visible: root.toolController !== null
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
                    onClicked: spaceCanvas.fitDesign()
                }
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

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 43
                color: Theme.background
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 20
                    anchors.rightMargin: 20
                    SectionLabel { text: qsTr("STRUCTURE DES POINTS") }
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

                ToolOverlay {
                    objectName: "toolOverlay"
                    anchors.fill: parent
                    gpu: spaceCanvas
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

                DigitizeHub {
                    anchors.fill: parent
                }

                ViewPanel {
                    objectName: "viewHub"
                    anchors.fill: parent
                    canvasItem: spaceCanvas
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

                Text {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.rightMargin: 20
                    anchors.bottomMargin: 29
                    visible: viewport.width >= 670
                    text: spaceCanvas.panMode ? qsTr("Glisser pour déplacer · Molette pour zoomer")
                                         : qsTr("Clic pour sélectionner · Molette pour zoomer")
                    color: Theme.faint
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallSize
                }
            }
        }

        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: Theme.divider }

        Rectangle {
            Layout.preferredWidth: root.width < 1180 ? 250 : Theme.inspectorWidth
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
                        anchors.leftMargin: Theme.panelPadding
                        anchors.rightMargin: Theme.panelPadding
                        Text {
                            text: qsTr("Objets")
                            color: Theme.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.headingSize
                            font.weight: Font.DemiBold
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: studio.objects.length
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
                        active: !root.workspace.treeMode
                        onClicked: root.workspace.treeMode = false
                    }
                    StudioButton {
                        objectName: "treeModeButton"
                        text: qsTr("Arbre et calques")
                        implicitHeight: 26
                        active: root.workspace.treeMode
                        onClicked: root.workspace.treeMode = true
                    }
                    StudioButton {
                        objectName: "editOpenButton"
                        text: qsTr("Édition")
                        implicitHeight: 26
                        active: root.workspace.editOpen
                        tooltip: qsTr("Afficher les actions d'édition de la sélection")
                        onClicked: root.workspace.editOpen = !root.workspace.editOpen
                    }
                }

                LayerPanel {
                    objectName: "layerPanel"
                    visible: root.workspace.treeMode
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 100
                }

                ListView {
                    id: objectList
                    objectName: "objectList"
                    visible: !root.workspace.treeMode
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 100
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: studio.objects
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
                                border.color: Qt.alpha(Theme.faint, 0.4)
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
                                    font.pixelSize: Theme.captionSize
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
                        StudioToolTip { delay: 700; text: modelData.label + "\n" + modelData.id }
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
                    Layout.margins: Theme.panelPadding
                    spacing: 11
                    RowLayout {
                        Layout.fillWidth: true
                        SectionLabel { text: qsTr("SÉLECTION") }
                        Item { Layout.fillWidth: true }
                        StudioButton {
                            visible: root.selectedCount > 0
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
                        text: root.selectedCount > 0 ? studio.selectionInfo
                                                       : qsTr("Cliquez sur un fil ou choisissez un objet dans la liste.")
                        color: root.selectedCount > 0 ? Theme.foreground : Theme.faint
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.bodySize
                        wrapMode: Text.Wrap
                        maximumLineCount: root.height < 760 ? 8 : 12
                        elide: Text.ElideRight
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

                Inspector {
                    Layout.fillWidth: true
                    Layout.preferredHeight: active ? Math.min(implicitHeight + 24, root.height * 0.5) : 0
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.divider }

                EditBar {
                    visible: root.workspace.editOpen
                    Layout.fillWidth: true
                    Layout.margins: 12
                }

                ToolOptions {
                    visible: root.toolController !== null && root.workspace.editOpen
                    Layout.fillWidth: true
                    Layout.margins: 12
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.divider }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: Theme.panelPadding
                    spacing: 9
                    SectionLabel { text: qsTr("POINTS") }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Observer la broderie, fil par fil")
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.bodySize
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: root.height >= 760
                        text: qsTr("Les segments proviennent du moteur OpenStitch. Sélectionnez un objet dans la vue ou dans la liste.")
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
}
