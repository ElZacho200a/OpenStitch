// SPDX-License-Identifier: Apache-2.0
// Panel container: uniform margins and title. Children go in the column below the title.
// Surface habillée par le style « panel » du thème (ThemedSurface).
import QtQuick
import QtQuick.Layouts
import "."

Item {
    id: panel
    property string title: ""
    property int padding: Theme.panelPadding
    property color color: Theme.panel
    default property alias content: column.data
    property alias spacing: column.spacing

    implicitWidth: column.implicitWidth + 2 * padding
    implicitHeight: column.implicitHeight + 2 * padding
    Accessible.role: Accessible.Pane
    Accessible.name: title

    ThemedSurface {
        anchors.fill: parent
        themeStyle: Theme.style("panel", "default", "normal")
        fill: panel.color
        radius: Theme.radius
        borderWidth: Theme.borderWidth
        border: Theme.divider
        animated: false
    }

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
