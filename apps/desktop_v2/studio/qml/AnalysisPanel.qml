// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."

// Diagnostics, density / fabric-risk maps, order optimisation, hoops and machine, realistic
// render. Adapter over AnalysisController: long computations run on a worker over a snapshot,
// results carry their document revision and are flagged stale after any edit.
Rectangle {
    id: panel
    objectName: "analysisPanel"
    property var controller
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
                SectionLabel { text: qsTr("ANALYSE"); Layout.fillWidth: true }
                Controls.BusyIndicator { implicitWidth: 18; implicitHeight: 18; running: panel.controller.busy; visible: running }
                Text { text: panel.controller.busyLabel; color: Theme.faint; font.family: Theme.fontFamily; font.pixelSize: Theme.smallSize; visible: panel.controller.busy }
                StudioButton { text: qsTr("Annuler"); quiet: true; visible: panel.controller.busy; onClicked: panel.controller.cancel() }
            }

            StudioButton { objectName: "diagnoseButton"; text: qsTr("Analyser le motif"); onClicked: panel.controller.runDiagnostics() }
            Text {
                objectName: "diagnosticsSummary"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: panel.controller.diagnosticsSummary
                color: panel.controller.diagnosticsStale ? Theme.warning : Theme.muted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
            }
            Repeater {
                model: panel.controller.findings
                delegate: Rectangle {
                    id: finding
                    required property var modelData
                    objectName: "finding" + modelData.index
                    Layout.fillWidth: true
                    implicitHeight: findingText.implicitHeight + 10
                    radius: Theme.radius
                    color: findingArea.containsMouse ? Theme.hover : Theme.raised
                    MouseArea {
                        id: findingArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: panel.controller.centerOnFinding(finding.modelData.index)
                    }
                    Text {
                        id: findingText
                        anchors.fill: parent
                        anchors.margins: 5
                        wrapMode: Text.WordWrap
                        text: "[" + finding.modelData.severity + "] " + finding.modelData.message
                              + (finding.modelData.suggestion.length > 0 ? "\n→ " + finding.modelData.suggestion : "")
                        color: finding.modelData.severity === "error" ? Theme.error : finding.modelData.severity === "warning" ? Theme.warning : Theme.muted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.smallSize
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.divider }
            SectionLabel { text: qsTr("CARTES") }
            Flow {
                Layout.fillWidth: true
                spacing: 4
                StudioButton { objectName: "densityButton"; text: qsTr("Épaisseurs de fil (densité)"); onClicked: panel.controller.computeDensity() }
                StudioButton { text: qsTr("Effacer"); quiet: true; visible: panel.controller.densityReady; onClicked: panel.controller.clearDensity() }
                StudioButton { objectName: "riskButton"; text: qsTr("Risque de tirage du tissu"); onClicked: panel.controller.computeFabricRisk() }
                StudioButton { text: qsTr("Effacer"); quiet: true; visible: panel.controller.riskReady; onClicked: panel.controller.clearFabricRisk() }
            }
            Text { objectName: "densitySummary"; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: panel.controller.densitySummary; color: Theme.muted; font.family: Theme.fontFamily; font.pixelSize: Theme.smallSize }
            Text { objectName: "riskSummary"; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: panel.controller.riskVerdict; color: Theme.muted; font.family: Theme.fontFamily; font.pixelSize: Theme.smallSize }

            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.divider }
            SectionLabel { text: qsTr("STATISTIQUES ET FIDÉLITÉ") }
            Text {
                objectName: "statisticsText"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: panel.controller.statistics.text
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
            }
            Flow {
                Layout.fillWidth: true
                spacing: 4
                StudioButton {
                    objectName: "fidelityButton"
                    text: qsTr("Fidélité à l'image")
                    tooltip: qsTr("Compare l'image source et le fil posé : trous, détails perdus, mauvaises couleurs")
                    enabled: !panel.controller.busy
                    onClicked: panel.controller.measureFidelity()
                }
                StudioButton { text: qsTr("Effacer"); quiet: true; visible: panel.controller.fidelityReady; onClicked: panel.controller.clearFidelity() }
            }
            Text {
                objectName: "fidelityStale"
                Layout.fillWidth: true
                visible: panel.controller.fidelityStale
                wrapMode: Text.WordWrap
                text: qsTr("Le motif a changé depuis cette mesure : relancez-la.")
                color: Theme.warning
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
            }
            Text {
                objectName: "fidelityText"
                Layout.fillWidth: true
                visible: panel.controller.fidelityReady
                wrapMode: Text.WordWrap
                text: panel.controller.fidelityText
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
            }
            Image {
                objectName: "fidelityMap"
                Layout.fillWidth: true
                Layout.preferredHeight: visible ? width * 0.75 : 0
                visible: panel.controller.fidelityReady && panel.controller.fidelityMapUrl.length > 0
                source: panel.controller.fidelityMapUrl
                fillMode: Image.PreserveAspectFit
                cache: false
            }

            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.divider }
            SectionLabel { text: qsTr("OPTIMISER L'ORDRE") }
            RowLayout {
                Layout.fillWidth: true
                StudioComboBox { id: strategyBox; objectName: "strategyBox"; Layout.fillWidth: true; model: panel.controller.strategies }
                StudioCheckBox { id: improveBox; text: qsTr("Affiner"); palette.windowText: Theme.muted }
                StudioButton { objectName: "proposeButton"; text: qsTr("Calculer"); onClicked: panel.controller.proposeOrder(strategyBox.currentIndex, improveBox.checked) }
            }
            Repeater {
                model: panel.controller.proposalSummary
                delegate: Text { required property string modelData; text: modelData; color: Theme.foreground; font.family: Theme.fontFamily; font.pixelSize: Theme.smallSize }
            }
            Text {
                visible: panel.controller.proposalStale
                text: qsTr("Proposition périmée : le motif a changé, recalculez.")
                color: Theme.warning; font.family: Theme.fontFamily; font.pixelSize: Theme.smallSize
            }
            RowLayout {
                visible: panel.controller.proposalReady
                StudioButton { objectName: "applyProposalButton"; emphasized: true; text: qsTr("Appliquer"); onClicked: panel.controller.applyProposal() }
                StudioButton { text: qsTr("Ignorer"); quiet: true; onClicked: panel.controller.discardProposal() }
            }

            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.divider }
            SectionLabel { text: qsTr("CADRE ET MACHINE") }
            Text {
                objectName: "currentHoopSummary"
                Layout.fillWidth: true
                text: qsTr("Cadre actuel : %1 × %2 mm")
                      .arg(panel.controller.currentHoop.widthMm)
                      .arg(panel.controller.currentHoop.heightMm)
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
            }
            RowLayout {
                Layout.fillWidth: true
                // This is a choice for a future change, not a readout of the current hoop.
                // Defaulting to the library's first item misleadingly displayed 50×50 for
                // projects whose actual hoop was 100×100 (QA-STUDIO-001 HOOP-001).
                StudioComboBox {
                    id: hoopBox
                    objectName: "hoopBox"
                    Layout.fillWidth: true
                    model: panel.controller.libraryHoops
                    textRole: "name"
                    currentIndex: -1
                    displayText: currentIndex < 0 ? qsTr("Choisir un cadre à appliquer…") : currentText
                }
                StudioButton {
                    objectName: "applyHoopButton"
                    text: qsTr("Appliquer")
                    enabled: hoopBox.currentIndex >= 0
                    onClicked: panel.controller.applyLibraryHoop(
                                   panel.controller.libraryHoops[hoopBox.currentIndex].id, 0)
                }
            }
            RowLayout {
                Layout.fillWidth: true
                StudioComboBox {
                    id: machineBox
                    objectName: "machineBox"
                    Layout.fillWidth: true
                    model: panel.controller.machines
                    textRole: "name"
                    currentIndex: { for (var i = 0; i < model.length; ++i) if (model[i].id === panel.controller.machineId) return i; return -1 }
                    onActivated: panel.controller.setMachine(model[currentIndex].id)
                }
                StudioButton { objectName: "checkHoopButton"; text: qsTr("Vérifier les limites"); onClicked: panel.controller.checkHoop() }
            }
            Repeater {
                model: panel.controller.userHoops
                delegate: RowLayout {
                    id: mine
                    required property var modelData
                    Layout.fillWidth: true
                    Text { Layout.fillWidth: true; elide: Text.ElideRight; text: mine.modelData.name + "  " + mine.modelData.widthMm + "×" + mine.modelData.heightMm; color: Theme.foreground; font.family: Theme.fontFamily; font.pixelSize: Theme.smallSize }
                    StudioButton { text: qsTr("Appliquer"); quiet: true; onClicked: panel.controller.applyUserHoop(mine.modelData.name) }
                    StudioButton { text: qsTr("Retirer"); quiet: true; onClicked: panel.controller.removeUserHoop(mine.modelData.name) }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                StudioTextField { id: hoopName; objectName: "hoopNameField"; Layout.fillWidth: true; placeholderText: qsTr("Nom du cadre actuel") }
                StudioButton { objectName: "saveHoopButton"; text: qsTr("Enregistrer"); onClicked: { if (panel.controller.saveCurrentHoopAs(hoopName.text)) hoopName.text = "" } }
            }

            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.divider }
            SectionLabel { text: qsTr("RENDU RÉALISTE") }
            Flow {
                Layout.fillWidth: true
                spacing: 4
                Repeater {
                    model: panel.controller.renderPresets
                    delegate: StudioButton {
                        required property var modelData
                        text: modelData.label
                        active: panel.controller.renderSettings.preset === modelData.id
                        onClicked: panel.controller.applyRenderPreset(modelData.id)
                    }
                }
            }
            RowLayout {
                StudioButton { objectName: "realisticButton"; text: qsTr("Rendu de référence (CPU) / Exporter le rendu"); tooltip: qsTr("Calcule l'image de référence avec le moteur CPU ; le rendu réaliste interactif se bascule dans la barre au-dessus du canevas"); onClicked: panel.controller.computeRealistic() }
                StudioButton { text: qsTr("Revenir aux points"); quiet: true; visible: panel.controller.realisticReady; onClicked: panel.controller.clearRealistic() }
            }

            Text {
                objectName: "analysisMessage"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: panel.controller.message
                color: panel.controller.lastOk ? Theme.success : Theme.error
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
            }
        }
    }
}
