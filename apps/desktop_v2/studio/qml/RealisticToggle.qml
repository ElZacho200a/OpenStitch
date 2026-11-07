// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."

// Bascule du rendu réaliste + accès aux préréglages de qualité (AnalysisController). Vue seule :
// basculer ne recalcule rien si la révision du motif est inchangée.
ColumnLayout {
    id: root
    spacing: 6
    readonly property bool ready: typeof view !== "undefined" && typeof analysis !== "undefined"

    Controls.CheckBox {
        objectName: "realisticCheck"
        enabled: root.ready
        text: qsTr("Rendu réaliste") + "  (Ctrl+Shift+T)"
        checked: root.ready && view.realistic
        onToggled: view.realistic = checked
    }
    Text {
        objectName: "realisticModeLabel"
        visible: root.ready && view.realistic
        text: root.ready ? view.realisticMode : ""
        color: Theme.muted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.smallSize
    }
    Text {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: qsTr("Qualité du rendu")
        color: Theme.faint
        font.family: Theme.fontFamily
        font.pixelSize: Theme.smallSize
    }
    Flow {
        Layout.fillWidth: true
        spacing: 4
        Repeater {
            model: root.ready ? analysis.renderPresets : []
            delegate: StudioButton {
                required property var modelData
                text: modelData.label
                active: analysis.renderSettings.preset === modelData.id
                onClicked: analysis.applyRenderPreset(modelData.id)
            }
        }
    }
}
