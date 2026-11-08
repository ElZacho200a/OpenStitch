// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."

// « Corriger » et « Réduire les fils » : proposition, aperçu, approbation, une étape annulable.
// Adaptateur SANS logique métier au-dessus de FixController. Une proposition périmée (document
// modifié depuis le calcul) est signalée et refusée à l'application.
Rectangle {
    id: panel
    objectName: "fixPanel"
    property var controller
    readonly property bool ready: controller !== undefined && controller !== null
    color: Theme.panel
    border.color: Theme.divider
    radius: Theme.radius

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

            RowLayout {
                Layout.fillWidth: true
                SectionLabel { text: qsTr("CORRIGER"); Layout.fillWidth: true }
                Controls.BusyIndicator { implicitWidth: 18; implicitHeight: 18; running: panel.ready && panel.controller.busy; visible: running }
                StudioButton { text: qsTr("Annuler"); quiet: true; visible: panel.ready && panel.controller.busy; onClicked: panel.controller.cancel() }
            }

            StudioButton { objectName: "fixAnalyze"; text: qsTr("Chercher les corrections"); enabled: panel.ready && !panel.controller.busy; onClicked: panel.controller.analyze() }
            Text {
                objectName: "fixStale"
                Layout.fillWidth: true
                visible: panel.ready && panel.controller.planStale
                wrapMode: Text.WordWrap
                text: qsTr("Le motif a changé depuis l'analyse : relancez-la avant d'appliquer.")
                color: Theme.warning
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
            }
            StudioButton {
                objectName: "fixApplyAll"
                text: qsTr("Corriger tout ce qui est sûr (%1)").arg(panel.ready ? panel.controller.safeCount : 0)
                enabled: panel.ready && panel.controller.planReady && !panel.controller.planStale && panel.controller.safeCount > 0
                onClicked: panel.controller.applyAllSafe()
            }
            Repeater {
                model: panel.ready ? panel.controller.proposals : []
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    objectName: "fixProposal" + modelData.index
                    Layout.fillWidth: true
                    implicitHeight: rowContent.implicitHeight + 12
                    radius: Theme.radius
                    color: Theme.raised
                    ColumnLayout {
                        id: rowContent
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 4
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: row.modelData.message; color: Theme.foreground; font.family: Theme.fontFamily; font.pixelSize: Theme.smallSize }
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "→ " + row.modelData.preview; color: Theme.muted; font.family: Theme.fontFamily; font.pixelSize: Theme.smallSize }
                        StudioButton {
                            objectName: "fixApply" + row.modelData.index
                            text: row.modelData.label
                            enabled: !panel.controller.planStale
                            onClicked: panel.controller.applyFix(row.modelData.index)
                        }
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.divider }

            SectionLabel { text: qsTr("RÉDUIRE LES FILS") }
            RowLayout {
                Layout.fillWidth: true
                Text { text: qsTr("Nombre de fils visé"); color: Theme.muted; font.family: Theme.fontFamily; font.pixelSize: Theme.smallSize }
                StudioSpinBox { id: targetThreads; objectName: "fixTargetThreads"; from: 1; to: 64; value: 6 }
            }
            StudioButton { objectName: "fixPreviewReduction"; text: qsTr("Aperçu de la réduction"); enabled: panel.ready && !panel.controller.busy; onClicked: panel.controller.previewReduction(targetThreads.value) }
            Text {
                objectName: "fixReductionSummary"
                Layout.fillWidth: true
                visible: panel.ready && panel.controller.reductionReady
                wrapMode: Text.WordWrap
                text: panel.ready ? qsTr("%1 fil(s) → %2 ; %3 objet(s) recolorié(s)").arg(panel.controller.threadsBefore).arg(panel.controller.threadsAfter).arg(panel.controller.objectsRecoloured)
                                    + (panel.controller.reductionStale ? qsTr(" — périmé, recalculez") : "") : ""
                color: panel.ready && panel.controller.reductionStale ? Theme.warning : Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
            }
            StudioButton {
                objectName: "fixApplyReduction"
                text: qsTr("Appliquer la réduction")
                enabled: panel.ready && panel.controller.reductionReady && !panel.controller.reductionStale && panel.controller.objectsRecoloured > 0
                onClicked: panel.controller.applyReduction()
            }

            Text {
                objectName: "fixMessage"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: panel.ready && !panel.controller.lastOk ? Theme.error : Theme.faint
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
                text: panel.ready ? panel.controller.message : ""
            }
        }
    }
}
