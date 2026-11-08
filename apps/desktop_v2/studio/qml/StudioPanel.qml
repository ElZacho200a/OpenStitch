// SPDX-License-Identifier: Apache-2.0
// Panel container: uniform margins and title. Children go in the column below the title.
import QtQuick
import QtQuick.Layouts
import "."

Rectangle {
    id: panel
    property string title: ""
    property int padding: Theme.panelPadding
    default property alias content: column.data
    property alias spacing: column.spacing

    color: Theme.panel
    radius: Theme.radius
    border.width: 1
    border.color: Theme.divider
    implicitWidth: column.implicitWidth + 2 * padding
    implicitHeight: column.implicitHeight + 2 * padding
    Accessible.role: Accessible.Pane
    Accessible.name: title

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: panel.padding
        spacing: 8
        StudioLabel {
            visible: panel.title.length > 0
            text: panel.title
            role: "heading"
            Layout.fillWidth: true
        }
    }
}
