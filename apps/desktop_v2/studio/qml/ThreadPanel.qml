// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "."

// Thread plan, sewing order, charts and colourways. Adapter over ThreadController: nothing is
// computed here and every change is one undo step on the shared document.
Rectangle {
    id: panel
    objectName: "threadPanel"
    property var controller
    property int current: -1
    property string chartId: controller && controller.charts.length > 0 ? controller.charts[0].id : ""
    readonly property var entryList: controller ? controller.entries : []
    color: Theme.panel
    border.color: Theme.divider
    radius: Theme.radius

    FileDialog {
        id: importDialog
        title: qsTr("Importer un nuancier (CSV ou JSON)")
        nameFilters: [qsTr("Nuanciers (*.csv *.json)")]
        onAccepted: panel.controller.importChart(selectedFile)
    }
    FileDialog {
        id: exportDialog
        title: qsTr("Exporter la liste des fils")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("CSV (*.csv)")]
        defaultSuffix: "csv"
        onAccepted: panel.controller.exportThreadList(selectedFile)
    }

    Flickable {
        anchors.fill: parent
        anchors.margins: 10
        contentHeight: content.implicitHeight
        clip: true
        Controls.ScrollBar.vertical: Controls.ScrollBar {}

        ColumnLayout {
            id: content
            width: parent.width - 12
            spacing: 8

            SectionLabel { text: qsTr("PLAN DE FILS") + "  ·  " + panel.entryList.length }
            Repeater {
                model: panel.entryList
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    objectName: "threadRow" + modelData.index
                    Layout.fillWidth: true
                    implicitHeight: 32
                    radius: Theme.radius
                    color: panel.current === modelData.index ? Theme.accentSoft : (modelData.selected ? Theme.raised : "transparent")
                    opacity: modelData.visible ? 1 : 0.5
                    MouseArea { anchors.fill: parent; onClicked: panel.current = row.modelData.index }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 6
                        spacing: 8
                        Rectangle { implicitWidth: 18; implicitHeight: 18; radius: 3; color: row.modelData.color; border.color: Theme.border }
                        Text { Layout.fillWidth: true; elide: Text.ElideRight; text: row.modelData.label; color: Theme.foreground; font.family: Theme.fontFamily; font.pixelSize: Theme.bodySize }
                        Text { text: qsTr("%1 obj.").arg(row.modelData.objectCount); color: Theme.faint; font.family: Theme.fontFamily; font.pixelSize: Theme.smallSize }
                    }
                }
            }
            Flow {
                Layout.fillWidth: true
                spacing: 4
                enabled: panel.current >= 0
                StudioButton { objectName: "threadSelectButton"; text: qsTr("Sélectionner les objets"); onClicked: panel.controller.selectThreadObjects(panel.current) }
                StudioButton {
                    objectName: "threadToggleButton"
                    readonly property bool shown: panel.current >= 0 && panel.current < panel.entryList.length ? panel.entryList[panel.current].visible : true
                    text: shown ? qsTr("Masquer ce fil") : qsTr("Afficher ce fil")
                    onClicked: panel.controller.setThreadVisible(panel.current, !shown)
                }
                StudioComboBox {
                    id: mergeBox
                    objectName: "threadMergeBox"
                    implicitWidth: 170
                    displayText: qsTr("Fusionner dans…")
                    model: panel.entryList
                    textRole: "label"
                    onActivated: {
                        panel.controller.mergeThreads(panel.current, currentIndex)
                        panel.current = -1
                    }
                }
                StudioTextField { id: recolorField; objectName: "threadRecolorField"; implicitWidth: 90; placeholderText: "#rrggbb" }
                StudioButton { objectName: "threadRecolorButton"; text: qsTr("Recolorer"); onClicked: panel.controller.recolorThread(panel.current, recolorField.text) }
            }
            StudioButton { text: qsTr("Exporter la liste (CSV)"); onClicked: exportDialog.open() }

            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.divider }
            SectionLabel { text: qsTr("NUANCIERS") }
            RowLayout {
                Layout.fillWidth: true
                StudioComboBox {
                    id: chartBox
                    objectName: "chartBox"
                    Layout.fillWidth: true
                    model: panel.controller ? panel.controller.charts : []
                    textRole: "name"
                    onActivated: panel.chartId = model[currentIndex].id
                }
                StudioButton { text: qsTr("Importer"); onClicked: importDialog.open() }
                StudioButton {
                    objectName: "chartRemoveButton"
                    text: qsTr("Retirer")
                    enabled: chartBox.currentIndex >= 0 && panel.controller.charts[chartBox.currentIndex].user
                    onClicked: panel.controller.removeChart(panel.chartId)
                }
            }
            RowLayout {
                Layout.fillWidth: true
                StudioTextField {
                    id: searchField
                    objectName: "chartSearchField"
                    Layout.fillWidth: true
                    placeholderText: qsTr("Nom ou référence")
                }
                StudioButton { objectName: "nearestButton"; text: qsTr("Plus proches"); enabled: panel.current >= 0; onClicked: resultList.model = panel.controller.nearestToEntry(panel.current, panel.chartId, 8) }
                StudioButton { objectName: "searchButton"; text: qsTr("Chercher"); onClicked: resultList.model = panel.controller.searchThreads(panel.chartId, searchField.text) }
            }
            StudioButton { text: qsTr("Couleurs libres → fil le plus proche"); onClicked: panel.controller.snapFreeColours(panel.chartId) }
            ListView {
                id: resultList
                objectName: "chartResults"
                Layout.fillWidth: true
                implicitHeight: Math.min(180, contentHeight)
                clip: true
                model: []
                delegate: Rectangle {
                    id: result
                    required property var modelData
                    width: ListView.view.width
                    height: 28
                    color: "transparent"
                    RowLayout {
                        anchors.fill: parent
                        spacing: 8
                        Rectangle { implicitWidth: 16; implicitHeight: 16; radius: 3; color: result.modelData.color; border.color: Theme.border }
                        Text { Layout.fillWidth: true; elide: Text.ElideRight; text: result.modelData.code + "  " + result.modelData.name; color: Theme.foreground; font.family: Theme.fontFamily; font.pixelSize: Theme.smallSize }
                        StudioButton {
                            text: qsTr("Assigner")
                            enabled: panel.current >= 0
                            quiet: true
                            onClicked: panel.controller.assignChartThread(panel.current, result.modelData.chartId, result.modelData.code)
                        }
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.divider }
            SectionLabel { text: qsTr("COLORIS") }
            Repeater {
                model: panel.controller ? panel.controller.colorways : []
                delegate: RowLayout {
                    id: way
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    Text { Layout.fillWidth: true; text: (way.modelData.active ? "● " : "○ ") + way.modelData.name; color: Theme.foreground; font.family: Theme.fontFamily; font.pixelSize: Theme.bodySize }
                    StudioButton { objectName: "colorwayActivate" + way.index; text: qsTr("Activer"); quiet: true; enabled: !way.modelData.active; onClicked: panel.controller.activateColorway(way.index) }
                    StudioButton { objectName: "colorwayRemove" + way.index; text: qsTr("Supprimer"); quiet: true; onClicked: panel.controller.removeColorway(way.index) }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                StudioTextField { id: colorwayName; objectName: "colorwayNameField"; Layout.fillWidth: true; placeholderText: qsTr("Nom du nouveau coloris") }
                StudioButton { objectName: "colorwayAddButton"; text: qsTr("Créer"); onClicked: { if (panel.controller.addColorway(colorwayName.text)) colorwayName.text = "" } }
            }

            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.divider }
            SectionLabel { text: qsTr("ORDRE DE COUTURE") }
            Repeater {
                model: panel.controller ? panel.controller.sewingOrder : []
                delegate: RowLayout {
                    id: ord
                    required property var modelData
                    Layout.fillWidth: true
                    Rectangle { implicitWidth: 14; implicitHeight: 14; radius: 3; color: ord.modelData.color; border.color: Theme.border }
                    Text { Layout.fillWidth: true; elide: Text.ElideRight; text: (ord.modelData.position + 1) + ". " + ord.modelData.name; color: Theme.foreground; font.family: Theme.fontFamily; font.pixelSize: Theme.smallSize }
                    StudioButton { objectName: "orderUp" + ord.modelData.position; text: "▲"; quiet: true; enabled: ord.modelData.position > 0; onClicked: panel.controller.moveInOrder(ord.modelData.id, ord.modelData.position - 1) }
                    StudioButton { objectName: "orderDown" + ord.modelData.position; text: "▼"; quiet: true; onClicked: panel.controller.moveInOrder(ord.modelData.id, ord.modelData.position + 1) }
                }
            }
            Text {
                objectName: "threadMessage"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: panel.controller ? panel.controller.message : ""
                color: panel.controller && !panel.controller.lastOk ? Theme.error : Theme.success
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
            }
        }
    }
}
