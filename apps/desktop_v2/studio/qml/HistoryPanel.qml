// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."

Item {
    id: hub
    objectName: "historyHub"
    property bool opened: false
    readonly property bool available: typeof history !== "undefined" && history !== null
    Loader {
        anchors.fill: parent
        active: typeof history !== "undefined" && history !== null
        sourceComponent: Item {
            Rectangle {
                visible: hub.opened
                opacity: hub.opened ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.feedbackFastMs } }
                // Le bouton « Historique » vit dans la barre du haut (Main.qml) : ce volet
                // s'ancre au canevas sans marges magiques qui chevauchaient les rails d'outils.
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.topMargin: 8
                anchors.leftMargin: 8
                width: Math.min(310, parent.width - 36)
                height: Math.min(360, parent.height - 24)
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
                                    color: parent.highlighted ? Theme.accentSoft : parent.hovered ? Theme.hover : "transparent"
                                    Behavior on color { ColorAnimation { duration: Theme.animationMs } }
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
