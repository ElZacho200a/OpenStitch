// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."

// Arbre documentaire : calques (du dessus au fond) > groupes > objets, dans l'ordre de couture.
// Pure présentation : toute action appelle l'EditController (`editor`) ou le StudioController.
// Glisser-déposer : glisser une ligne d'objet sur une ligne de calque déplace la sélection.
Item {
    id: root
    objectName: "objectTree"
    property string currentLayer: ""
    property bool dragActive: false
    property string dragLabel: ""
    property point dragPos: Qt.point(0, 0)

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
                Behavior on color { ColorAnimation { duration: Theme.animationMs } }
                Rectangle {
                    width: 3
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    color: Theme.accent
                    visible: row.isObject && row.modelData.selected
                }
            }

            MouseArea {
                id: area
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: row.isObject && row.modelData.locked ? Qt.ForbiddenCursor
                             : root.dragActive ? Qt.ClosedHandCursor : Qt.PointingHandCursor
                acceptedButtons: Qt.LeftButton
                preventStealing: true // le défilement de la liste ne doit pas voler le glisser
                onCanceled: root.dragActive = false
                property point pressPoint: Qt.point(0, 0)
                onPressed: function(mouse) {
                    pressPoint = Qt.point(mouse.x, mouse.y)
                }
                onPositionChanged: function(mouse) {
                    if (!pressed || !row.isObject)
                        return
                    if (Math.abs(mouse.x - pressPoint.x) + Math.abs(mouse.y - pressPoint.y) > 8) {
                        root.dragActive = true
                        root.dragLabel = row.modelData.label
                        root.dragPos = mapToItem(root, mouse.x, mouse.y)
                    }
                }
                onReleased: function(mouse) {
                    if (!root.dragActive)
                        return
                    root.dragActive = false
                    const p = mapToItem(view.contentItem, mouse.x, mouse.y)
                    const target = view.itemAt(p.x, p.y)
                    if (target && target.isLayer)
                        editor.moveObjectToLayer(row.modelData.id, target.modelData.id)
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
                onDoubleClicked: {
                    if (row.isObject || row.isLayer) {
                        row.renaming = true
                        nameEdit.forceActiveFocus()
                    }
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
                    border.color: Qt.alpha(Theme.faint, 0.4)
                    opacity: row.isObject && row.modelData.visible ? 1 : 0.4
                    Behavior on opacity { NumberAnimation { duration: Theme.feedbackFastMs } }
                }
                Text {
                    Layout.fillWidth: true
                    visible: !row.renaming
                    text: row.modelData.label + (row.isObject && row.modelData.locked ? "  (verrouillé)" : "")
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
                    text: row.modelData.visible ? "Voir" : "Caché"
                    color: row.modelData.visible ? Theme.muted : Theme.faint
                    font.pixelSize: Theme.smallSize
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -4
                        onClicked: {
                            if (row.isLayer)
                                editor.setLayerVisible(row.modelData.id, !row.modelData.visible)
                            else
                                editor.setObjectVisible(row.modelData.id, !row.modelData.visible)
                        }
                    }
                }
                Text {
                    visible: row.isObject || row.isLayer
                    text: row.modelData.locked ? "Verr." : "Libre"
                    color: row.modelData.locked ? Theme.accent : Theme.faint
                    font.pixelSize: Theme.smallSize
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -4
                        onClicked: {
                            if (row.isLayer)
                                editor.setLayerLocked(row.modelData.id, !row.modelData.locked)
                            else
                                editor.setObjectLocked(row.modelData.id, !row.modelData.locked)
                        }
                    }
                }
            }
        }
    }

    // Étiquette qui suit la souris pendant un glisser-déposer.
    Rectangle {
        visible: root.dragActive
        x: root.dragPos.x + 8
        y: root.dragPos.y + 8
        width: dragText.implicitWidth + 12
        height: 22
        radius: 3
        color: Theme.accentSoft
        border.color: Theme.accent
        Text {
            id: dragText
            anchors.centerIn: parent
            text: root.dragLabel
            color: Theme.foreground
            font.pixelSize: Theme.smallSize
        }
    }
}
