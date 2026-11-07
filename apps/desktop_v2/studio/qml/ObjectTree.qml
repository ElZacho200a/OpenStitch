// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."

// Arbre documentaire : calques (du dessus au fond) > groupes > objets, dans l'ordre de couture.
// Pure présentation : toute action appelle l'EditController (`editor`) ou le StudioController.
Item {
    id: root
    objectName: "objectTree"
    property string currentLayer: ""
    readonly property alias count: view.count

    ListView {
        id: view
        objectName: "objectTreeList"
        anchors.fill: parent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: editor.tree
        spacing: 1
        Controls.ScrollBar.vertical: Controls.ScrollBar { }

        delegate: Item {
            id: row
            required property var modelData
            required property int index
            readonly property bool isObject: modelData.kind === "object"
            readonly property bool isLayer: modelData.kind === "layer"
            readonly property bool isGroup: modelData.kind === "group"
            property bool renaming: false
            width: view.width
            height: 30

            Rectangle {
                anchors.fill: parent
                color: row.isObject && row.modelData.selected ? Theme.accentSoft
                       : row.isLayer ? Theme.raised
                       : area.containsMouse ? Theme.hover : "transparent"
                border.width: row.isLayer && root.currentLayer === row.modelData.id ? 1 : 0
                border.color: Theme.accent
                Rectangle {
                    visible: drop.containsDrag
                    anchors.fill: parent
                    color: "transparent"
                    border.width: 2
                    border.color: Theme.accent
                }
            }

            DropArea {
                id: drop
                anchors.fill: parent
                keys: row.isLayer ? ["object"] : []
                onDropped: editor.moveSelectionToLayer(row.modelData.id)
            }

            MouseArea {
                id: area
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton
                drag.target: row.isObject ? dragGhost : null
                onPressed: {
                    if (row.isObject && !row.modelData.selected)
                        studio.selectObject(row.modelData.id, false)
                }
                onClicked: function(mouse) {
                    if (row.isObject) {
                        studio.selectObject(row.modelData.id,
                                            Boolean(mouse.modifiers & Qt.ControlModifier))
                    } else if (row.isGroup) {
                        editor.selectGroup(row.modelData.id)
                    } else if (row.isLayer) {
                        root.currentLayer = row.modelData.id
                        editor.selectByLayer(row.modelData.id)
                    }
                }
                onDoubleClicked: if (row.isObject || row.isLayer) { row.renaming = true; nameEdit.forceActiveFocus() }
                onReleased: dragGhost.Drag.drop()
            }

            // Fantôme de glisser-déposer : suit la souris, retourne à sa place au relâchement.
            Rectangle {
                id: dragGhost
                width: 120
                height: 22
                radius: 3
                visible: Drag.active
                color: Theme.accentSoft
                border.color: Theme.accent
                z: 10
                Drag.active: area.drag.active
                Drag.keys: ["object"]
                Drag.hotSpot.x: 10
                Drag.hotSpot.y: 10
                onVisibleChanged: if (!visible) { x = 0; y = 0 }
                Text {
                    anchors.centerIn: parent
                    text: row.modelData.label
                    color: Theme.foreground
                    font.pixelSize: Theme.smallSize
                    elide: Text.ElideRight
                    width: parent.width - 8
                }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8 + row.modelData.depth * 14
                anchors.rightMargin: 6
                spacing: 6
                Text {
                    visible: !row.isObject
                    text: row.isLayer ? "▾" : "▸"
                    color: Theme.faint
                    font.pixelSize: Theme.bodySize
                }
                Rectangle {
                    visible: row.isObject
                    implicitWidth: 11
                    implicitHeight: 11
                    radius: 3
                    color: row.isObject ? row.modelData.color : "transparent"
                    border.width: 1
                    border.color: "#66858d9e"
                    opacity: row.isObject && row.modelData.visible ? 1 : 0.4
                }
                Text {
                    Layout.fillWidth: true
                    visible: !row.renaming
                    text: row.modelData.label + (row.isObject && row.modelData.locked ? "  🔒" : "")
                    color: row.isObject && row.modelData.selected ? Theme.foreground : Theme.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.bodySize
                    font.weight: row.isLayer ? Font.DemiBold : Font.Normal
                    elide: Text.ElideRight
                }
                TextInput {
                    id: nameEdit
                    objectName: "treeRenameField"
                    Layout.fillWidth: true
                    visible: row.renaming
                    text: row.modelData.label
                    color: Theme.foreground
                    font.pixelSize: Theme.bodySize
                    selectByMouse: true
                    onAccepted: {
                        if (row.isLayer)
                            editor.renameLayer(row.modelData.id, text)
                        else
                            editor.renameObject(row.modelData.id, text)
                        row.renaming = false
                    }
                    Keys.onEscapePressed: row.renaming = false
                    onActiveFocusChanged: if (!activeFocus) row.renaming = false
                }
                Text {
                    visible: row.isObject || row.isLayer
                    text: row.modelData.visible ? "👁" : "—"
                    color: row.modelData.visible ? Theme.muted : Theme.faint
                    font.pixelSize: Theme.bodySize
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -4
                        onClicked: row.isLayer
                                   ? editor.setLayerVisible(row.modelData.id, !row.modelData.visible)
                                   : editor.setObjectVisible(row.modelData.id, !row.modelData.visible)
                    }
                }
                Text {
                    visible: row.isLayer || row.isObject
                    text: row.modelData.locked ? "🔒" : "🔓"
                    color: row.modelData.locked ? Theme.accent : Theme.faint
                    font.pixelSize: Theme.bodySize
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -4
                        onClicked: row.isLayer
                                   ? editor.setLayerLocked(row.modelData.id, !row.modelData.locked)
                                   : editor.setObjectLocked(row.modelData.id, !row.modelData.locked)
                    }
                }
            }
        }
    }
}
