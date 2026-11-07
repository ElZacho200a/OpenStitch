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

    SectionLabel { text: root.title.toUpperCase() }
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
