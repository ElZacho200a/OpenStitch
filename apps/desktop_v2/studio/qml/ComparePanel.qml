// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."
Item {
    id: hub
    objectName: "compareHub"
    property bool expanded: false
    Loader {
        anchors.fill: parent
        active: typeof compare !== "undefined"
        sourceComponent: Item {
            StudioButton {
                objectName: "compareToggle"
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 16
                text: qsTr("Calques et comparaison")
                onClicked: hub.expanded = !hub.expanded
            }
            Rectangle {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: 16
                anchors.bottomMargin: 58
                width: 280
                height: content.implicitHeight + 24
                color: Theme.panel
                border.color: Theme.border
                radius: 8
                visible: hub.expanded
                enabled: visible
                ColumnLayout {
                    id: content
                    anchors.fill: parent
                    anchors.margins: 12
                    Controls.CheckBox {
                        objectName: "compareSource"
                        text: qsTr("Image source")
                        Accessible.name: text
                        enabled: compare.hasSource
                        checked: compare.sourceVisible
                        onToggled: compare.sourceVisible = checked
                    }
                    Controls.CheckBox {
                        objectName: "compareSegmentation"
                        text: qsTr("Segmentation et aperçu")
                        Accessible.name: text
                        enabled: compare.hasSegmentation
                        checked: compare.segmentationVisible
                        onToggled: compare.segmentationVisible = checked
                    }
                    Controls.CheckBox {
                        objectName: "compareVectors"
                        text: qsTr("Contours vectoriels")
                        Accessible.name: text
                        checked: compare.vectorsVisible
                        onToggled: compare.vectorsVisible = checked
                    }
                    Controls.CheckBox {
                        objectName: "compareOrder"
                        text: qsTr("Ordre de couture")
                        Accessible.name: text
                        checked: compare.orderVisible
                        onToggled: compare.orderVisible = checked
                    }
                    Controls.Label { text: qsTr("Opacité des calques"); color: Theme.faint }
                    Controls.Slider {
                        objectName: "compareOpacity"
                        Layout.fillWidth: true
                        Accessible.name: qsTr("Opacité des calques")
                        from: 0; to: 1; value: compare.opacity
                        onMoved: compare.opacity = value
                    }
                    Controls.CheckBox {
                        objectName: "compareCurtain"
                        text: qsTr("Comparer par rideau")
                        Accessible.name: text
                        enabled: compare.hasSource
                        checked: compare.curtain
                        onToggled: { compare.curtain = checked; if (checked) compare.sourceVisible = true }
                    }
                    Controls.Slider {
                        objectName: "comparePosition"
                        Layout.fillWidth: true
                        Accessible.name: qsTr("Position du rideau")
                        enabled: compare.curtain
                        from: 0; to: 1; value: compare.position
                        onMoved: compare.position = value
                    }
                }
            }
        }
    }
}
