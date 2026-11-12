// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Layouts
import "."

// A titled, collapsible group of settings (points, underlay, pull compensation...). The folded
// state belongs to the parent (so it survives rebuilds of the section list); the header only
// asks for a toggle.
ColumnLayout {
    id: root
    required property var ctl
    property string title: ""
    property var keys: []
    property bool collapsed: false
    signal toggleRequested
    spacing: 9
    visible: keys.length > 0
    readonly property bool modified: {
        const revision = root.ctl.revision
        for (let i = 0; i < root.keys.length; ++i)
            if (root.ctl.field(root.keys[i]).isDefault === false)
                return true
        return false
    }

    Item {
        id: header
        objectName: "paramSectionHeader"
        Layout.fillWidth: true
        implicitHeight: headerRow.implicitHeight + 6
        Accessible.role: Accessible.Button
        Accessible.name: (root.collapsed ? qsTr("Déplier la section ") : qsTr("Replier la section "))
                         + root.title
        Accessible.onPressAction: root.toggleRequested()
        activeFocusOnTab: true
        Keys.onSpacePressed: root.toggleRequested()
        Keys.onReturnPressed: root.toggleRequested()

        RowLayout {
            id: headerRow
            anchors.fill: parent
            spacing: 6
            Text {
                text: root.collapsed ? "▸" : "▾"
                color: root.modified ? Theme.accent : Theme.muted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.captionSize
            }
            SectionLabel {
                Layout.fillWidth: true
                text: root.title.toUpperCase() + (root.modified ? "  •" : "")
                      + (root.collapsed ? "  (" + root.keys.length + ")" : "")
                color: root.modified ? Theme.accent : Theme.muted
                Behavior on color { ColorAnimation { duration: Theme.feedbackFastMs } }
            }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.toggleRequested()
        }
    }
    Repeater {
        model: root.collapsed ? [] : root.keys
        delegate: ParamField {
            required property string modelData
            Layout.fillWidth: true
            ctl: root.ctl
            fieldKey: modelData
        }
    }
}
