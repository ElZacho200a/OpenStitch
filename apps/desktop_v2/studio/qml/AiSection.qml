// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Layouts
import "."

// Segmentation IA : état des backends déclarés. L'analyse elle-même n'est pas encore migrée ;
// la segmentation classique par couleurs reste toujours disponible (repli explicite).
ColumnLayout {
    id: section
    objectName: "aiSection"
    spacing: 8

    SectionLabel { text: qsTr("BACKENDS IA") }
    Text {
        objectName: "aiSummary"
        Layout.fillWidth: true
        text: imageTools.aiSummary.length > 0 ? imageTools.aiSummary
              : qsTr("État inconnu : sondez les backends. La segmentation classique par couleurs reste disponible.")
        wrapMode: Text.WordWrap
        color: Theme.muted; font.pixelSize: Theme.smallSize; font.family: Theme.fontFamily
    }
    Repeater {
        model: imageTools.aiBackends
        delegate: Rectangle {
            required property var modelData
            Layout.fillWidth: true
            implicitHeight: col.implicitHeight + 12
            color: Theme.surface
            border.color: modelData.available ? Theme.success : Theme.border
            Column {
                id: col
                anchors.fill: parent
                anchors.margins: 6
                Text { text: modelData.name + " — " + modelData.state; color: Theme.foreground
                    font.pixelSize: Theme.bodySize; font.family: Theme.fontFamily }
                Text { width: parent.width; text: modelData.diagnostics; wrapMode: Text.WordWrap
                    color: Theme.muted; font.pixelSize: Theme.smallSize; font.family: Theme.fontFamily }
            }
        }
    }
    StudioButton {
        objectName: "aiProbeButton"
        text: imageTools.aiBusy ? qsTr("Sondage en cours…") : qsTr("Sonder les backends")
        enabled: !imageTools.aiBusy
        onClicked: imageTools.refreshAiBackends()
    }
}
