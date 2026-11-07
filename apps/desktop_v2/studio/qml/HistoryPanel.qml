// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."

Item {
    id: hub
    objectName: "historyHub"
    property bool opened: false
    Loader {
        anchors.fill: parent
        active: typeof history !== "undefined" && history !== null
        sourceComponent: Item {
            StudioButton {
                id: toggle
                objectName: "historyToggle"
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.leftMargin: 44
                anchors.topMargin: 56
                text: qsTr("Historique")
                active: hub.opened
                tooltip: qsTr("Afficher les étapes du document")
                onClicked: {
                    hub.opened = !hub.opened
                    if (!hub.opened) forceActiveFocus()
                }
            }
            Rectangle {
                visible: hub.opened
                anchors.top: toggle.bottom
                anchors.left: toggle.left
                anchors.topMargin: 6
                width: Math.min(310, parent.width - 36)
                height: Math.min(360, parent.height - 90)
                color: Theme.panel
                border.color: Theme.border
                radius: Theme.radius
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6
                    Text {
                        text: history.dirty ? qsTr("Modifications non enregistrées") : qsTr("Enregistré")
                        color: history.dirty ? Theme.muted : Theme.success
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.bodySize
                    }
                    Controls.ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        ListView {
                            id: steps
                            objectName: "historySteps"
                            model: history.entries
                            currentIndex: history.currentIndex
                            spacing: 3
                            onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
                            delegate: Controls.ItemDelegate {
                                required property var modelData
                                width: steps.width
                                objectName: "historyStep_" + modelData.index
                                text: (modelData.current ? "▶ " : "") + modelData.label
                                      + (modelData.saved ? qsTr(" · enregistré ici") : "")
                                highlighted: modelData.current
                                opacity: modelData.future ? 0.65 : 1
                                Accessible.name: text
                                focusPolicy: Qt.StrongFocus
                                onClicked: history.jumpTo(modelData.index)
                                background: Rectangle {
                                    color: parent.highlighted ? Theme.accentSoft : parent.hovered ? Theme.raised : "transparent"
                                    border.width: parent.activeFocus ? 1 : 0
                                    border.color: Theme.accent
                                    radius: Theme.radius
                                }
                                contentItem: Text {
                                    text: parent.text
                                    color: Theme.foreground
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.bodySize
                                    elide: Text.ElideRight
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
