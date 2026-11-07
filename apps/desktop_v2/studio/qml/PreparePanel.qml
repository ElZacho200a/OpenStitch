// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."

// Espace « Préparer » : image, segmentation, régions, IA. Inerte (ne charge rien) quand l'hôte
// ne fournit pas ses adaptateurs, comme les autres panneaux optionnels.
Item {
    id: hub
    objectName: "preparePanel"
    property bool opened: false
    property int tab: 0
    readonly property bool ready: loader.status === Loader.Ready

    function toggle() { opened = !opened }

    Shortcut { sequence: "Ctrl+Shift+P"; onActivated: hub.toggle() }

    StudioButton {
        objectName: "prepareToggle"
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 8
        anchors.topMargin: 56
        text: qsTr("Préparer")
        checkable: true
        checked: hub.opened
        visible: loader.active
        onClicked: hub.toggle()
    }

    Loader {
        id: loader
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: 100
        width: 340
        active: typeof imageTools !== "undefined" && typeof regionTools !== "undefined"
        visible: hub.opened
        sourceComponent: Rectangle {
            color: Theme.panel
            border.color: Theme.border

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6
                Row {
                    spacing: 4
                    Repeater {
                        model: [qsTr("Image"), qsTr("Segmentation"), qsTr("Régions"), qsTr("IA")]
                        delegate: StudioButton {
                            required property string modelData
                            required property int index
                            objectName: "prepareTab_" + index
                            text: modelData
                            checkable: true
                            checked: hub.tab === index
                            onClicked: hub.tab = index
                        }
                    }
                }
                Controls.ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    clip: true
                    Item {
                        width: parent.width
                        implicitHeight: pages.implicitHeight
                        StackLayout {
                            id: pages
                            width: parent.width - 4
                            currentIndex: hub.tab
                            ImagePrepSection { Layout.fillWidth: true; Layout.alignment: Qt.AlignTop }
                            SegmentationSection { Layout.fillWidth: true; Layout.alignment: Qt.AlignTop }
                            RegionTools { Layout.fillWidth: true; Layout.alignment: Qt.AlignTop }
                            AiSection { Layout.fillWidth: true; Layout.alignment: Qt.AlignTop }
                        }
                    }
                }
            }
        }
    }
}
