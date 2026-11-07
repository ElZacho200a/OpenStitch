// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."

// Appliqué : conversion, réglages (inspecteur), export du contour de découpe.
Rectangle {
    id: panel
    objectName: "appliquePanel"
    property var controller
    readonly property bool ready: controller !== undefined && controller !== null
    color: Theme.panel
    border.color: Theme.divider
    radius: Theme.radius

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8
        SectionLabel { text: qsTr("APPLIQUÉ") }
        StudioButton { objectName: "appliqueConvert"; text: qsTr("Convertir en appliqué"); enabled: panel.ready && panel.controller.canConvert; onClicked: panel.controller.convert() }
        StudioButton { objectName: "appliqueExport"; text: qsTr("Exporter le contour de découpe…"); enabled: panel.ready && panel.controller.appliqueCount > 0; onClicked: panel.controller.exportCut(exportPath.text) }
        StudioTextField { id: exportPath; objectName: "appliqueExportPath"; Layout.fillWidth: true; placeholderText: qsTr("découpe.svg ou .dxf") }
        Text {
            objectName: "appliqueMessage"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: panel.ready && !panel.controller.lastOk ? Theme.error : Theme.faint
            font.family: Theme.fontFamily
            text: panel.ready ? panel.controller.message : ""
        }
        Item { Layout.fillHeight: true }
    }
}
