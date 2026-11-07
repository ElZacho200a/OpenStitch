// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."

// Compensation de tirage : mode de l'objet, comparaison nominal/prédit, décalage entre couleurs,
// étalonnage sur pièce-test. Adaptateur sur PullController.
Rectangle {
    id: panel
    objectName: "pullPanel"
    property var controller
    readonly property bool ready: controller !== undefined && controller !== null
    color: Theme.panel
    border.color: Theme.divider
    radius: Theme.radius

    Flickable {
        anchors.fill: parent
        anchors.margins: 10
        contentHeight: column.implicitHeight
        clip: true
        ColumnLayout {
            id: column
            width: parent.width - 12
            spacing: 8
            SectionLabel { text: qsTr("COMPENSATION DE TIRAGE") }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.faint
                font.family: Theme.fontFamily
                text: panel.ready ? panel.controller.limits : ""
            }
            StudioComboBox {
                objectName: "pullMode"
                Layout.fillWidth: true
                model: panel.ready ? panel.controller.modes : []
                currentIndex: panel.ready ? Math.max(0, panel.controller.mode) : 0
                enabled: panel.ready && panel.controller.mode >= 0
                onActivated: function(i) { panel.controller.setMode(i) }
            }
            StudioComboBox {
                objectName: "pullFabric"
                Layout.fillWidth: true
                model: panel.ready ? panel.controller.fabrics : []
                onActivated: function(i) { panel.controller.setFabric(model[i]) }
            }
            StudioButton { objectName: "pullCompare"; text: qsTr("Comparer nominal et prédit"); onClicked: panel.controller.compare() }
            Text {
                objectName: "pullSummary"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.foreground
                font.family: Theme.fontFamily
                text: panel.ready && panel.controller.comparison.summary ? panel.controller.comparison.summary : ""
            }
            StudioButton { objectName: "pullRegistration"; text: qsTr("Prédire le décalage entre couleurs"); enabled: panel.ready && !panel.controller.busy; onClicked: panel.controller.predictRegistration(0) }
            Text {
                objectName: "pullRegistrationText"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.faint
                font.family: Theme.fontFamily
                text: panel.ready && panel.controller.registration.text ? panel.controller.registration.text : ""
            }
            SectionLabel { text: qsTr("PIÈCE-TEST") }
            Text {
                objectName: "pullFit"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.foreground
                font.family: Theme.fontFamily
                text: panel.ready ? qsTr("%1 mesure(s), erreur quadratique %2 mm").arg(panel.controller.measuredCount).arg(panel.controller.rmsMm.toFixed(3)) : ""
            }
            Text {
                objectName: "pullMessage"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: panel.ready && !panel.controller.lastOk ? Theme.error : Theme.faint
                font.family: Theme.fontFamily
                text: panel.ready ? panel.controller.message : ""
            }
        }
    }
}
