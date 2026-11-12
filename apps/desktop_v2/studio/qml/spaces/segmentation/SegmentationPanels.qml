// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "../.."

// Panneaux de l'espace « Segmentation » : les sections existantes (segmentation couleur, édition
// des régions, IA) instanciées directement, sans PreparePanel. Aucune logique ici : tout passe
// par imageTools / regionTools (RegionController, une étape d'annulation par geste).
Rectangle {
    id: panel
    objectName: "segmentationPanels"
    property int tab: 0
    // Réglages courants de la segmentation (utilisés par l'action « Segmenter l'image »).
    readonly property var segmentationOptions: segSection.options
    readonly property var tabLabels: [qsTr("Segmentation"), qsTr("Régions"), qsTr("IA")]
    color: Theme.panel
    border.color: Theme.border

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6
        Row {
            spacing: 4
            Repeater {
                model: panel.tabLabels
                delegate: StudioButton {
                    required property string modelData
                    required property int index
                    objectName: "segmentationTab_" + index
                    text: modelData
                    checkable: true
                    checked: panel.tab === index
                    onClicked: panel.tab = index
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
                    currentIndex: panel.tab
                    SegmentationSection { id: segSection; Layout.fillWidth: true; Layout.alignment: Qt.AlignTop }
                    RegionTools {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        // Re-segmenting must use the same live values as Preview/Apply.
                        segmentationOptions: panel.segmentationOptions
                    }
                    AiSection { Layout.fillWidth: true; Layout.alignment: Qt.AlignTop }
                }
            }
        }
    }
}
