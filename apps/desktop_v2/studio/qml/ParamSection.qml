// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Layouts
import "."

// A titled group of settings (points, underlay, pull compensation...).
ColumnLayout {
    id: root
    required property var ctl
    property string title: ""
    property var keys: []
    spacing: 9
    visible: keys.length > 0
    readonly property bool modified: {
        const revision = root.ctl.revision
        for (let i = 0; i < root.keys.length; ++i)
            if (root.ctl.field(root.keys[i]).isDefault === false)
                return true
        return false
    }

    SectionLabel {
        text: root.title.toUpperCase() + (root.modified ? "  •" : "")
        color: root.modified ? Theme.accent : Theme.muted
        Behavior on color { ColorAnimation { duration: Theme.feedbackFastMs } }
    }
    Repeater {
        model: root.keys
        delegate: ParamField {
            required property string modelData
            Layout.fillWidth: true
            ctl: root.ctl
            fieldKey: modelData
        }
    }
}
